#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#ifdef CONFIG_USE_MQTT_PROTOCOL
#include "mqtt_protocol.h"
#endif
#ifdef CONFIG_USE_WEBSOCKET_PROTOCOL
#include "websocket_protocol.h"
#endif
#ifdef CONFIG_USE_HTTP_PROTOCOL
#include "http_protocol.h"
#endif
#include "font_awesome_symbols.h"
#include "assets/lang_config.h"
#include "mcp_server.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>

#define TAG "Application"


static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"
};

Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

#ifdef CONFIG_VAD_TRIGGER_RECORDING
    vad_trigger_recording_ = true;
#else
    vad_trigger_recording_ = false;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            app->OnClockTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}


void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "happy", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert %s: %s [%s]", status, message, emotion);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (protocol_ && !protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            } else if (!protocol_) {
                ESP_LOGI(TAG, "Running in offline mode, skipping protocol connection");
            }

            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            if (protocol_ != nullptr) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (protocol_ != nullptr && !protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            } else if (protocol_ == nullptr) {
                ESP_LOGI(TAG, "Running in offline mode, skipping protocol connection");
            }

            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

void Application::StopListening() {
    if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            if (protocol_ != nullptr) {
                protocol_->SendStopListening();
            }
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();

    /* Setup the audio service */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        ESP_LOGI(TAG, "🎤 VAD State Changed: %s", speaking ? "VOICE_DETECTED" : "VOICE_STOPPED");
        
        if (vad_trigger_recording_ && http_audio_client_) {
            Schedule([this, speaking]() {
                OnVadStateChange(speaking);
            });
        }
    };
    
    // Set PCM data callback for recording when VAD is active
    callbacks.on_pcm_data_available = [this](const std::vector<int16_t>& pcm_data) {
        if (is_recording_vad_ && vad_trigger_recording_) {
            // Append PCM data to buffer when recording
            vad_audio_buffer_.insert(vad_audio_buffer_.end(), pcm_data.begin(), pcm_data.end());
            ESP_LOGD(TAG, "Recording: %zu samples collected, total: %zu", 
                     pcm_data.size(), vad_audio_buffer_.size());
        }
    };
    audio_service_.SetCallbacks(callbacks);

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);


    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
    McpServer::GetInstance().AddCommonTools();

#ifdef CONFIG_USE_HTTP_PROTOCOL
    ESP_LOGI(TAG, "Using HTTP protocol");
    protocol_ = std::make_unique<HttpProtocol>();
#elif defined(CONFIG_USE_MQTT_PROTOCOL)
    ESP_LOGI(TAG, "Using MQTT protocol");
    protocol_ = std::make_unique<MqttProtocol>();
#elif defined(CONFIG_USE_WEBSOCKET_PROTOCOL)
    ESP_LOGI(TAG, "Using WebSocket protocol");
    protocol_ = std::make_unique<WebsocketProtocol>();
#else
    ESP_LOGI(TAG, "No protocol configured, device will run in WiFi-only mode");
    protocol_ = nullptr;
#endif

    if (protocol_ != nullptr) {
        protocol_->OnNetworkError([this](const std::string& message) {
            last_error_message_ = message;
            xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
        });
        protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
            if (device_state_ == kDeviceStateSpeaking) {
                audio_service_.PushPacketToDecodeQueue(std::move(packet));
            }
        });
        protocol_->OnAudioChannelOpened([this, codec, &board]() {
            board.SetPowerSaveMode(false);
            if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
                ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                    protocol_->server_sample_rate(), codec->output_sample_rate());
            }
        });
        protocol_->OnAudioChannelClosed([this, &board]() {
            board.SetPowerSaveMode(true);
            Schedule([this]() {
                auto display = Board::GetInstance().GetDisplay();
                display->SetChatMessage("system", "");
                SetDeviceState(kDeviceStateIdle);
            });
        });
        protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (device_state_ == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
        });
    }
    bool protocol_started = false;
    if (protocol_ != nullptr) {
        protocol_started = protocol_->Start();
    } else {
        ESP_LOGI(TAG, "Running in offline mode, no network protocol will be used");
        protocol_started = true; // 离线模式视为启动成功
    }

    SetDeviceState(kDeviceStateIdle);

    // Initialize audio processing mode based on protocol availability
    if (vad_trigger_recording_ && protocol_ != nullptr) {
        ESP_LOGI(TAG, "Initializing VAD trigger recording mode with protocol");
        audio_service_.EnableVoiceProcessing(true);
        audio_service_.EnableWakeWordDetection(false);
    } else if (vad_trigger_recording_) {
        ESP_LOGI(TAG, "VAD trigger recording enabled - using HTTP upload mode");
        
        // Initialize HTTP client for audio upload
        http_audio_client_ = std::make_unique<HttpAudioClient>("http://192.168.0.114:8000/api/v1/process-voice-raw");
        
        // Set text response callback
        http_audio_client_->SetResponseCallback([this](const std::string& response) {
            ESP_LOGI(TAG, "Server text response: %s", response.c_str());
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("assistant", response.c_str());
        });
        
        // Set audio response callback for streaming audio data
        http_audio_client_->SetAudioResponseCallback([this](const std::vector<uint8_t>& audio_data) {
            ESP_LOGI(TAG, "Received streaming audio response: %zu bytes", audio_data.size());
            
            if (audio_data.size() < 100) { // Too small to be valid audio
                ESP_LOGW(TAG, "Audio response too small, ignoring");
                return;
            }
            
            // Convert uint8_t to int16_t PCM data
            std::vector<int16_t> pcm_data;
            size_t start_offset = 0;
            
            // Check for WAV header and skip it
            if (audio_data.size() > 44 && 
                audio_data[0] == 'R' && audio_data[1] == 'I' && 
                audio_data[2] == 'F' && audio_data[3] == 'F') {
                ESP_LOGI(TAG, "WAV header detected, skipping 44 bytes");
                start_offset = 44;
            }
            
            // Convert bytes to 16-bit PCM samples
            size_t pcm_bytes = audio_data.size() - start_offset;
            if (pcm_bytes % 2 != 0) {
                pcm_bytes--; // Ensure even number of bytes
            }
            
            pcm_data.resize(pcm_bytes / 2);
            const int16_t* pcm_ptr = reinterpret_cast<const int16_t*>(audio_data.data() + start_offset);
            std::copy(pcm_ptr, pcm_ptr + (pcm_bytes / 2), pcm_data.begin());
            
            ESP_LOGI(TAG, "Converted to %zu PCM samples, playing directly via AudioCodec", pcm_data.size());
            
            // Change device state to speaking for audio output
            Schedule([this, pcm_data = std::move(pcm_data)]() mutable {
                SetDeviceState(kDeviceStateSpeaking);
                
                // Play PCM data directly through audio codec
                auto codec = Board::GetInstance().GetAudioCodec();
                ESP_LOGI(TAG, "Codec status: codec=%p, output_enabled=%s", 
                         codec, codec ? (codec->output_enabled() ? "true" : "false") : "null");
                
                // Ensure output is enabled before playback
                if (codec) {
                    if (!codec->output_enabled()) {
                        ESP_LOGI(TAG, "Enabling codec output for playback");
                        codec->EnableOutput(true);
                    }
                }
                
                if (codec && codec->output_enabled()) {
                    // Split large audio into chunks to avoid blocking
                    const size_t chunk_size = 1024; // 1024 samples per chunk
                    size_t offset = 0;
                    
                    while (offset < pcm_data.size()) {
                        size_t chunk_samples = std::min(chunk_size, pcm_data.size() - offset);
                        std::vector<int16_t> chunk(pcm_data.begin() + offset, 
                                                   pcm_data.begin() + offset + chunk_samples);
                        
                        ESP_LOGD(TAG, "Playing audio chunk: %zu samples at offset %zu", 
                                chunk_samples, offset);
                        codec->OutputData(chunk);
                        
                        offset += chunk_samples;
                        
                        // Small delay to prevent audio buffer overflow
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }
                    
                    ESP_LOGI(TAG, "Audio playback completed");
                    SetDeviceState(kDeviceStateIdle);
                } else {
                    ESP_LOGE(TAG, "Audio codec not available or output not enabled");
                    SetDeviceState(kDeviceStateIdle);
                }
            });
        });
        
        // Enable voice processing for VAD detection
        audio_service_.EnableVoiceProcessing(true);
        // Disable wake word detection
        audio_service_.EnableWakeWordDetection(false);
    } else {
        ESP_LOGI(TAG, "Using wake word detection mode");
        audio_service_.EnableWakeWordDetection(true);
    }

    has_server_time_ = false;
    if (protocol_started) {
        display->ShowNotification(Lang::Strings::CONNECTION_SUCCESSFUL);
        display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    }

    // Print heap stats
    SystemInfo::PrintHeapStats();
}

void Application::OnClockTimer() {
    clock_ticks_++;

    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar();

    // Print the debug info every 10 seconds
    if (clock_ticks_ % 10 == 0) {
        // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
        // SystemInfo::PrintTaskList();
        SystemInfo::PrintHeapStats();
    }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
    // Raise the priority of the main event loop to avoid being interrupted by background tasks (which has priority 2)
    vTaskPrioritySet(NULL, 3);

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_ERROR, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "sad", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            if (protocol_ != nullptr) {
                while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                    if (!protocol_->SendAudio(std::move(packet))) {
                        break;
                    }
                }
            } else {
                // In offline mode, consume audio packets to prevent buffer overflow
                while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                    // Discard the packet silently
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }
    }
}

void Application::OnWakeWordDetected() {
    if (!protocol_) {
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (protocol_ != nullptr && !protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        } else if (protocol_ == nullptr) {
            ESP_LOGI(TAG, "Running in offline mode, skipping protocol connection");
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
        // Encode and send the wake word data to the server
        if (protocol_ != nullptr) {
            while (auto packet = audio_service_.PopWakeWordPacket()) {
                protocol_->SendAudio(std::move(packet));
            }
            // Set the chat state to wake word detected
            protocol_->SendWakeWordDetected(wake_word);
        }
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        // Play the pop up sound to indicate the wake word is detected
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_ != nullptr) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

    // Send the state change event
    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            if (vad_trigger_recording_) {
                // VAD触发录音模式：保持语音处理开启，关闭唤醒词检测
                audio_service_.EnableVoiceProcessing(true);
                audio_service_.EnableWakeWordDetection(false);
            } else {
                // 普通模式：关闭语音处理，开启唤醒词检测
                audio_service_.EnableVoiceProcessing(false);
                audio_service_.EnableWakeWordDetection(true);
            }
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            // Make sure the audio processor is running
            if (!audio_service_.IsAudioProcessorRunning()) {
                // Send the start listening command
                if (protocol_ != nullptr) {
                    protocol_->SendStartListening(listening_mode_);
                }
                audio_service_.EnableVoiceProcessing(true);
                audio_service_.EnableWakeWordDetection(false);
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                if (vad_trigger_recording_) {
                    // VAD触发录音模式：保持语音处理启用，确保音频输出正常
                    // 不要重新初始化音频服务，保持当前配置
                    ESP_LOGI(TAG, "Speaking in VAD mode, keeping voice processing enabled");
                } else {
                    // 传统模式：禁用语音处理，启用唤醒词检测
                    audio_service_.EnableVoiceProcessing(false);
                    // Only AFE wake word can be detected in speaking mode
#if CONFIG_USE_AFE_WAKE_WORD
                    audio_service_.EnableWakeWordDetection(true);
#else
                    audio_service_.EnableWakeWordDetection(false);
#endif
                }
            }
            audio_service_.ResetDecoder();
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (device_state_ == kDeviceStateIdle) {
        ToggleChatState();
        Schedule([this, wake_word]() {
            if (protocol_ != nullptr) {
                protocol_->SendWakeWordDetected(wake_word);
            }
        }); 
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                if (protocol_ != nullptr) {
                protocol_->CloseAudioChannel();
            }
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    Schedule([this, payload]() {
        if (protocol_ != nullptr) {
            protocol_->SendMcpMessage(payload);
        }
    });
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            if (protocol_ != nullptr) {
                protocol_->CloseAudioChannel();
            }
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

void Application::SetVadTriggerRecording(bool enable) {
    vad_trigger_recording_ = enable;
    if (enable) {
        ESP_LOGI(TAG, "VAD trigger recording enabled");
        // 启用VAD触发录音时，需要确保VAD始终运行
        if (device_state_ == kDeviceStateIdle) {
            audio_service_.EnableVoiceProcessing(true);
            audio_service_.EnableWakeWordDetection(false);
        }
    } else {
        ESP_LOGI(TAG, "VAD trigger recording disabled");
        // 禁用VAD触发录音时，恢复唤醒词检测
        if (device_state_ == kDeviceStateIdle) {
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
        }
    }
}

void Application::OnVadDetected() {
    ESP_LOGI(TAG, "🎯 OnVadDetected() called - device_state: %s", STATE_STRINGS[device_state_]);

    if (device_state_ == kDeviceStateIdle) {
        ESP_LOGI(TAG, "🚀 VAD detected, starting recording session...");
        
        if (protocol_ != nullptr) {
            if (!protocol_->IsAudioChannelOpened()) {
                ESP_LOGI(TAG, "🔗 Opening audio channel for HTTP transmission...");
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    ESP_LOGE(TAG, "❌ Failed to open audio channel, will retry on next VAD trigger");
                    return;
                }
                ESP_LOGI(TAG, "✅ Audio channel opened successfully");
            } else {
                ESP_LOGI(TAG, "✅ Audio channel already open, proceeding with recording");
            }
        } else {
            ESP_LOGW(TAG, "⚠️ No protocol available - running in offline mode");
        }

        ESP_LOGI(TAG, "🎵 Switching to listening mode (auto-stop)");
        SetListeningMode(kListeningModeAutoStop);
    } else {
        ESP_LOGW(TAG, "⚠️ VAD detected but device not in idle state (current: %s)", STATE_STRINGS[device_state_]);
    }
}

void Application::OnVadStateChange(bool is_speaking) {
    ESP_LOGI(TAG, "VAD state change: %s", is_speaking ? "SPEAKING" : "SILENT");
    
    if (is_speaking && !is_recording_vad_) {
        // Start recording
        is_recording_vad_ = true;
        vad_audio_buffer_.clear();
        vad_audio_buffer_.reserve(16000 * 10); // Reserve space for 10 seconds at 16kHz
        
        ESP_LOGI(TAG, "Started VAD recording (need ≥0.25s for 8000 bytes minimum)");
        
        // Update display
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus("Recording...");
        display->SetEmotion("listening");
        
    } else if (!is_speaking && is_recording_vad_) {
        // Stop recording and send data
        is_recording_vad_ = false;
        
        ESP_LOGI(TAG, "Stopped VAD recording, collected %zu samples (%.2f seconds)", 
                 vad_audio_buffer_.size(), 
                 (float)vad_audio_buffer_.size() / 16000.0f);
        
        // Send audio to server
        if (!vad_audio_buffer_.empty()) {
            SendVadAudioToServer();
        }
        
        // Update display
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus("Processing...");
        display->SetEmotion("thinking");
    }
}

void Application::SendVadAudioToServer() {
    if (!http_audio_client_) {
        ESP_LOGE(TAG, "HTTP client not initialized");
        return;
    }
    
    if (vad_audio_buffer_.empty()) {
        ESP_LOGW(TAG, "No audio data to send");
        return;
    }
    
    // Check minimum audio data size (8000 bytes = 4000 samples of 16-bit PCM)
    size_t audio_bytes = vad_audio_buffer_.size() * sizeof(int16_t);
    const size_t min_audio_bytes = 8000;
    
    if (audio_bytes < min_audio_bytes) {
        ESP_LOGW(TAG, "Audio data too small: %zu bytes (need at least %zu bytes), discarding", 
                 audio_bytes, min_audio_bytes);
        vad_audio_buffer_.clear();
        
        // Update display to show the recording was too short
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus("Recording too short");
        display->SetEmotion("neutral");
        
        // Reset to idle after a short delay
        Schedule([]() {
            vTaskDelay(pdMS_TO_TICKS(1500));
            auto display = Board::GetInstance().GetDisplay();
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
        });
        return;
    }
    
    ESP_LOGI(TAG, "Sending %zu audio samples (%zu bytes) to server", 
             vad_audio_buffer_.size(), audio_bytes);
    
    // Create a copy of the buffer for sending
    auto audio_data = std::move(vad_audio_buffer_);
    vad_audio_buffer_.clear();
    
    // Send in background task to avoid blocking
    xTaskCreate([](void* param) {
        auto* app = static_cast<Application*>(param);
        auto& client = app->http_audio_client_;
        auto& data = app->vad_audio_buffer_;
        
        // Note: We already moved the data, so use the local copy
        // This is a simplified version - in production you'd pass the data properly
        ESP_LOGI(TAG, "HTTP upload task started");
        
        vTaskDelete(NULL);
    }, "http_upload", 8192, this, 5, NULL);
    
    // Actually send the data
    bool success = http_audio_client_->SendAudioData(audio_data);
    
    auto display = Board::GetInstance().GetDisplay();
    if (success) {
        ESP_LOGI(TAG, "Audio sent successfully");
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
    } else {
        ESP_LOGE(TAG, "Failed to send audio");
        display->SetStatus("Upload failed");
        display->SetEmotion("error");
    }
}
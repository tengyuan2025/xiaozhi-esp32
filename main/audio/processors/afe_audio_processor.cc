#include "afe_audio_processor.h"
#include <esp_log.h>
#include <inttypes.h>

#define PROCESSOR_RUNNING 0x01

#define TAG "AfeAudioProcessor"

AfeAudioProcessor::AfeAudioProcessor()
    : afe_data_(nullptr) {
    event_group_ = xEventGroupCreate();
}

void AfeAudioProcessor::Initialize(AudioCodec* codec, int frame_duration_ms) {
    codec_ = codec;
    frame_samples_ = frame_duration_ms * 16000 / 1000;

    // Pre-allocate output buffer capacity
    output_buffer_.reserve(frame_samples_);

    int ref_num = codec_->input_reference() ? 1 : 0;

    std::string input_format;
    for (int i = 0; i < codec_->input_channels() - ref_num; i++) {
        input_format.push_back('M');
    }
    for (int i = 0; i < ref_num; i++) {
        input_format.push_back('R');
    }

    srmodel_list_t *models = esp_srmodel_init("model");
    char* ns_model_name = esp_srmodel_filter(models, ESP_NSNET_PREFIX, NULL);
    char* vad_model_name = esp_srmodel_filter(models, ESP_VADN_PREFIX, NULL);
    
    ESP_LOGI(TAG, "🎛️ AFE Configuration:");
    ESP_LOGI(TAG, "  📡 Input format: %s", input_format.c_str());
    ESP_LOGI(TAG, "  🎤 Input channels: %d (reference: %d)", codec_->input_channels(), ref_num);
    ESP_LOGI(TAG, "  📊 Frame samples: %d", frame_samples_);
    
    afe_config_t* afe_config = afe_config_init(input_format.c_str(), NULL, AFE_TYPE_VC, AFE_MODE_HIGH_PERF);
    afe_config->aec_mode = AEC_MODE_VOIP_HIGH_PERF;
    // Try more sensitive VAD mode for debugging
    afe_config->vad_mode = VAD_MODE_0;  // 0=normal, 1=loose, 2=strict
    afe_config->vad_min_noise_ms = 50;  // Reduced from 100ms to make more sensitive
    
    ESP_LOGI(TAG, "  🔊 VAD mode: %d", afe_config->vad_mode);
    ESP_LOGI(TAG, "  🔇 VAD min noise ms: %d", afe_config->vad_min_noise_ms);
    if (vad_model_name != nullptr) {
        afe_config->vad_model_name = vad_model_name;
    }

    if (ns_model_name != nullptr) {
        afe_config->ns_init = true;
        afe_config->ns_model_name = ns_model_name;
        afe_config->afe_ns_mode = AFE_NS_MODE_NET;
    } else {
        afe_config->ns_init = false;
    }

    afe_config->afe_perferred_core = 1;
    afe_config->afe_perferred_priority = 1;
    afe_config->agc_init = false;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

#ifdef CONFIG_USE_DEVICE_AEC
    afe_config->aec_init = true;
    afe_config->vad_init = false;
    ESP_LOGI(TAG, "  ✅ Device AEC enabled, VAD disabled");
#else
    afe_config->aec_init = false;
    afe_config->vad_init = true;
    ESP_LOGI(TAG, "  ✅ VAD enabled, Device AEC disabled");
#endif

    ESP_LOGI(TAG, "  🔧 VAD model: %s", vad_model_name ? vad_model_name : "NULL");
    ESP_LOGI(TAG, "  🔧 NS model: %s", ns_model_name ? ns_model_name : "NULL");

    afe_iface_ = esp_afe_handle_from_config(afe_config);
    afe_data_ = afe_iface_->create_from_config(afe_config);
    
    ESP_LOGI(TAG, "🚀 AFE initialized successfully, VAD init: %s", 
             afe_config->vad_init ? "ENABLED" : "DISABLED");
    
    xTaskCreate([](void* arg) {
        auto this_ = (AfeAudioProcessor*)arg;
        this_->AudioProcessorTask();
        vTaskDelete(NULL);
    }, "audio_communication", 4096, this, 3, NULL);
}

AfeAudioProcessor::~AfeAudioProcessor() {
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }
    vEventGroupDelete(event_group_);
}

size_t AfeAudioProcessor::GetFeedSize() {
    if (afe_data_ == nullptr) {
        return 0;
    }
    return afe_iface_->get_feed_chunksize(afe_data_);
}

void AfeAudioProcessor::Feed(std::vector<int16_t>&& data) {
    if (afe_data_ == nullptr) {
        ESP_LOGW(TAG, "⚠️ AFE not initialized, dropping %zu samples", data.size());
        return;
    }
    
    // Calculate audio level for debugging
    int32_t max_level = 0;
    for (size_t i = 0; i < data.size(); i++) {
        int32_t abs_val = abs(data[i]);
        if (abs_val > max_level) {
            max_level = abs_val;
        }
    }
    
    ESP_LOGD(TAG, "🎵 Feeding audio: %zu samples, max level: %" PRId32 "/32767 (%.1f%%)", 
             data.size(), max_level, (float)max_level * 100.0f / 32767.0f);
    
    afe_iface_->feed(afe_data_, data.data());
}

void AfeAudioProcessor::Start() {
    xEventGroupSetBits(event_group_, PROCESSOR_RUNNING);
}

void AfeAudioProcessor::Stop() {
    xEventGroupClearBits(event_group_, PROCESSOR_RUNNING);
    if (afe_data_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
}

bool AfeAudioProcessor::IsRunning() {
    return xEventGroupGetBits(event_group_) & PROCESSOR_RUNNING;
}

void AfeAudioProcessor::OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) {
    output_callback_ = callback;
}

void AfeAudioProcessor::OnVadStateChange(std::function<void(bool speaking)> callback) {
    vad_state_change_callback_ = callback;
}

void AfeAudioProcessor::AudioProcessorTask() {
    auto fetch_size = afe_iface_->get_fetch_chunksize(afe_data_);
    auto feed_size = afe_iface_->get_feed_chunksize(afe_data_);
    ESP_LOGI(TAG, "Audio communication task started, feed size: %d fetch size: %d",
        feed_size, fetch_size);

    while (true) {
        xEventGroupWaitBits(event_group_, PROCESSOR_RUNNING, pdFALSE, pdTRUE, portMAX_DELAY);

        auto res = afe_iface_->fetch_with_delay(afe_data_, portMAX_DELAY);
        if ((xEventGroupGetBits(event_group_) & PROCESSOR_RUNNING) == 0) {
            continue;
        }
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            if (res != nullptr) {
                ESP_LOGI(TAG, "Error code: %d", res->ret_value);
            }
            continue;
        }

        // VAD state change with detailed logging
        if (vad_state_change_callback_) {
            ESP_LOGI(TAG, "🎙️ Raw VAD state: %d, current is_speaking_: %s", 
                     res->vad_state, is_speaking_ ? "true" : "false");
            
            if (res->vad_state == VAD_SPEECH && !is_speaking_) {
                is_speaking_ = true;
                ESP_LOGI(TAG, "🔊 VAD SPEECH DETECTED! Triggering callback (silence->speech)");
                vad_state_change_callback_(true);
            } else if (res->vad_state == VAD_SILENCE && is_speaking_) {
                is_speaking_ = false;
                ESP_LOGI(TAG, "🔇 VAD SILENCE DETECTED! Triggering callback (speech->silence)");
                vad_state_change_callback_(false);
            } else {
                ESP_LOGD(TAG, "🎯 VAD state unchanged: %s", 
                         res->vad_state == VAD_SPEECH ? "SPEECH" : "SILENCE");
            }
        }

        if (output_callback_) {
            size_t samples = res->data_size / sizeof(int16_t);
            
            // Add data to buffer
            output_buffer_.insert(output_buffer_.end(), res->data, res->data + samples);
            
            // Output complete frames when buffer has enough data
            while (output_buffer_.size() >= frame_samples_) {
                if (output_buffer_.size() == frame_samples_) {
                    // If buffer size equals frame size, move the entire buffer
                    output_callback_(std::move(output_buffer_));
                    output_buffer_.clear();
                    output_buffer_.reserve(frame_samples_);
                } else {
                    // If buffer size exceeds frame size, copy one frame and remove it
                    output_callback_(std::vector<int16_t>(output_buffer_.begin(), output_buffer_.begin() + frame_samples_));
                    output_buffer_.erase(output_buffer_.begin(), output_buffer_.begin() + frame_samples_);
                }
            }
        }
    }
}

void AfeAudioProcessor::EnableDeviceAec(bool enable) {
    if (enable) {
#if CONFIG_USE_DEVICE_AEC
        afe_iface_->disable_vad(afe_data_);
        afe_iface_->enable_aec(afe_data_);
#else
        ESP_LOGE(TAG, "Device AEC is not supported");
#endif
    } else {
        afe_iface_->disable_aec(afe_data_);
        afe_iface_->enable_vad(afe_data_);
    }
}

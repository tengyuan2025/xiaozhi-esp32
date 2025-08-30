#include "doubao_protocol.h"
#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>
#include <arpa/inet.h>
#include <zlib.h>
#include <esp_system.h>

#define TAG "DoubaoProtocol"

DoubaoProtocol::DoubaoProtocol() 
    : audio_channel_opened_(false)
    , session_started_(false)
    , user_speaking_(false)
    , audio_send_task_(nullptr) {
    event_group_ = xEventGroupCreate();
    
    // Generate session ID using ESP32 random
    char uuid_str[37];
    uint32_t random1 = esp_random();
    uint32_t random2 = esp_random();
    uint32_t random3 = esp_random();
    uint32_t random4 = esp_random();
    snprintf(uuid_str, sizeof(uuid_str), "%08x-%04x-%04x-%04x-%08x%04x",
             random1, 
             (random2 >> 16) & 0xFFFF,
             (random2 & 0xFFFF) | 0x4000,  // Version 4 UUID
             (random3 >> 16 & 0x3FFF) | 0x8000,  // Variant
             random3 & 0xFFFF,
             random4);
    session_id_ = std::string(uuid_str);
    
    ESP_LOGI(TAG, "Created Doubao protocol with session ID: %s", session_id_.c_str());
}

DoubaoProtocol::~DoubaoProtocol() {
    if (audio_send_task_) {
        vTaskDelete(audio_send_task_);
    }
    vEventGroupDelete(event_group_);
}

bool DoubaoProtocol::Start() {
    ESP_LOGI(TAG, "Starting Doubao protocol");
    return ConnectToDoubao();
}

bool DoubaoProtocol::ConnectToDoubao() {
    ESP_LOGI(TAG, "Connecting to Doubao at %s", BASE_URL);
    
    // Prepare headers
    std::map<std::string, std::string> headers;
    headers["X-Api-App-ID"] = APP_ID;
    headers["X-Api-Access-Key"] = ACCESS_TOKEN;
    headers["X-Api-Resource-Id"] = RESOURCE_ID;
    headers["X-Api-App-Key"] = APP_KEY;
    
    // Create WebSocket connection
    websocket_ = std::make_unique<WebSocket>();
    websocket_->SetDataCallback(OnWebSocketData, this);
    websocket_->SetConnectedCallback(OnWebSocketConnected, this);
    websocket_->SetDisconnectedCallback(OnWebSocketDisconnected, this);
    websocket_->SetErrorCallback(OnWebSocketError, this);
    
    if (!websocket_->Connect(BASE_URL, headers)) {
        ESP_LOGE(TAG, "Failed to connect to Doubao");
        return false;
    }
    
    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(event_group_, 
        DOUBAO_CONNECTED | DOUBAO_ERROR,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    
    if (!(bits & DOUBAO_CONNECTED)) {
        ESP_LOGE(TAG, "Connection timeout or error");
        return false;
    }
    
    // Send StartConnection event
    if (!SendStartConnection()) {
        ESP_LOGE(TAG, "Failed to send StartConnection");
        return false;
    }
    
    return true;
}

bool DoubaoProtocol::OpenAudioChannel() {
    ESP_LOGI(TAG, "Opening audio channel");
    
    if (!websocket_ || !websocket_->IsConnected()) {
        ESP_LOGE(TAG, "WebSocket not connected");
        return false;
    }
    
    // Send StartSession event
    if (!SendStartSession()) {
        ESP_LOGE(TAG, "Failed to send StartSession");
        return false;
    }
    
    // Wait for session to be ready
    EventBits_t bits = xEventGroupWaitBits(event_group_,
        DOUBAO_SESSION_READY | DOUBAO_ERROR,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(5000));
    
    if (!(bits & DOUBAO_SESSION_READY)) {
        ESP_LOGE(TAG, "Session start timeout or error");
        return false;
    }
    
    audio_channel_opened_ = true;
    
    // Start audio send task
    if (!audio_send_task_) {
        xTaskCreate(AudioSendTaskEntry, "doubao_audio_send", 4096, this, 5, &audio_send_task_);
    }
    
    if (on_audio_channel_opened_) {
        on_audio_channel_opened_();
    }
    
    return true;
}

void DoubaoProtocol::CloseAudioChannel() {
    ESP_LOGI(TAG, "Closing audio channel");
    
    audio_channel_opened_ = false;
    
    if (session_started_) {
        SendFinishSession();
        session_started_ = false;
    }
    
    if (on_audio_channel_closed_) {
        on_audio_channel_closed_();
    }
}

bool DoubaoProtocol::IsAudioChannelOpened() const {
    return audio_channel_opened_;
}

bool DoubaoProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (!audio_channel_opened_ || !packet) {
        return false;
    }
    
    // Convert OPUS to PCM if needed (Doubao expects PCM)
    // For now, assume the packet contains PCM data
    // The audio service should be configured to send PCM for Doubao
    
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    audio_send_queue_.push_back(packet->payload);
    
    return true;
}

void DoubaoProtocol::SendStartListening(ListeningMode mode) {
    ESP_LOGI(TAG, "Start listening mode: %d", mode);
    // Doubao handles VAD automatically, no need to send explicit start
}

void DoubaoProtocol::SendStopListening() {
    ESP_LOGI(TAG, "Stop listening");
    // Doubao handles VAD automatically, no need to send explicit stop
}

bool DoubaoProtocol::SendText(const std::string& text) {
    ESP_LOGI(TAG, "Sending text query: %s", text.c_str());
    
    // Build ChatTextQuery event
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "content", text.c_str());
    
    char* json_str = cJSON_PrintUnformatted(root);
    std::string payload(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    
    auto message = BuildMessage(MSG_TYPE_CLIENT_REQUEST, FLAG_HAS_EVENT,
                                DOUBAO_EVENT_CHAT_TEXT_QUERY, session_id_,
                                (const uint8_t*)payload.c_str(), payload.size(),
                                true, true);
    
    return websocket_->Send(message.data(), message.size(), true);
}

bool DoubaoProtocol::SendStartConnection() {
    ESP_LOGI(TAG, "Sending StartConnection");
    
    std::string payload = "{}";
    auto message = BuildMessage(MSG_TYPE_CLIENT_REQUEST, FLAG_HAS_EVENT,
                                DOUBAO_EVENT_START_CONNECTION, "",
                                (const uint8_t*)payload.c_str(), payload.size(),
                                true, true);
    
    return websocket_->Send(message.data(), message.size(), true);
}

bool DoubaoProtocol::SendStartSession() {
    ESP_LOGI(TAG, "Sending StartSession");
    
    // Build StartSession JSON payload
    cJSON* root = cJSON_CreateObject();
    
    // ASR configuration
    cJSON* asr = cJSON_CreateObject();
    cJSON* asr_extra = cJSON_CreateObject();
    cJSON_AddNumberToObject(asr_extra, "end_smooth_window_ms", 1000);
    cJSON_AddItemToObject(asr, "extra", asr_extra);
    cJSON_AddItemToObject(root, "asr", asr);
    
    // TTS configuration - request PCM format for easier playback
    cJSON* tts = cJSON_CreateObject();
    cJSON* audio_config = cJSON_CreateObject();
    cJSON_AddNumberToObject(audio_config, "channel", 1);
    cJSON_AddStringToObject(audio_config, "format", "pcm_s16le");
    cJSON_AddNumberToObject(audio_config, "sample_rate", 24000);
    cJSON_AddItemToObject(tts, "audio_config", audio_config);
    cJSON_AddStringToObject(tts, "speaker", "zh_female_vv_jupiter_bigtts");
    cJSON_AddItemToObject(root, "tts", tts);
    
    // Dialog configuration
    cJSON* dialog = cJSON_CreateObject();
    cJSON_AddStringToObject(dialog, "bot_name", "豆包");
    if (!dialog_id_.empty()) {
        cJSON_AddStringToObject(dialog, "dialog_id", dialog_id_.c_str());
    }
    cJSON_AddItemToObject(root, "dialog", dialog);
    
    char* json_str = cJSON_PrintUnformatted(root);
    std::string payload(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "StartSession payload: %s", payload.c_str());
    
    auto message = BuildMessage(MSG_TYPE_CLIENT_REQUEST, FLAG_HAS_EVENT,
                                DOUBAO_EVENT_START_SESSION, session_id_,
                                (const uint8_t*)payload.c_str(), payload.size(),
                                true, true);
    
    return websocket_->Send(message.data(), message.size(), true);
}

bool DoubaoProtocol::SendFinishSession() {
    ESP_LOGI(TAG, "Sending FinishSession");
    
    std::string payload = "{}";
    auto message = BuildMessage(MSG_TYPE_CLIENT_REQUEST, FLAG_HAS_EVENT,
                                DOUBAO_EVENT_FINISH_SESSION, session_id_,
                                (const uint8_t*)payload.c_str(), payload.size(),
                                true, true);
    
    return websocket_->Send(message.data(), message.size(), true);
}

bool DoubaoProtocol::SendTaskRequest(const uint8_t* pcm_data, size_t len) {
    if (!audio_channel_opened_ || !pcm_data || len == 0) {
        return false;
    }
    
    // Build TaskRequest message with audio data
    auto message = BuildMessage(MSG_TYPE_AUDIO_REQUEST, FLAG_HAS_EVENT,
                                DOUBAO_EVENT_TASK_REQUEST, session_id_,
                                pcm_data, len,
                                false, false);  // Raw audio, no compression
    
    return websocket_->Send(message.data(), message.size(), true);
}

std::vector<uint8_t> DoubaoProtocol::BuildMessage(uint8_t message_type, uint8_t message_flags,
                                                   uint32_t event_id, const std::string& session_id,
                                                   const uint8_t* payload, size_t payload_len,
                                                   bool use_json, bool compress) {
    std::vector<uint8_t> message;
    
    // Build header (4 bytes)
    DoubaoHeader header;
    header.protocol_version = PROTOCOL_VERSION;
    header.header_size = HEADER_SIZE;
    header.message_type = message_type;
    header.message_flags = message_flags;
    header.serialization = use_json ? SERIALIZATION_JSON : SERIALIZATION_RAW;
    header.compression = compress ? COMPRESSION_GZIP : COMPRESSION_NONE;
    header.reserved = 0;
    
    message.push_back((header.protocol_version << 4) | header.header_size);
    message.push_back((header.message_type << 4) | header.message_flags);
    message.push_back((header.serialization << 4) | header.compression);
    message.push_back(header.reserved);
    
    // Add event ID (4 bytes, big-endian)
    uint32_t event_id_be = htonl(event_id);
    message.insert(message.end(), (uint8_t*)&event_id_be, (uint8_t*)&event_id_be + 4);
    
    // Add session ID if provided
    if (!session_id.empty()) {
        uint32_t session_id_len = htonl(session_id.size());
        message.insert(message.end(), (uint8_t*)&session_id_len, (uint8_t*)&session_id_len + 4);
        message.insert(message.end(), session_id.begin(), session_id.end());
    }
    
    // Compress payload if needed
    std::vector<uint8_t> processed_payload;
    if (compress && payload_len > 0) {
        uLongf compressed_len = compressBound(payload_len);
        processed_payload.resize(compressed_len);
        if (compress2(processed_payload.data(), &compressed_len, payload, payload_len, Z_DEFAULT_COMPRESSION) == Z_OK) {
            processed_payload.resize(compressed_len);
        } else {
            ESP_LOGE(TAG, "Failed to compress payload");
            processed_payload.assign(payload, payload + payload_len);
        }
    } else {
        processed_payload.assign(payload, payload + payload_len);
    }
    
    // Add payload size (4 bytes, big-endian)
    uint32_t payload_size_be = htonl(processed_payload.size());
    message.insert(message.end(), (uint8_t*)&payload_size_be, (uint8_t*)&payload_size_be + 4);
    
    // Add payload
    message.insert(message.end(), processed_payload.begin(), processed_payload.end());
    
    return message;
}

void DoubaoProtocol::HandleWebSocketMessage(const uint8_t* data, size_t len) {
    if (len < 8) {
        ESP_LOGE(TAG, "Message too short: %d bytes", len);
        return;
    }
    
    // Parse header
    uint8_t protocol_version = (data[0] >> 4) & 0x0F;
    uint8_t header_size = data[0] & 0x0F;
    uint8_t message_type = (data[1] >> 4) & 0x0F;
    uint8_t message_flags = data[1] & 0x0F;
    uint8_t serialization = (data[2] >> 4) & 0x0F;
    uint8_t compression = data[2] & 0x0F;
    
    if (protocol_version != PROTOCOL_VERSION || header_size != HEADER_SIZE) {
        ESP_LOGE(TAG, "Invalid protocol version or header size");
        return;
    }
    
    size_t offset = 4;
    
    // Parse event ID if present
    uint32_t event_id = 0;
    if (message_flags & FLAG_HAS_EVENT) {
        if (offset + 4 > len) return;
        event_id = ntohl(*(uint32_t*)(data + offset));
        offset += 4;
    }
    
    // Parse session ID if present (for session events)
    if (event_id >= 100 && event_id < 600) {
        if (offset + 4 > len) return;
        uint32_t session_id_len = ntohl(*(uint32_t*)(data + offset));
        offset += 4;
        if (offset + session_id_len > len) return;
        offset += session_id_len;  // Skip session ID
    }
    
    // Parse payload size
    if (offset + 4 > len) return;
    uint32_t payload_size = ntohl(*(uint32_t*)(data + offset));
    offset += 4;
    
    if (offset + payload_size > len) {
        ESP_LOGE(TAG, "Invalid payload size: %d", payload_size);
        return;
    }
    
    // Extract payload
    const uint8_t* payload = data + offset;
    
    // Decompress if needed
    std::vector<uint8_t> decompressed;
    if (compression == COMPRESSION_GZIP && payload_size > 0) {
        uLongf decompressed_len = payload_size * 10;  // Estimate
        decompressed.resize(decompressed_len);
        if (uncompress(decompressed.data(), &decompressed_len, payload, payload_size) == Z_OK) {
            payload = decompressed.data();
            payload_size = decompressed_len;
        } else {
            ESP_LOGE(TAG, "Failed to decompress payload");
            return;
        }
    }
    
    // Handle event
    bool is_json = (serialization == SERIALIZATION_JSON);
    HandleDoubaoEvent(event_id, payload, payload_size, is_json);
}

void DoubaoProtocol::HandleDoubaoEvent(uint32_t event_id, const uint8_t* payload, size_t payload_len, bool is_json) {
    ESP_LOGD(TAG, "Handling event %d, payload_len=%d, is_json=%d", event_id, payload_len, is_json);
    
    switch (event_id) {
        case DOUBAO_EVENT_CONNECTION_STARTED:
            ESP_LOGI(TAG, "Connection started");
            break;
            
        case DOUBAO_EVENT_SESSION_STARTED:
            if (is_json && payload_len > 0) {
                cJSON* root = cJSON_ParseWithLength((const char*)payload, payload_len);
                if (root) {
                    HandleSessionStarted(root);
                    cJSON_Delete(root);
                }
            }
            session_started_ = true;
            xEventGroupSetBits(event_group_, DOUBAO_SESSION_READY);
            break;
            
        case DOUBAO_EVENT_TTS_SENTENCE_START:
            ESP_LOGD(TAG, "TTS sentence start");
            break;
            
        case DOUBAO_EVENT_TTS_RESPONSE:
            HandleTTSResponse(payload, payload_len);
            break;
            
        case DOUBAO_EVENT_TTS_SENTENCE_END:
            ESP_LOGD(TAG, "TTS sentence end");
            break;
            
        case DOUBAO_EVENT_TTS_ENDED:
            ESP_LOGD(TAG, "TTS ended");
            xEventGroupSetBits(event_group_, DOUBAO_AUDIO_END);
            break;
            
        case DOUBAO_EVENT_ASR_INFO:
            ESP_LOGD(TAG, "ASR info - user started speaking");
            user_speaking_ = true;
            break;
            
        case DOUBAO_EVENT_ASR_RESPONSE:
            if (is_json && payload_len > 0) {
                cJSON* root = cJSON_ParseWithLength((const char*)payload, payload_len);
                if (root) {
                    HandleASRResponse(root);
                    cJSON_Delete(root);
                }
            }
            break;
            
        case DOUBAO_EVENT_ASR_ENDED:
            ESP_LOGD(TAG, "ASR ended - user stopped speaking");
            user_speaking_ = false;
            HandleASREnded();
            break;
            
        case DOUBAO_EVENT_CHAT_RESPONSE:
            if (is_json && payload_len > 0) {
                cJSON* root = cJSON_ParseWithLength((const char*)payload, payload_len);
                if (root) {
                    HandleChatResponse(root);
                    cJSON_Delete(root);
                }
            }
            break;
            
        case DOUBAO_EVENT_CHAT_ENDED:
            ESP_LOGD(TAG, "Chat ended");
            break;
            
        case DOUBAO_EVENT_SESSION_FAILED:
        case DOUBAO_EVENT_CONNECTION_FAILED:
            ESP_LOGE(TAG, "Error event %d", event_id);
            if (is_json && payload_len > 0) {
                cJSON* root = cJSON_ParseWithLength((const char*)payload, payload_len);
                if (root) {
                    cJSON* error = cJSON_GetObjectItem(root, "error");
                    if (cJSON_IsString(error)) {
                        ESP_LOGE(TAG, "Error: %s", error->valuestring);
                        SetError(error->valuestring);
                    }
                    cJSON_Delete(root);
                }
            }
            xEventGroupSetBits(event_group_, DOUBAO_ERROR);
            break;
            
        default:
            ESP_LOGD(TAG, "Unhandled event: %d", event_id);
            break;
    }
}

void DoubaoProtocol::HandleTTSResponse(const uint8_t* audio_data, size_t len) {
    if (!audio_data || len == 0) return;
    
    ESP_LOGD(TAG, "Received TTS audio: %d bytes", len);
    
    // Create audio packet for playback
    auto packet = std::make_unique<AudioStreamPacket>();
    packet->sample_rate = 24000;  // Doubao returns 24kHz PCM
    packet->frame_duration = 20;   // Assume 20ms frames
    packet->payload.assign(audio_data, audio_data + len);
    
    // Pass to audio callback for playback
    if (on_incoming_audio_) {
        on_incoming_audio_(std::move(packet));
    }
}

void DoubaoProtocol::HandleASRResponse(const cJSON* json) {
    cJSON* results = cJSON_GetObjectItem(json, "results");
    if (!cJSON_IsArray(results)) return;
    
    cJSON* result = cJSON_GetArrayItem(results, 0);
    if (!result) return;
    
    cJSON* text = cJSON_GetObjectItem(result, "text");
    cJSON* is_interim = cJSON_GetObjectItem(result, "is_interim");
    
    if (cJSON_IsString(text)) {
        bool interim = cJSON_IsTrue(is_interim);
        ESP_LOGI(TAG, "ASR: %s %s", text->valuestring, interim ? "(interim)" : "(final)");
        
        // Could forward ASR results if needed
    }
}

void DoubaoProtocol::HandleASREnded() {
    ESP_LOGI(TAG, "User finished speaking");
    // Doubao will automatically generate response
}

void DoubaoProtocol::HandleChatResponse(const cJSON* json) {
    cJSON* content = cJSON_GetObjectItem(json, "content");
    if (cJSON_IsString(content)) {
        ESP_LOGI(TAG, "Chat response: %s", content->valuestring);
        // Could forward chat text if needed
    }
}

void DoubaoProtocol::HandleSessionStarted(const cJSON* json) {
    cJSON* dialog_id = cJSON_GetObjectItem(json, "dialog_id");
    if (cJSON_IsString(dialog_id)) {
        dialog_id_ = dialog_id->valuestring;
        ESP_LOGI(TAG, "Session started with dialog_id: %s", dialog_id_.c_str());
    }
}

void DoubaoProtocol::OnWebSocketData(const uint8_t* data, size_t len, void* ctx) {
    DoubaoProtocol* self = static_cast<DoubaoProtocol*>(ctx);
    self->HandleWebSocketMessage(data, len);
}

void DoubaoProtocol::OnWebSocketConnected(void* ctx) {
    DoubaoProtocol* self = static_cast<DoubaoProtocol*>(ctx);
    ESP_LOGI(TAG, "WebSocket connected");
    xEventGroupSetBits(self->event_group_, DOUBAO_CONNECTED);
}

void DoubaoProtocol::OnWebSocketDisconnected(void* ctx) {
    DoubaoProtocol* self = static_cast<DoubaoProtocol*>(ctx);
    ESP_LOGI(TAG, "WebSocket disconnected");
    self->audio_channel_opened_ = false;
    if (self->on_audio_channel_closed_) {
        self->on_audio_channel_closed_();
    }
}

void DoubaoProtocol::OnWebSocketError(const char* message, void* ctx) {
    DoubaoProtocol* self = static_cast<DoubaoProtocol*>(ctx);
    ESP_LOGE(TAG, "WebSocket error: %s", message);
    self->SetError(message);
    xEventGroupSetBits(self->event_group_, DOUBAO_ERROR);
}

void DoubaoProtocol::AudioSendTask() {
    ESP_LOGI(TAG, "Audio send task started");
    
    while (audio_channel_opened_) {
        std::vector<uint8_t> audio_data;
        
        {
            std::lock_guard<std::mutex> lock(audio_queue_mutex_);
            if (!audio_send_queue_.empty()) {
                audio_data = std::move(audio_send_queue_.front());
                audio_send_queue_.pop_front();
            }
        }
        
        if (!audio_data.empty()) {
            SendTaskRequest(audio_data.data(), audio_data.size());
        } else {
            // Send silence to keep connection alive
            std::vector<uint8_t> silence(320, 0);  // 10ms of 16kHz silence
            SendTaskRequest(silence.data(), silence.size());
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));  // Send every 10ms
    }
    
    ESP_LOGI(TAG, "Audio send task stopped");
}

void DoubaoProtocol::AudioSendTaskEntry(void* arg) {
    DoubaoProtocol* self = static_cast<DoubaoProtocol*>(arg);
    self->AudioSendTask();
    vTaskDelete(NULL);
}
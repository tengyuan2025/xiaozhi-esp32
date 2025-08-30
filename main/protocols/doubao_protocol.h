#ifndef DOUBAO_PROTOCOL_H
#define DOUBAO_PROTOCOL_H

#include "protocol.h"
#include "../utils/websocket.h"
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <memory>
#include <deque>
#include <mutex>

// Doubao protocol event IDs
enum DoubaoEventId {
    // Client events
    DOUBAO_EVENT_START_CONNECTION = 1,
    DOUBAO_EVENT_FINISH_CONNECTION = 2,
    DOUBAO_EVENT_START_SESSION = 100,
    DOUBAO_EVENT_FINISH_SESSION = 102,
    DOUBAO_EVENT_TASK_REQUEST = 200,
    DOUBAO_EVENT_SAY_HELLO = 300,
    DOUBAO_EVENT_CHAT_TTS_TEXT = 500,
    DOUBAO_EVENT_CHAT_TEXT_QUERY = 501,
    
    // Server events  
    DOUBAO_EVENT_CONNECTION_STARTED = 50,
    DOUBAO_EVENT_CONNECTION_FAILED = 51,
    DOUBAO_EVENT_CONNECTION_FINISHED = 52,
    DOUBAO_EVENT_SESSION_STARTED = 150,
    DOUBAO_EVENT_SESSION_FINISHED = 152,
    DOUBAO_EVENT_SESSION_FAILED = 153,
    DOUBAO_EVENT_USAGE_RESPONSE = 154,
    DOUBAO_EVENT_TTS_SENTENCE_START = 350,
    DOUBAO_EVENT_TTS_SENTENCE_END = 351,
    DOUBAO_EVENT_TTS_RESPONSE = 352,
    DOUBAO_EVENT_TTS_ENDED = 359,
    DOUBAO_EVENT_ASR_INFO = 450,
    DOUBAO_EVENT_ASR_RESPONSE = 451,
    DOUBAO_EVENT_ASR_ENDED = 459,
    DOUBAO_EVENT_CHAT_RESPONSE = 550,
    DOUBAO_EVENT_CHAT_ENDED = 559
};

// Doubao binary protocol header structure
struct DoubaoHeader {
    uint8_t protocol_version : 4;  // 0b0001 for v1
    uint8_t header_size : 4;       // 0b0001 for 4 bytes
    uint8_t message_type : 4;      // Message type
    uint8_t message_flags : 4;     // Message type specific flags
    uint8_t serialization : 4;     // 0b0000: Raw, 0b0001: JSON
    uint8_t compression : 4;       // 0b0000: None, 0b0001: gzip
    uint8_t reserved;               // Reserved byte
} __attribute__((packed));

class DoubaoProtocol : public Protocol {
public:
    DoubaoProtocol();
    ~DoubaoProtocol();
    
    // Protocol interface implementation
    bool Start() override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    void SendStartListening(ListeningMode mode) override;
    void SendStopListening() override;
    
protected:
    bool SendText(const std::string& text) override;
    
private:
    // Doubao specific methods
    bool ConnectToDoubao();
    void DisconnectFromDoubao();
    bool SendStartConnection();
    bool SendStartSession();
    bool SendFinishSession();
    bool SendTaskRequest(const uint8_t* pcm_data, size_t len);
    
    // Message building helpers
    std::vector<uint8_t> BuildMessage(uint8_t message_type, uint8_t message_flags,
                                      uint32_t event_id, const std::string& session_id,
                                      const uint8_t* payload, size_t payload_len,
                                      bool use_json = true, bool compress = true);
    
    // Message parsing
    void HandleWebSocketMessage(const uint8_t* data, size_t len);
    void HandleDoubaoEvent(uint32_t event_id, const uint8_t* payload, size_t payload_len, bool is_json);
    void HandleTTSResponse(const uint8_t* audio_data, size_t len);
    void HandleASRResponse(const cJSON* json);
    void HandleASREnded();
    void HandleChatResponse(const cJSON* json);
    void HandleSessionStarted(const cJSON* json);
    
    // WebSocket callbacks
    static void OnWebSocketData(const uint8_t* data, size_t len, void* ctx);
    static void OnWebSocketConnected(void* ctx);
    static void OnWebSocketDisconnected(void* ctx);
    static void OnWebSocketError(const char* message, void* ctx);
    
    // Task functions
    void AudioSendTask();
    static void AudioSendTaskEntry(void* arg);
    
    // Member variables
    std::unique_ptr<WebSocket> websocket_;
    EventGroupHandle_t event_group_;
    std::string session_id_;
    std::string dialog_id_;
    bool audio_channel_opened_;
    bool session_started_;
    bool user_speaking_;
    
    // Audio send queue
    std::deque<std::vector<uint8_t>> audio_send_queue_;
    std::mutex audio_queue_mutex_;
    TaskHandle_t audio_send_task_;
    
    // Configuration
    static constexpr const char* APP_ID = "7059594059";
    static constexpr const char* ACCESS_TOKEN = "tRDp6c2pMhqtMXWYCINDSCDQPyfaWZbt";
    static constexpr const char* BASE_URL = "wss://openspeech.bytedance.com/api/v3/realtime/dialogue";
    static constexpr const char* RESOURCE_ID = "volc.speech.dialog";
    static constexpr const char* APP_KEY = "PlgvMymc7f3tQnJ6";
    
    // Protocol constants
    static constexpr uint8_t PROTOCOL_VERSION = 0x01;
    static constexpr uint8_t HEADER_SIZE = 0x01;
    static constexpr uint8_t MSG_TYPE_CLIENT_REQUEST = 0x01;
    static constexpr uint8_t MSG_TYPE_SERVER_RESPONSE = 0x09;
    static constexpr uint8_t MSG_TYPE_AUDIO_REQUEST = 0x02;
    static constexpr uint8_t MSG_TYPE_AUDIO_RESPONSE = 0x0B;
    static constexpr uint8_t MSG_TYPE_ERROR = 0x0F;
    static constexpr uint8_t FLAG_HAS_EVENT = 0x04;
    static constexpr uint8_t SERIALIZATION_RAW = 0x00;
    static constexpr uint8_t SERIALIZATION_JSON = 0x01;
    static constexpr uint8_t COMPRESSION_NONE = 0x00;
    static constexpr uint8_t COMPRESSION_GZIP = 0x01;
    
    // Event bits
    static constexpr EventBits_t DOUBAO_CONNECTED = BIT0;
    static constexpr EventBits_t DOUBAO_SESSION_READY = BIT1;
    static constexpr EventBits_t DOUBAO_ERROR = BIT2;
    static constexpr EventBits_t DOUBAO_AUDIO_END = BIT3;
};

#endif // DOUBAO_PROTOCOL_H
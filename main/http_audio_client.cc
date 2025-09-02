#include "http_audio_client.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <cstring>

static const char* TAG = "HttpAudioClient";

HttpAudioClient::HttpAudioClient(const std::string& server_url) 
    : server_url_(server_url) {
    ESP_LOGI(TAG, "HTTP Audio Client initialized with URL: %s", server_url_.c_str());
}

HttpAudioClient::~HttpAudioClient() {
}

bool HttpAudioClient::SendAudioData(const std::vector<int16_t>& pcm_data) {
    if (pcm_data.empty()) {
        ESP_LOGW(TAG, "Empty audio data, not sending");
        return false;
    }

    size_t data_size = pcm_data.size() * sizeof(int16_t);
    const size_t min_size = 8000; // Minimum 8000 bytes required by server
    
    if (data_size < min_size) {
        ESP_LOGE(TAG, "Audio data too small: %zu bytes (server requires ≥%zu bytes)", 
                 data_size, min_size);
        return false;
    }

    ESP_LOGI(TAG, "正在发送音频数据到服务器: %zu 个采样点 (%zu 字节)", 
             pcm_data.size(), data_size);

    // Convert int16_t vector to byte array
    const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(pcm_data.data());

    // Configure HTTP client with longer timeout for streaming response
    esp_http_client_config_t config = {
        .url = server_url_.c_str(),
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000,  // 60 second timeout for streaming response
        .buffer_size = 8192,  // Larger buffer for streaming
        .buffer_size_tx = 8192,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    // Set headers
    esp_http_client_set_header(client, "Content-Type", "audio/pcm");
    esp_http_client_set_header(client, "Content-Length", std::to_string(data_size).c_str());
    
    // Add sample rate and channel info as headers
    esp_http_client_set_header(client, "X-Sample-Rate", "16000");
    esp_http_client_set_header(client, "X-Channels", "1");
    esp_http_client_set_header(client, "X-Bit-Depth", "16");

    // Open connection
    esp_err_t err = esp_http_client_open(client, data_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    // Send data in chunks
    size_t written_total = 0;
    const size_t write_chunk_size = 4096;
    
    while (written_total < data_size) {
        size_t to_write = std::min(write_chunk_size, data_size - written_total);
        int written = esp_http_client_write(client, 
                                           (const char*)(data_ptr + written_total), 
                                           to_write);
        
        if (written < 0) {
            ESP_LOGE(TAG, "Failed to write data to HTTP client");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        
        written_total += written;
        ESP_LOGD(TAG, "Sent %d bytes, total: %zu/%zu", written, written_total, data_size);
    }

    // Get response headers
    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    
    ESP_LOGI(TAG, "HTTP response status: %d, content length: %d", status_code, content_length);

    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP request failed with status: %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    // Stream response handling - don't close connection immediately
    ESP_LOGI(TAG, "Starting streaming response read...");
    
    std::vector<uint8_t> audio_buffer;
    const size_t chunk_size = 4096;
    std::vector<char> read_buffer(chunk_size);
    int total_read = 0;
    
    // Keep reading until no more data is available
    while (true) {
        int read_len = esp_http_client_read(client, read_buffer.data(), chunk_size);
        
        if (read_len <= 0) {
            // No more data available
            ESP_LOGI(TAG, "Stream reading completed, read_len: %d", read_len);
            break;
        }
        
        total_read += read_len;
        ESP_LOGI(TAG, "Read chunk: %d bytes, total: %d bytes", read_len, total_read);
        
        // Append to audio buffer
        audio_buffer.insert(audio_buffer.end(), read_buffer.begin(), read_buffer.begin() + read_len);
        
        // Process audio data chunk immediately if it's audio data
        // You can add audio processing logic here if needed
        
        // Add a small delay to allow for streaming
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    ESP_LOGI(TAG, "Streaming response completed. Total received: %d bytes", total_read);
    
    // Process the complete response
    if (total_read > 0 && response_callback_) {
        // Check if response is text (JSON) or binary (audio)
        bool is_text = true;
        for (size_t i = 0; i < std::min(audio_buffer.size(), size_t(100)); i++) {
            if (audio_buffer[i] == 0 || audio_buffer[i] > 127) {
                is_text = false;
                break;
            }
        }
        
        if (is_text) {
            // Text response (JSON)
            std::string response(audio_buffer.begin(), audio_buffer.end());
            ESP_LOGI(TAG, "Received text response: %s", response.c_str());
            response_callback_(response);
        } else {
            // Binary audio response
            ESP_LOGI(TAG, "Received binary audio response: %zu bytes", audio_buffer.size());
            
            if (audio_response_callback_) {
                audio_response_callback_(audio_buffer);
            }
            
            // Also notify text callback for status
            if (response_callback_) {
                std::string info = "Received audio data: " + std::to_string(audio_buffer.size()) + " bytes";
                response_callback_(info);
            }
        }
    }

    // Now close the connection
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return (status_code == 200 || status_code == 201);
}

void HttpAudioClient::SetResponseCallback(std::function<void(const std::string&)> callback) {
    response_callback_ = callback;
}

void HttpAudioClient::SetAudioResponseCallback(std::function<void(const std::vector<uint8_t>&)> callback) {
    audio_response_callback_ = callback;
}
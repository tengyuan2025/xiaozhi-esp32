#include "http_protocol.h"
#include "board.h"
#include "system_info.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <cstring>
#include <sstream>
#include <iomanip>

#define TAG "HTTP"

HttpProtocol::HttpProtocol() : channel_opened_(false) {
}

HttpProtocol::~HttpProtocol() {
    CloseAudioChannel();
}

bool HttpProtocol::Start() {
    // HTTP协议不需要持久连接，只在发送时建立连接
    return true;
}

bool HttpProtocol::OpenAudioChannel() {
    Settings settings("http", false);
    server_url_ = settings.GetString("url", "http://192.168.1.105:8000/api/v1/process-voice-json");
    
    ESP_LOGI(TAG, "Setting up HTTP audio channel to: %s", server_url_.c_str());
    
    auto network = Board::GetInstance().GetNetwork();
    http_client_ = network->CreateHttp(1);
    if (http_client_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return false;
    }
    
    // 清空音频缓存
    audio_buffer_.clear();
    pcm_buffer_.clear();
    channel_opened_ = true;
    
    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }
    
    ESP_LOGI(TAG, "HTTP audio channel opened successfully");
    return true;
}

void HttpProtocol::CloseAudioChannel() {
    if (!channel_opened_) {
        return;
    }
    
    // 发送剩余的音频数据
    if (!pcm_buffer_.empty()) {
        SendPcmBuffer();
    } else if (!audio_buffer_.empty()) {
        SendAudioBuffer();
    }
    
    http_client_.reset();
    audio_buffer_.clear();
    pcm_buffer_.clear();
    channel_opened_ = false;
    
    if (on_audio_channel_closed_ != nullptr) {
        on_audio_channel_closed_();
    }
    
    ESP_LOGI(TAG, "HTTP audio channel closed");
}

bool HttpProtocol::IsAudioChannelOpened() const {
    return channel_opened_;
}

bool HttpProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (!channel_opened_ || http_client_ == nullptr) {
        ESP_LOGW(TAG, "HTTP channel not opened");
        return false;
    }
    
    // HTTP协议期望接收PCM数据，但这里收到的是OPUS数据
    // 我们需要将OPUS数据解码为PCM，或者修改音频处理流程
    // 临时方案：直接发送OPUS数据，但在multipart中标记为PCM
    // TODO: 实际部署时需要添加OPUS->PCM解码
    
    ESP_LOGW(TAG, "Receiving OPUS data but HTTP server expects PCM - needs decoder implementation");
    
    // 将音频数据添加到缓存（目前是OPUS数据）
    audio_buffer_.insert(audio_buffer_.end(), packet->payload.begin(), packet->payload.end());
    
    // 当缓存达到一定大小时发送，或者每收到一定数量的包就发送
    if (audio_buffer_.size() >= MAX_AUDIO_BUFFER_SIZE || audio_buffer_.size() >= 4096) {
        return SendAudioBuffer();
    }
    
    return true;
}

bool HttpProtocol::SendPcmAudio(const std::vector<int16_t>& pcm_data) {
    if (!channel_opened_ || http_client_ == nullptr) {
        ESP_LOGW(TAG, "⚠️ HTTP channel not opened - cannot send PCM data");
        return false;
    }
    
    ESP_LOGI(TAG, "📥 Receiving PCM chunk: %zu samples (%.1f ms, %.1f KB)", 
             pcm_data.size(),
             (float)pcm_data.size() / 16.0f,  // 16kHz -> ms
             (float)pcm_data.size() * sizeof(int16_t) / 1024.0f);  // size in KB
    
    // 将PCM数据添加到缓存
    size_t old_size = pcm_buffer_.size();
    pcm_buffer_.insert(pcm_buffer_.end(), pcm_data.begin(), pcm_data.end());
    
    ESP_LOGI(TAG, "📦 Buffer status: %zu -> %zu samples (%.1fs of audio)", 
             old_size, pcm_buffer_.size(), 
             (float)pcm_buffer_.size() / 16000.0f);
    
    // 当缓存达到一定大小时发送(约1秒的音频数据)
    if (pcm_buffer_.size() >= MAX_PCM_SAMPLES) {
        ESP_LOGI(TAG, "🚀 Buffer full (%.1fs), triggering HTTP POST to /api/v1/process-voice-json", 
                 (float)pcm_buffer_.size() / 16000.0f);
        return SendPcmBuffer();
    }
    
    return true;
}

bool HttpProtocol::SendAudioBuffer() {
    if (audio_buffer_.empty() || http_client_ == nullptr) {
        return true;
    }
    
    ESP_LOGI(TAG, "Sending audio buffer, size: %zu bytes", audio_buffer_.size());
    
    try {
        // 创建multipart/form-data
        std::string boundary = CreateMultipartBoundary();
        std::string form_data = CreateMultipartFormData(audio_buffer_, boundary);
        
        // 设置HTTP请求头
        http_client_->SetHeader("Content-Type", ("multipart/form-data; boundary=" + boundary));
        http_client_->SetHeader("User-Agent", "Xiaozhi-ESP32/1.0");
        http_client_->SetHeader("Accept", "*/*");
        
        // 设置请求内容
        http_client_->SetContent(std::move(form_data));
        
        // 发送POST请求
        if (!http_client_->Open("POST", server_url_)) {
            ESP_LOGE(TAG, "Failed to open HTTP connection");
            SetError(Lang::Strings::SERVER_ERROR);
            return false;
        }
        
        // 检查响应状态码
        int status_code = http_client_->GetStatusCode();
        if (status_code == 200) {
            // 读取响应内容
            std::string response_body = http_client_->ReadAll();
            ESP_LOGI(TAG, "Audio sent successfully, response size: %zu bytes", response_body.size());
            
            // 处理返回的音频数据（PCM格式）
            if (!response_body.empty() && on_incoming_audio_ != nullptr) {
                auto audio_packet = std::make_unique<AudioStreamPacket>();
                audio_packet->sample_rate = 24000; // 服务器返回24kHz
                audio_packet->frame_duration = 60;  // 60ms帧
                audio_packet->timestamp = 0;
                audio_packet->payload.assign(response_body.begin(), response_body.end());
                
                on_incoming_audio_(std::move(audio_packet));
            }
            
            // 清空缓存
            audio_buffer_.clear();
            
            http_client_->Close();
            return true;
        } else {
            ESP_LOGE(TAG, "HTTP request failed, status: %d", status_code);
            http_client_->Close();
            SetError(Lang::Strings::SERVER_ERROR);
            return false;
        }
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "HTTP request exception: %s", e.what());
        http_client_->Close();
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }
}

std::string HttpProtocol::CreateMultipartBoundary() {
    // 生成简单的boundary
    std::stringstream ss;
    ss << "----WebKitFormBoundary" << std::hex << esp_random();
    return ss.str();
}

std::string HttpProtocol::CreateMultipartFormData(const std::vector<uint8_t>& audio_data, const std::string& boundary) {
    std::stringstream form_data;
    
    // 开始boundary
    form_data << "--" << boundary << "\r\n";
    
    // Content-Disposition header
    form_data << "Content-Disposition: form-data; name=\"audio\"; filename=\"audio.pcm\"\r\n";
    form_data << "Content-Type: application/octet-stream\r\n\r\n";
    
    // 音频数据
    form_data.write(reinterpret_cast<const char*>(audio_data.data()), audio_data.size());
    
    // 结束boundary
    form_data << "\r\n--" << boundary << "--\r\n";
    
    return form_data.str();
}

bool HttpProtocol::SendPcmBuffer() {
    if (pcm_buffer_.empty() || http_client_ == nullptr) {
        return true;
    }
    
    ESP_LOGI(TAG, "🌐 === HTTP POST REQUEST START ===");
    ESP_LOGI(TAG, "🎯 Target: %s", server_url_.c_str());
    ESP_LOGI(TAG, "📊 Audio data: %zu samples (%.1fs, %zu bytes)", 
             pcm_buffer_.size(), 
             (float)pcm_buffer_.size() / 16000.0f,
             pcm_buffer_.size() * sizeof(int16_t));
    
    try {
        // 创建multipart/form-data for PCM
        std::string boundary = CreateMultipartBoundary();
        ESP_LOGI(TAG, "🔗 Creating multipart form with boundary: %.20s...", boundary.c_str());
        
        std::string form_data = CreateMultipartFormDataForPcm(pcm_buffer_, boundary);
        ESP_LOGI(TAG, "📦 Form data created: %zu bytes total", form_data.size());
        
        // 设置HTTP请求头
        http_client_->SetHeader("Content-Type", ("multipart/form-data; boundary=" + boundary));
        http_client_->SetHeader("User-Agent", "Xiaozhi-ESP32/1.0");
        http_client_->SetHeader("Accept", "*/*");
        ESP_LOGI(TAG, "📋 HTTP headers set (Content-Type: multipart/form-data)");
        
        // 设置请求内容
        http_client_->SetContent(std::move(form_data));
        ESP_LOGI(TAG, "📤 Request content set, initiating POST...");
        
        // 发送POST请求
        if (!http_client_->Open("POST", server_url_)) {
            ESP_LOGE(TAG, "❌ Failed to open HTTP connection to %s", server_url_.c_str());
            SetError(Lang::Strings::SERVER_ERROR);
            return false;
        }
        ESP_LOGI(TAG, "🔗 HTTP connection opened successfully");
        
        // 检查响应状态码
        int status_code = http_client_->GetStatusCode();
        ESP_LOGI(TAG, "📊 HTTP Response Status: %d", status_code);
        
        if (status_code == 200) {
            // 读取响应内容
            std::string response_body = http_client_->ReadAll();
            ESP_LOGI(TAG, "✅ SUCCESS! HTTP POST completed");
            ESP_LOGI(TAG, "📥 Response size: %zu bytes", response_body.size());
            
            // 如果响应是JSON格式（process-voice-json端点），解析并记录
            if (!response_body.empty()) {
                ESP_LOGI(TAG, "📄 Response preview (first 200 chars): %.200s%s", 
                         response_body.c_str(), 
                         response_body.size() > 200 ? "..." : "");
                         
                // 处理返回的数据
                if (on_incoming_audio_ != nullptr) {
                    auto audio_packet = std::make_unique<AudioStreamPacket>();
                    audio_packet->sample_rate = 24000; // 服务器返回24kHz
                    audio_packet->frame_duration = 60;  // 60ms帧
                    audio_packet->timestamp = 0;
                    audio_packet->payload.assign(response_body.begin(), response_body.end());
                    
                    ESP_LOGI(TAG, "🎵 Processing server response as audio data");
                    on_incoming_audio_(std::move(audio_packet));
                } else {
                    ESP_LOGI(TAG, "ℹ️ No audio callback registered, response data not processed");
                }
            }
            
            // 清空缓存
            size_t buffer_cleared = pcm_buffer_.size();
            pcm_buffer_.clear();
            ESP_LOGI(TAG, "🧹 Buffer cleared: %zu samples freed", buffer_cleared);
            
            http_client_->Close();
            ESP_LOGI(TAG, "🌐 === HTTP POST REQUEST COMPLETED SUCCESSFULLY ===");
            return true;
        } else {
            ESP_LOGE(TAG, "❌ HTTP request failed with status: %d", status_code);
            
            // 尝试读取错误响应
            std::string error_body = http_client_->ReadAll();
            if (!error_body.empty()) {
                ESP_LOGE(TAG, "❌ Error response: %.500s", error_body.c_str());
            }
            
            http_client_->Close();
            ESP_LOGE(TAG, "🌐 === HTTP POST REQUEST FAILED ===");
            SetError(Lang::Strings::SERVER_ERROR);
            return false;
        }
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "HTTP request exception: %s", e.what());
        http_client_->Close();
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }
}

std::string HttpProtocol::CreateMultipartFormDataForPcm(const std::vector<int16_t>& pcm_data, const std::string& boundary) {
    std::stringstream form_data;
    
    // 开始boundary
    form_data << "--" << boundary << "\r\n";
    
    // Content-Disposition header
    form_data << "Content-Disposition: form-data; name=\"audio\"; filename=\"audio.pcm\"\r\n";
    form_data << "Content-Type: audio/pcm\r\n\r\n";
    
    // PCM音频数据（16位小端格式）
    const char* pcm_bytes = reinterpret_cast<const char*>(pcm_data.data());
    size_t pcm_size = pcm_data.size() * sizeof(int16_t);
    form_data.write(pcm_bytes, pcm_size);
    
    // 结束boundary
    form_data << "\r\n--" << boundary << "--\r\n";
    
    return form_data.str();
}

bool HttpProtocol::SendText(const std::string& text) {
    // HTTP协议暂不支持发送文本消息
    ESP_LOGW(TAG, "SendText not implemented for HTTP protocol");
    return false;
}
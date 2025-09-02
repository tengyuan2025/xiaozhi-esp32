#ifndef HTTP_AUDIO_CLIENT_H
#define HTTP_AUDIO_CLIENT_H

#include <vector>
#include <string>
#include <cstdint>
#include <functional>

class HttpAudioClient {
public:
    HttpAudioClient(const std::string& server_url);
    ~HttpAudioClient();

    // Send PCM audio data to server
    bool SendAudioData(const std::vector<int16_t>& pcm_data);
    
    // Set callback for text server response
    void SetResponseCallback(std::function<void(const std::string&)> callback);
    
    // Set callback for binary audio response
    void SetAudioResponseCallback(std::function<void(const std::vector<uint8_t>&)> callback);

private:
    std::string server_url_;
    std::function<void(const std::string&)> response_callback_;
    std::function<void(const std::vector<uint8_t>&)> audio_response_callback_;
};

#endif // HTTP_AUDIO_CLIENT_H
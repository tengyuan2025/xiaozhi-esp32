#ifndef _HTTP_PROTOCOL_H_
#define _HTTP_PROTOCOL_H_

#include "protocol.h"
#include <http.h>
#include <string>
#include <vector>
#include <memory>

class HttpProtocol : public Protocol {
public:
    HttpProtocol();
    ~HttpProtocol();

    bool Start() override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    bool SendPcmAudio(const std::vector<int16_t>& pcm_data) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;

private:
    std::unique_ptr<Http> http_client_;
    std::string server_url_;
    std::vector<uint8_t> audio_buffer_;  // 缓存音频数据
    std::vector<int16_t> pcm_buffer_;    // PCM音频数据缓存
    bool channel_opened_;
    static constexpr size_t MAX_AUDIO_BUFFER_SIZE = 32768; // 32KB缓存
    static constexpr size_t MAX_PCM_SAMPLES = 16000;       // 1秒的PCM样本数(16kHz)

    bool SendText(const std::string& text) override;
    bool SendAudioBuffer();  // 发送缓存的音频数据(OPUS)
    bool SendPcmBuffer();    // 发送缓存的PCM音频数据
    std::string CreateMultipartBoundary();
    std::string CreateMultipartFormData(const std::vector<uint8_t>& audio_data, const std::string& boundary);
    std::string CreateMultipartFormDataForPcm(const std::vector<int16_t>& pcm_data, const std::string& boundary);
};

#endif
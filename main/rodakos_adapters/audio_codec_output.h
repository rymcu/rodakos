#pragma once

#include <cstdint>

namespace rodakos {

class AudioCodecOutput {
public:
    bool Init();
    void Deinit();

    bool Open(uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample, int volume);
    void Close();
    bool Write(const void* data, int bytes);
    bool SetVolume(int volume);

    bool IsReady() const { return initialized_; }
    bool IsOpen() const { return codec_open_; }

private:
    bool MatchesOpenFormat(uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample) const;
    void ClearOpenFormat();

    bool initialized_ = false;
    bool codec_open_ = false;
    uint32_t sample_rate_ = 0;
    uint16_t channels_ = 0;
    uint16_t bits_per_sample_ = 0;
    void* dac_handle_ = nullptr;
};

}  // namespace rodakos

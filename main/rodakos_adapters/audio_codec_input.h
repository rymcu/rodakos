#pragma once

#include <cstdint>

namespace rodakos {

class AudioCodecInput {
public:
    bool Init();
    void Deinit();

    bool Open(uint32_t sample_rate,
              uint16_t channels,
              uint16_t bits_per_sample,
              int gain,
              uint16_t channel_mask = 0);
    void Close();
    bool Read(void* data, int bytes);
    bool SetGain(int gain);

    bool IsReady() const { return initialized_; }
    bool IsOpen() const { return codec_open_; }

private:
    bool MatchesOpenFormat(uint32_t sample_rate,
                           uint16_t channels,
                           uint16_t bits_per_sample,
                           uint16_t channel_mask) const;
    void ClearOpenFormat();

    bool initialized_ = false;
    bool codec_open_ = false;
    uint32_t sample_rate_ = 0;
    uint16_t channels_ = 0;
    uint16_t bits_per_sample_ = 0;
    uint16_t channel_mask_ = 0;
    void* adc_handle_ = nullptr;
};

}  // namespace rodakos

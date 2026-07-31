#pragma once

#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace rodakos {

class AudioCodecInput {
public:
    AudioCodecInput();
    ~AudioCodecInput();

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

    bool OpenForOwner(const char* owner,
                      int priority,
                      uint32_t sample_rate,
                      uint16_t channels,
                      uint16_t bits_per_sample,
                      int gain,
                      uint16_t channel_mask = 0);
    void CloseForOwner(const char* owner);
    bool ReadForOwner(const char* owner, void* data, int bytes);

    bool IsReady() const { return initialized_; }
    bool IsOpen() const;

private:
    bool InitLocked();
    void CloseLocked();
    bool SetGainLocked(int gain);
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
    int gain_ = 0;
    bool gain_configured_ = false;
    void* adc_handle_ = nullptr;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    std::string owner_;
    int owner_priority_ = 0;
};

}  // namespace rodakos

#pragma once

#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "rodakos_adapters/audio_codec_output.h"

namespace rodakos {

class AudioOutputService {
public:
    AudioOutputService();
    ~AudioOutputService();

    bool Init();
    void Deinit();

    bool ReserveOwner(const char* owner);
    bool OpenForOwner(const char* owner,
                      uint32_t sample_rate,
                      uint16_t channels,
                      uint16_t bits_per_sample);
    void CloseForOwner(const char* owner);
    bool WriteForOwner(const char* owner, const void* data, int bytes);

    bool SetVolume(int volume);
    int volume() const;

    bool IsReady() const { return initialized_; }
    bool IsOpen();
    bool IsOpenForOwner(const char* owner);

private:
    void CloseLocked();

    SemaphoreHandle_t mutex_ = nullptr;
    bool initialized_ = false;
    int volume_ = 60;
    std::string owner_;
    AudioCodecOutput output_;
};

}  // namespace rodakos

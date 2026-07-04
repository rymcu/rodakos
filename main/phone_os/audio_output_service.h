#pragma once

#include <cstdint>

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

    bool Open(uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample);
    void Close();
    bool Write(const void* data, int bytes);

    bool SetVolume(int volume);
    int volume() const;

    bool IsReady() const { return initialized_; }
    bool IsOpen();

private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool initialized_ = false;
    int volume_ = 60;
    AudioCodecOutput output_;
};

}  // namespace rodakos

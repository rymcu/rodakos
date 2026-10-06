#pragma once

#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "rodakos_adapters/audio_codec_output.h"

namespace rodakos {

enum class AudioVolumeOperation { kSet, kUp, kDown };

enum class AudioVolumeApplication { kDeferred, kCodecApplied, kUnverified };

struct AudioVolumeResult {
    bool accepted = false;
    int previous_volume = 60;
    int volume = 60;
    uint32_t configuration_revision = 0;
    AudioVolumeApplication application = AudioVolumeApplication::kUnverified;
};

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
    AudioVolumeResult ApplyVolume(AudioVolumeOperation operation, int value);
    int volume() const;

    bool IsReady() const { return initialized_; }
    bool IsOpen();
    bool IsOpenForOwner(const char* owner);

private:
    void CloseLocked();

    SemaphoreHandle_t mutex_ = nullptr;
    bool initialized_ = false;
    int volume_ = 60;
    uint32_t configuration_revision_ = 0;
    std::string owner_;
    AudioCodecOutput output_;
};

}  // namespace rodakos

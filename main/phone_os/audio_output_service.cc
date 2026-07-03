#include "phone_os/audio_output_service.h"

#include <algorithm>

#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "AudioOutputService";
}

AudioOutputService::AudioOutputService() {
    mutex_ = xSemaphoreCreateMutex();
}

AudioOutputService::~AudioOutputService() {
    Deinit();
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool AudioOutputService::Init() {
    if (initialized_) {
        return true;
    }

    if (!output_.Init()) {
        return false;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Audio output initialized");
    return true;
}

void AudioOutputService::Deinit() {
    Close();
    if (initialized_) {
        output_.Deinit();
        initialized_ = false;
        ESP_LOGI(TAG, "Audio output deinitialized");
    }
}

bool AudioOutputService::Open(uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample) {
    if (!Init()) {
        return false;
    }

    int volume = 60;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        volume = volume_;
        xSemaphoreGive(mutex_);
    }

    bool ok = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    ok = output_.Open(sample_rate, channels, bits_per_sample, volume);
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
    return ok;
}

void AudioOutputService::Close() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    output_.Close();

    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

bool AudioOutputService::Write(const void* data, int bytes) {
    if (data == nullptr || bytes <= 0) {
        return false;
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    const bool ok = output_.Write(data, bytes);
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
    return ok;
}

bool AudioOutputService::SetVolume(int volume) {
    const int clamped = std::clamp(volume, 0, 100);
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    volume_ = clamped;

    if (output_.IsOpen()) {
        if (!output_.SetVolume(clamped)) {
            if (mutex_ != nullptr) {
                xSemaphoreGive(mutex_);
            }
            ESP_LOGW(TAG, "Failed to set volume to %d", clamped);
            return false;
        }
    }
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
    return true;
}

int AudioOutputService::volume() const {
    if (mutex_ == nullptr) {
        return volume_;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const int volume = volume_;
    xSemaphoreGive(mutex_);
    return volume;
}

bool AudioOutputService::IsOpen() {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool open = output_.IsOpen();
    xSemaphoreGive(mutex_);
    return open;
}

}  // namespace rodakos

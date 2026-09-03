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
    bool initialized_now = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    if (!initialized_) {
        initialized_ = output_.Init();
        initialized_now = initialized_;
    }
    const bool initialized = initialized_;
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
    if (initialized_now) {
        ESP_LOGI(TAG, "Audio output initialized");
    }
    return initialized;
}

void AudioOutputService::Deinit() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    CloseLocked();
    if (initialized_) {
        output_.Deinit();
        initialized_ = false;
        ESP_LOGI(TAG, "Audio output deinitialized");
    }
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

bool AudioOutputService::ReserveOwner(const char* owner) {
    if (owner == nullptr || owner[0] == '\0' || mutex_ == nullptr) {
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool available = owner_.empty() || owner_ == owner;
    const std::string active_owner = owner_;
    if (available) {
        owner_ = owner;
    }
    xSemaphoreGive(mutex_);
    if (!available) {
        ESP_LOGW(TAG, "Audio output busy: owner=%s requested_by=%s",
                 active_owner.c_str(), owner);
    }
    return available;
}

bool AudioOutputService::OpenForOwner(const char* owner,
                                      uint32_t sample_rate,
                                      uint16_t channels,
                                      uint16_t bits_per_sample) {
    if (owner == nullptr || owner[0] == '\0') {
        return false;
    }
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
    if (owner_.empty() || owner_ == owner) {
        ok = output_.Open(sample_rate, channels, bits_per_sample, volume);
        if (ok) {
            owner_ = owner;
        } else if (!output_.IsOpen()) {
            owner_.clear();
        }
    }
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
    return ok;
}

void AudioOutputService::CloseForOwner(const char* owner) {
    if (owner == nullptr || owner[0] == '\0') {
        return;
    }
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    if (owner_ == owner) {
        CloseLocked();
    }
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

bool AudioOutputService::WriteForOwner(const char* owner, const void* data, int bytes) {
    if (owner == nullptr || owner[0] == '\0' || data == nullptr || bytes <= 0) {
        return false;
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    const bool ok = owner_ == owner && output_.Write(data, bytes);
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

bool AudioOutputService::IsOpenForOwner(const char* owner) {
    if (owner == nullptr || owner[0] == '\0' || mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool open = owner_ == owner && output_.IsOpen();
    xSemaphoreGive(mutex_);
    return open;
}

void AudioOutputService::CloseLocked() {
    output_.Close();
    owner_.clear();
}

}  // namespace rodakos

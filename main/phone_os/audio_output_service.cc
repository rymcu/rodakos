#include "phone_os/audio_output_service.h"

#include <algorithm>
#include <limits>

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

    bool ok = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    if (owner_.empty() || owner_ == owner) {
        ok = output_.Open(sample_rate, channels, bits_per_sample,
                          volume_.load(std::memory_order_relaxed));
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
    return ApplyVolume(AudioVolumeOperation::kSet, std::clamp(volume, 0, 100)).accepted;
}

AudioVolumeResult AudioOutputService::ApplyVolume(AudioVolumeOperation operation, int value) {
    AudioVolumeResult result;
    if (mutex_ == nullptr) {
        return result;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const int current_volume = volume_.load(std::memory_order_relaxed);
    result.previous_volume = current_volume;
    result.volume = current_volume;
    result.configuration_revision = configuration_revision_;

    int requested = current_volume;
    bool valid = false;
    switch (operation) {
        case AudioVolumeOperation::kSet:
            valid = value >= 0 && value <= 100;
            requested = value;
            break;
        case AudioVolumeOperation::kUp:
            valid = value >= 1 && value <= 100;
            if (valid) requested = std::min(100, current_volume + value);
            break;
        case AudioVolumeOperation::kDown:
            valid = value >= 1 && value <= 100;
            if (valid) requested = std::max(0, current_volume - value);
            break;
    }

    if (valid && configuration_revision_ != std::numeric_limits<uint32_t>::max()) {
        const bool open = output_.IsOpen();
        if (!open || output_.SetVolume(requested)) {
            // 同一锁区生成回执；不能在另一次查询时推断本次写入是否经过 codec。
            volume_.store(requested, std::memory_order_release);
            ++configuration_revision_;
            result.accepted = true;
            result.volume = requested;
            result.configuration_revision = configuration_revision_;
            result.application = open ? AudioVolumeApplication::kCodecApplied
                                      : AudioVolumeApplication::kDeferred;
        }
    }
    xSemaphoreGive(mutex_);
    return result;
}

int AudioOutputService::volume() const {
    return volume_.load(std::memory_order_acquire);
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

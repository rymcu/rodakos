#include "phone_os/audio_output_service.h"

#include <algorithm>

#include <dev_audio_codec.h>
#include <esp_board_manager.h>
#include <esp_codec_dev.h>
#include <esp_err.h>
#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "AudioOutputService";
constexpr const char* kAudioDacDeviceName = "audio_dac";
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

    esp_err_t ret = esp_board_manager_init_device_by_name(kAudioDacDeviceName);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio DAC (%s)", esp_err_to_name(ret));
        return false;
    }

    ret = esp_board_manager_get_device_handle(kAudioDacDeviceName, &dac_handle_);
    if (ret != ESP_OK || dac_handle_ == nullptr) {
        ESP_LOGE(TAG, "Failed to get audio DAC handle (%s)", esp_err_to_name(ret));
        esp_board_manager_deinit_device_by_name(kAudioDacDeviceName);
        dac_handle_ = nullptr;
        return false;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Audio output initialized");
    return true;
}

void AudioOutputService::Deinit() {
    Close();
    if (initialized_) {
        esp_board_manager_deinit_device_by_name(kAudioDacDeviceName);
        initialized_ = false;
        dac_handle_ = nullptr;
        ESP_LOGI(TAG, "Audio output deinitialized");
    }
}

bool AudioOutputService::Open(uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample) {
    if (!Init()) {
        return false;
    }
    Close();

    auto* handle = static_cast<dev_audio_codec_handles_t*>(dac_handle_);
    if (handle == nullptr || handle->codec_dev == nullptr) {
        ESP_LOGE(TAG, "Audio DAC unavailable");
        return false;
    }

    esp_codec_dev_sample_info_t sample_info = {};
    sample_info.bits_per_sample = static_cast<uint8_t>(bits_per_sample);
    sample_info.channel = static_cast<uint8_t>(channels);
    sample_info.channel_mask = 0;
    sample_info.sample_rate = sample_rate;
    sample_info.mclk_multiple = (sample_rate % 11025U) == 0 ? 384 : 256;
    const int ret = esp_codec_dev_open(handle->codec_dev, &sample_info);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open codec: %d", ret);
        return false;
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        codec_open_ = true;
        xSemaphoreGive(mutex_);
    }

    SetVolume(volume_);
    return true;
}

void AudioOutputService::Close() {
    if (!IsOpen() || dac_handle_ == nullptr) {
        return;
    }

    auto* handle = static_cast<dev_audio_codec_handles_t*>(dac_handle_);
    if (handle != nullptr && handle->codec_dev != nullptr) {
        esp_codec_dev_close(handle->codec_dev);
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        codec_open_ = false;
        xSemaphoreGive(mutex_);
    }
}

bool AudioOutputService::Write(const void* data, int bytes) {
    if (data == nullptr || bytes <= 0 || dac_handle_ == nullptr) {
        return false;
    }

    auto* handle = static_cast<dev_audio_codec_handles_t*>(dac_handle_);
    if (handle == nullptr || handle->codec_dev == nullptr) {
        return false;
    }

    const int ret = esp_codec_dev_write(handle->codec_dev, const_cast<void*>(data), bytes);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Codec write failed: %d", ret);
        return false;
    }
    return true;
}

bool AudioOutputService::SetVolume(int volume) {
    const int clamped = std::clamp(volume, 0, 100);
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        volume_ = clamped;
        xSemaphoreGive(mutex_);
    } else {
        volume_ = clamped;
    }

    if (initialized_ && dac_handle_ != nullptr) {
        auto* handle = static_cast<dev_audio_codec_handles_t*>(dac_handle_);
        if (handle != nullptr && handle->codec_dev != nullptr) {
            const int ret = esp_codec_dev_set_out_vol(handle->codec_dev, clamped);
            if (ret != ESP_CODEC_DEV_OK) {
                ESP_LOGW(TAG, "Failed to set volume to %d", clamped);
                return false;
            }
        }
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
    const bool open = codec_open_;
    xSemaphoreGive(mutex_);
    return open;
}

}  // namespace rodakos

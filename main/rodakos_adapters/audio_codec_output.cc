#include "rodakos_adapters/audio_codec_output.h"

#include <dev_audio_codec.h>
#include <esp_board_manager.h>
#include <esp_codec_dev.h>
#include <esp_err.h>
#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "AudioCodecOutput";
constexpr const char* kAudioDacDeviceName = "audio_dac";

esp_codec_dev_handle_t CodecFromHandle(void* dac_handle) {
    auto* handle = static_cast<dev_audio_codec_handles_t*>(dac_handle);
    return handle != nullptr ? handle->codec_dev : nullptr;
}
}  // namespace

bool AudioCodecOutput::Init() {
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
    ESP_LOGI(TAG, "Audio codec output initialized");
    return true;
}

void AudioCodecOutput::Deinit() {
    Close();
    if (initialized_) {
        const esp_err_t ret = esp_board_manager_deinit_device_by_name(kAudioDacDeviceName);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Audio DAC release failed: %s", esp_err_to_name(ret));
        }
        initialized_ = false;
        dac_handle_ = nullptr;
        ESP_LOGI(TAG, "Audio codec output deinitialized");
    }
}

bool AudioCodecOutput::Open(uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample, int volume) {
    if (!Init()) {
        return false;
    }
    Close();

    esp_codec_dev_handle_t codec = CodecFromHandle(dac_handle_);
    if (codec == nullptr) {
        ESP_LOGE(TAG, "Audio DAC unavailable");
        return false;
    }

    esp_codec_dev_sample_info_t sample_info = {};
    sample_info.bits_per_sample = static_cast<uint8_t>(bits_per_sample);
    sample_info.channel = static_cast<uint8_t>(channels);
    sample_info.channel_mask = 0;
    sample_info.sample_rate = sample_rate;
    sample_info.mclk_multiple = (sample_rate % 11025U) == 0 ? 384 : 256;

    const int ret = esp_codec_dev_open(codec, &sample_info);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open codec: %d", ret);
        return false;
    }

    codec_open_ = true;
    const int vol_ret = esp_codec_dev_set_out_vol(codec, volume);
    if (vol_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set volume to %d", volume);
    }
    return true;
}

void AudioCodecOutput::Close() {
    esp_codec_dev_handle_t codec = CodecFromHandle(dac_handle_);
    if (codec_open_ && codec != nullptr) {
        esp_codec_dev_close(codec);
    }
    codec_open_ = false;
}

bool AudioCodecOutput::Write(const void* data, int bytes) {
    if (data == nullptr || bytes <= 0 || !codec_open_) {
        return false;
    }

    esp_codec_dev_handle_t codec = CodecFromHandle(dac_handle_);
    if (codec == nullptr) {
        return false;
    }

    const int ret = esp_codec_dev_write(codec, const_cast<void*>(data), bytes);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Codec write failed: %d", ret);
        return false;
    }
    return true;
}

bool AudioCodecOutput::SetVolume(int volume) {
    esp_codec_dev_handle_t codec = CodecFromHandle(dac_handle_);
    if (!codec_open_ || codec == nullptr) {
        return true;
    }

    const int ret = esp_codec_dev_set_out_vol(codec, volume);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set volume to %d", volume);
        return false;
    }
    return true;
}

}  // namespace rodakos

#include "rodakos_adapters/audio_codec_input.h"

#include <dev_audio_codec.h>
#include <esp_board_manager.h>
#include <esp_codec_dev.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_log_level.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "AudioCodecInput";
constexpr const char* kAudioAdcDeviceName = "audio_adc";
constexpr const char* kI2sCommonTag = "i2s_common";

esp_codec_dev_handle_t CodecFromHandle(void* adc_handle) {
    auto* handle = static_cast<dev_audio_codec_handles_t*>(adc_handle);
    return handle != nullptr ? handle->codec_dev : nullptr;
}

int OpenCodecSuppressingBenignI2sDisable(esp_codec_dev_handle_t codec,
                                         esp_codec_dev_sample_info_t* sample_info) {
    const esp_log_level_t previous_level = esp_log_level_get(kI2sCommonTag);
    esp_log_level_set(kI2sCommonTag, ESP_LOG_NONE);
    const int ret = esp_codec_dev_open(codec, sample_info);
    esp_log_level_set(kI2sCommonTag, previous_level);
    return ret;
}
}  // namespace

bool AudioCodecInput::Init() {
    if (initialized_) {
        return true;
    }

    esp_err_t ret = esp_board_manager_init_device_by_name(kAudioAdcDeviceName);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio ADC (%s)", esp_err_to_name(ret));
        return false;
    }

    ret = esp_board_manager_get_device_handle(kAudioAdcDeviceName, &adc_handle_);
    if (ret != ESP_OK || adc_handle_ == nullptr) {
        ESP_LOGE(TAG, "Failed to get audio ADC handle (%s)", esp_err_to_name(ret));
        esp_board_manager_deinit_device_by_name(kAudioAdcDeviceName);
        adc_handle_ = nullptr;
        return false;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Audio codec input initialized");
    return true;
}

void AudioCodecInput::Deinit() {
    Close();
    if (initialized_) {
        const esp_err_t ret = esp_board_manager_deinit_device_by_name(kAudioAdcDeviceName);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Audio ADC release failed: %s", esp_err_to_name(ret));
        }
        initialized_ = false;
        adc_handle_ = nullptr;
        ESP_LOGI(TAG, "Audio codec input deinitialized");
    }
}

bool AudioCodecInput::Open(uint32_t sample_rate,
                           uint16_t channels,
                           uint16_t bits_per_sample,
                           int gain,
                           uint16_t channel_mask) {
    if (!Init()) {
        return false;
    }

    if (codec_open_ && MatchesOpenFormat(sample_rate, channels, bits_per_sample, channel_mask)) {
        return SetGain(gain);
    }
    if (codec_open_) {
        Close();
    }

    esp_codec_dev_handle_t codec = CodecFromHandle(adc_handle_);
    if (codec == nullptr) {
        ESP_LOGE(TAG, "Audio ADC unavailable");
        return false;
    }

    esp_codec_dev_sample_info_t sample_info = {};
    sample_info.sample_rate = sample_rate;
    sample_info.channel = static_cast<uint8_t>(channels);
    sample_info.bits_per_sample = static_cast<uint8_t>(bits_per_sample);
    sample_info.channel_mask = channel_mask;
    sample_info.mclk_multiple = (sample_rate % 11025U) == 0 ? 384 : 256;

    const int ret = OpenCodecSuppressingBenignI2sDisable(codec, &sample_info);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open ADC codec: %d", ret);
        ClearOpenFormat();
        return false;
    }

    codec_open_ = true;
    sample_rate_ = sample_rate;
    channels_ = channels;
    bits_per_sample_ = bits_per_sample;
    channel_mask_ = channel_mask;
    SetGain(gain);
    return true;
}

void AudioCodecInput::Close() {
    esp_codec_dev_handle_t codec = CodecFromHandle(adc_handle_);
    if (codec_open_ && codec != nullptr) {
        esp_codec_dev_close(codec);
    }
    ClearOpenFormat();
}

bool AudioCodecInput::Read(void* data, int bytes) {
    if (data == nullptr || bytes <= 0 || !codec_open_) {
        return false;
    }

    esp_codec_dev_handle_t codec = CodecFromHandle(adc_handle_);
    if (codec == nullptr) {
        return false;
    }

    const int ret = esp_codec_dev_read(codec, data, bytes);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Codec read failed: %d", ret);
        return false;
    }
    return true;
}

bool AudioCodecInput::SetGain(int gain) {
    esp_codec_dev_handle_t codec = CodecFromHandle(adc_handle_);
    if (!codec_open_ || codec == nullptr) {
        return true;
    }

    const int ret = esp_codec_dev_set_in_gain(codec, gain);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set input gain to %d", gain);
        return false;
    }
    return true;
}

bool AudioCodecInput::MatchesOpenFormat(uint32_t sample_rate,
                                        uint16_t channels,
                                        uint16_t bits_per_sample,
                                        uint16_t channel_mask) const {
    return codec_open_ &&
           sample_rate_ == sample_rate &&
           channels_ == channels &&
           bits_per_sample_ == bits_per_sample &&
           channel_mask_ == channel_mask;
}

void AudioCodecInput::ClearOpenFormat() {
    codec_open_ = false;
    sample_rate_ = 0;
    channels_ = 0;
    bits_per_sample_ = 0;
    channel_mask_ = 0;
}

}  // namespace rodakos

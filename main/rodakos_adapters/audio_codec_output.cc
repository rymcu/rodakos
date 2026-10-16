#include "rodakos_adapters/audio_codec_output.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include <dev_audio_codec.h>
#include <esp_board_manager.h>
#include <esp_codec_dev.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_log_level.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "AudioCodecOutput";
constexpr const char* kAudioDacDeviceName = "audio_dac";
constexpr const char* kI2sCommonTag = "i2s_common";

// esp_codec_dev's default curve maps 1-100 linearly onto -50..0 dB, which leaves the default
// volume 60 at -20 dB and hard to hear on BigSmart's small speaker. Lift the middle of the range
// but keep the 100 endpoint at 0 dB so maximum output and ES8311 clipping headroom are unchanged.
//
// The curve is applied here instead of through esp_codec_dev_set_vol_curve(): that call reallocates
// a small internal-RAM block on the first playback and keeps it for the device lifetime. On BigSmart
// that block landed inside a fragmented heap and left too little contiguous RAM for the voice
// WebSocket task.
constexpr struct {
    int volume;
    float db;
} kVolumeCurve[] = {
    {1, -40.0f},
    {50, -12.0f},
    {100, 0.0f},
};
// Linear span of the codec's default curve for volumes 1..100.
constexpr float kCodecDefaultMinDb = -50.0f;
constexpr float kCodecDefaultMaxDb = 0.0f;

float VolumeCurveDb(int volume) {
    if (volume >= kVolumeCurve[std::size(kVolumeCurve) - 1].volume) {
        return kVolumeCurve[std::size(kVolumeCurve) - 1].db;
    }
    for (size_t i = 1; i < std::size(kVolumeCurve); ++i) {
        const auto& upper = kVolumeCurve[i];
        if (volume < upper.volume) {
            const auto& lower = kVolumeCurve[i - 1];
            return lower.db + (volume - lower.volume) * (upper.db - lower.db) /
                                  static_cast<float>(upper.volume - lower.volume);
        }
    }
    return kVolumeCurve[0].db;
}

// Returns the codec-default volume whose dB matches this project's curve.
int ToCodecVolume(int volume) {
    if (volume <= 0) {
        return 0;
    }
    const float db = std::clamp(VolumeCurveDb(volume), kCodecDefaultMinDb, kCodecDefaultMaxDb);
    const float codec_volume =
        (db - kCodecDefaultMinDb) * 100.0f / (kCodecDefaultMaxDb - kCodecDefaultMinDb);
    // Volume 0 means mute in esp_codec_dev, so audible settings must stay at least 1.
    return std::clamp(static_cast<int>(std::lround(codec_volume)), 1, 100);
}

esp_codec_dev_handle_t CodecFromHandle(void* dac_handle) {
    auto* handle = static_cast<dev_audio_codec_handles_t*>(dac_handle);
    return handle != nullptr ? handle->codec_dev : nullptr;
}

int OpenCodecSuppressingBenignI2sDisable(esp_codec_dev_handle_t codec,
                                         esp_codec_dev_sample_info_t* sample_info) {
    // esp_codec_dev's I2S data-if disables before set_fmt even when it already tracks the channel as disabled.
    const esp_log_level_t previous_level = esp_log_level_get(kI2sCommonTag);
    esp_log_level_set(kI2sCommonTag, ESP_LOG_NONE);
    const int ret = esp_codec_dev_open(codec, sample_info);
    esp_log_level_set(kI2sCommonTag, previous_level);
    return ret;
}
}  // namespace

int AudioCodecVolumeForUserVolume(int volume) {
    return ToCodecVolume(volume);
}

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

    if (codec_open_ && MatchesOpenFormat(sample_rate, channels, bits_per_sample)) {
        return SetVolume(volume);
    }
    if (codec_open_) {
        Close();
    }

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

    const int ret = OpenCodecSuppressingBenignI2sDisable(codec, &sample_info);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open codec: %d", ret);
        ClearOpenFormat();
        return false;
    }

    codec_open_ = true;
    sample_rate_ = sample_rate;
    channels_ = channels;
    bits_per_sample_ = bits_per_sample;
    const int vol_ret = esp_codec_dev_set_out_vol(codec, ToCodecVolume(volume));
    if (vol_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set volume to %d", volume);
        Close();
        return false;
    }
    return true;
}

void AudioCodecOutput::Close() {
    esp_codec_dev_handle_t codec = CodecFromHandle(dac_handle_);
    if (codec_open_ && codec != nullptr) {
        esp_codec_dev_close(codec);
    }
    ClearOpenFormat();
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

    const int ret = esp_codec_dev_set_out_vol(codec, ToCodecVolume(volume));
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set volume to %d", volume);
        return false;
    }
    return true;
}

bool AudioCodecOutput::MatchesOpenFormat(uint32_t sample_rate,
                                         uint16_t channels,
                                         uint16_t bits_per_sample) const {
    return codec_open_ &&
           sample_rate_ == sample_rate &&
           channels_ == channels &&
           bits_per_sample_ == bits_per_sample;
}

void AudioCodecOutput::ClearOpenFormat() {
    codec_open_ = false;
    sample_rate_ = 0;
    channels_ = 0;
    bits_per_sample_ = 0;
}

}  // namespace rodakos

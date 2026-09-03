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
constexpr const char* kLegacyOwner = "legacy-audio-input";

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

AudioCodecInput::AudioCodecInput() {
    mutex_ = xSemaphoreCreateMutex();
}

AudioCodecInput::~AudioCodecInput() {
    Deinit();
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool AudioCodecInput::Init() {
    if (mutex_ == nullptr) {
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool initialized = InitLocked();
    xSemaphoreGive(mutex_);
    return initialized;
}

bool AudioCodecInput::InitLocked() {
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
    if (mutex_ == nullptr) {
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    CloseLocked();
    if (initialized_) {
        const esp_err_t ret = esp_board_manager_deinit_device_by_name(kAudioAdcDeviceName);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Audio ADC release failed: %s", esp_err_to_name(ret));
        }
        initialized_ = false;
        adc_handle_ = nullptr;
        ESP_LOGI(TAG, "Audio codec input deinitialized");
    }
    xSemaphoreGive(mutex_);
}

bool AudioCodecInput::Open(uint32_t sample_rate,
                           uint16_t channels,
                           uint16_t bits_per_sample,
                           int gain,
                           uint16_t channel_mask) {
    return OpenForOwner(kLegacyOwner,
                        0,
                        sample_rate,
                        channels,
                        bits_per_sample,
                        gain,
                        channel_mask);
}

bool AudioCodecInput::OpenForOwner(const char* owner,
                                   int priority,
                                   uint32_t sample_rate,
                                   uint16_t channels,
                                   uint16_t bits_per_sample,
                                   int gain,
                                   uint16_t channel_mask) {
    if (mutex_ == nullptr || owner == nullptr || owner[0] == '\0') {
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!owner_.empty() && owner_ != owner && priority <= owner_priority_) {
        xSemaphoreGive(mutex_);
        return false;
    }

    if (!owner_.empty() && owner_ != owner) {
        ESP_LOGI(TAG, "Audio input owner changed: %s -> %s", owner_.c_str(), owner);
        CloseLocked();
    }

    if (!InitLocked()) {
        xSemaphoreGive(mutex_);
        return false;
    }

    if (codec_open_ && MatchesOpenFormat(sample_rate, channels, bits_per_sample, channel_mask)) {
        owner_ = owner;
        owner_priority_ = priority;
        const bool gain_set = SetGainLocked(gain);
        xSemaphoreGive(mutex_);
        return gain_set;
    }
    if (codec_open_) {
        CloseLocked();
    }

    esp_codec_dev_handle_t codec = CodecFromHandle(adc_handle_);
    if (codec == nullptr) {
        ESP_LOGE(TAG, "Audio ADC unavailable");
        xSemaphoreGive(mutex_);
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
        const int close_ret = esp_codec_dev_close(codec);
        if (close_ret != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "Failed to clean up ADC codec after open error: %d", close_ret);
        }
        ClearOpenFormat();
        xSemaphoreGive(mutex_);
        return false;
    }

    codec_open_ = true;
    sample_rate_ = sample_rate;
    channels_ = channels;
    bits_per_sample_ = bits_per_sample;
    channel_mask_ = channel_mask;
    owner_ = owner;
    owner_priority_ = priority;
    const bool gain_set = SetGainLocked(gain);
    xSemaphoreGive(mutex_);
    return gain_set;
}

void AudioCodecInput::Close() {
    CloseForOwner(kLegacyOwner);
}

void AudioCodecInput::CloseForOwner(const char* owner) {
    if (mutex_ == nullptr || owner == nullptr) {
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (owner_ == owner) {
        CloseLocked();
    }
    xSemaphoreGive(mutex_);
}

void AudioCodecInput::CloseLocked() {
    esp_codec_dev_handle_t codec = CodecFromHandle(adc_handle_);
    if (codec_open_ && codec != nullptr) {
        esp_codec_dev_close(codec);
    }
    ClearOpenFormat();
    owner_.clear();
    owner_priority_ = 0;
}

bool AudioCodecInput::Read(void* data, int bytes) {
    return ReadForOwner(kLegacyOwner, data, bytes);
}

bool AudioCodecInput::ReadForOwner(const char* owner, void* data, int bytes) {
    if (mutex_ == nullptr || owner == nullptr || data == nullptr || bytes <= 0) {
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!codec_open_ || owner_ != owner) {
        xSemaphoreGive(mutex_);
        return false;
    }

    esp_codec_dev_handle_t codec = CodecFromHandle(adc_handle_);
    if (codec == nullptr) {
        xSemaphoreGive(mutex_);
        return false;
    }

    const int ret = esp_codec_dev_read(codec, data, bytes);
    xSemaphoreGive(mutex_);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Codec read failed: %d", ret);
        return false;
    }
    return true;
}

bool AudioCodecInput::SetGain(int gain) {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool gain_set = SetGainLocked(gain);
    xSemaphoreGive(mutex_);
    return gain_set;
}

bool AudioCodecInput::SetGainLocked(int gain) {
    esp_codec_dev_handle_t codec = CodecFromHandle(adc_handle_);
    if (!codec_open_ || codec == nullptr) {
        return true;
    }
    if (gain_configured_ && gain_ == gain) {
        return true;
    }

    const int ret = esp_codec_dev_set_in_gain(codec, gain);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set input gain to %d", gain);
        return false;
    }
    gain_ = gain;
    gain_configured_ = true;
    return true;
}

bool AudioCodecInput::IsOpen() const {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool open = codec_open_;
    xSemaphoreGive(mutex_);
    return open;
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
    gain_ = 0;
    gain_configured_ = false;
}

}  // namespace rodakos

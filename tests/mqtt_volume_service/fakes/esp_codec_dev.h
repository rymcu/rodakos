#pragma once
#include <cstdint>
#include <functional>
using esp_codec_dev_handle_t = void*;
constexpr int ESP_CODEC_DEV_OK = 0;
struct esp_codec_dev_sample_info_t {
    uint8_t bits_per_sample; uint8_t channel; uint16_t channel_mask;
    uint32_t sample_rate; uint16_t mclk_multiple;
};
namespace fake_codec {
inline bool fail_volume_write = false;
inline bool open = false;
inline int volume = -1;
inline int volume_writes = 0;
inline int opens = 0;
inline int closes = 0;
inline int initializations = 0;
inline int deinitializations = 0;
inline int handle = 0;
inline std::function<void()> before_volume_write;
inline void Reset() {
    fail_volume_write = open = false;
    volume = -1;
    volume_writes = opens = closes = initializations = deinitializations = 0;
    before_volume_write = {};
}
}
inline int esp_codec_dev_open(esp_codec_dev_handle_t, esp_codec_dev_sample_info_t*) {
    ++fake_codec::opens; fake_codec::open = true; return ESP_CODEC_DEV_OK;
}
inline int esp_codec_dev_close(esp_codec_dev_handle_t) {
    ++fake_codec::closes; fake_codec::open = false; return ESP_CODEC_DEV_OK;
}
inline int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t, int volume) {
    if (fake_codec::before_volume_write) fake_codec::before_volume_write();
    ++fake_codec::volume_writes;
    if (!fake_codec::open || fake_codec::fail_volume_write) return -1;
    fake_codec::volume = volume;
    return ESP_CODEC_DEV_OK;
}
inline int esp_codec_dev_write(esp_codec_dev_handle_t, void*, int) {
    return fake_codec::open ? ESP_CODEC_DEV_OK : -1;
}

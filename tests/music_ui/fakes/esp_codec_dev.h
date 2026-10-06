#pragma once

#include <cstdint>
#include <atomic>
#include <functional>

using esp_codec_dev_handle_t = void*;
constexpr int ESP_CODEC_DEV_OK = 0;
struct esp_codec_dev_sample_info_t {
    uint8_t bits_per_sample;
    uint8_t channel;
    uint16_t channel_mask;
    uint32_t sample_rate;
    uint16_t mclk_multiple;
};

namespace fake_codec {
inline std::atomic<bool> fail_volume_write = false;
inline std::atomic<bool> fail_write = false;
inline std::function<void()> open_hook;
inline std::function<void()> write_hook;
inline std::atomic<bool> open = false;
inline std::atomic<int> volume = -1;
inline std::atomic<int> volume_writes = 0;
inline std::atomic<int> opens = 0;
inline std::atomic<int> closes = 0;
inline std::atomic<int> initializations = 0;
inline std::atomic<int> deinitializations = 0;
inline std::atomic<int> handle = 0;

inline void Reset() {
    fail_volume_write = false;
    fail_write = false;
    open_hook = {};
    write_hook = {};
    open = false;
    volume = -1;
    volume_writes = 0;
    opens = 0;
    closes = 0;
    initializations = 0;
    deinitializations = 0;
}
}  // namespace fake_codec

inline int esp_codec_dev_open(esp_codec_dev_handle_t, esp_codec_dev_sample_info_t*) {
    if (fake_codec::open_hook) fake_codec::open_hook();
    ++fake_codec::opens;
    fake_codec::open = true;
    return ESP_CODEC_DEV_OK;
}
inline int esp_codec_dev_close(esp_codec_dev_handle_t) {
    ++fake_codec::closes;
    fake_codec::open = false;
    return ESP_CODEC_DEV_OK;
}
inline int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t, int volume) {
    ++fake_codec::volume_writes;
    if (!fake_codec::open || fake_codec::fail_volume_write) return -1;
    fake_codec::volume = volume;
    return ESP_CODEC_DEV_OK;
}
inline int esp_codec_dev_write(esp_codec_dev_handle_t, void*, int) {
    if (fake_codec::write_hook) fake_codec::write_hook();
    return fake_codec::open && !fake_codec::fail_write ? ESP_CODEC_DEV_OK : -1;
}

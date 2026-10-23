#pragma once

#include <cstdint>
#include <functional>
#include <vector>

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
inline bool fail_volume_write = false;
inline bool open = false;
inline int volume = -1;
inline int volume_writes = 0;
inline int opens = 0;
inline int closes = 0;
inline int initializations = 0;
inline int deinitializations = 0;
inline int handle = 0;
inline esp_codec_dev_sample_info_t format{};
inline std::vector<int16_t> written;
// Called before each write is recorded; lets a test block the speaker mid-reply.
inline std::function<void()> on_write;

inline void Reset() {
    fail_volume_write = false;
    open = false;
    volume = -1;
    volume_writes = 0;
    opens = 0;
    closes = 0;
    initializations = 0;
    deinitializations = 0;
    format = {};
    written.clear();
    on_write = {};
}
}  // namespace fake_codec

inline int esp_codec_dev_open(esp_codec_dev_handle_t, esp_codec_dev_sample_info_t* format) {
    ++fake_codec::opens;
    fake_codec::format = *format;
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
inline int esp_codec_dev_write(esp_codec_dev_handle_t, void* data, int bytes) {
    if (fake_codec::on_write) fake_codec::on_write();
    if (!fake_codec::open) return -1;
    const auto* samples = static_cast<const int16_t*>(data);
    fake_codec::written.insert(fake_codec::written.end(), samples, samples + bytes / sizeof(int16_t));
    return ESP_CODEC_DEV_OK;
}

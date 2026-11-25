#pragma once

#include <cstring>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#define ESP_CODEC_DEV_MAKE_CHANNEL_MASK(channel) (static_cast<uint16_t>(1U << (channel)))

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
inline bool fail_in_gain_write = false;
inline bool fail_in_channel_gain_write = false;
inline uint16_t fail_in_channel_gain_mask = 0;
inline uint32_t required_sample_rate = 0;
inline bool open = false;
inline int volume = -1;
inline int volume_writes = 0;
inline int in_gain_writes = 0;
inline float last_in_gain_db = -1.0f;
inline std::vector<std::pair<uint16_t, float>> in_channel_gain_writes;
inline int opens = 0;
inline int closes = 0;
inline int initializations = 0;
inline int deinitializations = 0;
inline int handle = 0;
inline esp_codec_dev_sample_info_t format{};
inline std::mutex written_mutex;
inline std::vector<int16_t> written;
// Called before each write is recorded; lets a test block the speaker mid-reply.
inline std::function<void()> on_write;

inline void Reset() {
    fail_volume_write = false;
    fail_in_gain_write = false;
    fail_in_channel_gain_write = false;
    fail_in_channel_gain_mask = 0;
    required_sample_rate = 0;
    open = false;
    volume = -1;
    volume_writes = 0;
    in_gain_writes = 0;
    last_in_gain_db = -1.0f;
    in_channel_gain_writes.clear();
    opens = 0;
    closes = 0;
    initializations = 0;
    deinitializations = 0;
    format = {};
    {
        std::lock_guard<std::mutex> lock(written_mutex);
        written.clear();
    }
    on_write = {};
}
}  // namespace fake_codec

inline int esp_codec_dev_open(esp_codec_dev_handle_t, esp_codec_dev_sample_info_t* format) {
    ++fake_codec::opens;
    if (fake_codec::required_sample_rate != 0 &&
        format->sample_rate != fake_codec::required_sample_rate) {
        return -1;
    }
    fake_codec::format = *format;
    fake_codec::open = true;
    return ESP_CODEC_DEV_OK;
}
inline int esp_codec_dev_close(esp_codec_dev_handle_t) {
    ++fake_codec::closes;
    fake_codec::open = false;
    fake_codec::last_in_gain_db = 0.0f;
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
    std::lock_guard<std::mutex> lock(fake_codec::written_mutex);
    fake_codec::written.insert(fake_codec::written.end(), samples, samples + bytes / sizeof(int16_t));
    return ESP_CODEC_DEV_OK;
}
inline int esp_codec_dev_read(esp_codec_dev_handle_t, void* data, int bytes) {
    if (!fake_codec::open || data == nullptr || bytes <= 0) return -1;
    std::memset(data, 0, static_cast<size_t>(bytes));
    return ESP_CODEC_DEV_OK;
}
inline int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t, float db) {
    ++fake_codec::in_gain_writes;
    if (!fake_codec::open || fake_codec::fail_in_gain_write) return -1;
    fake_codec::last_in_gain_db = db;
    return ESP_CODEC_DEV_OK;
}
inline int esp_codec_dev_set_in_channel_gain(esp_codec_dev_handle_t, uint16_t mask, float db) {
    fake_codec::in_channel_gain_writes.emplace_back(mask, db);
    if (!fake_codec::open || fake_codec::fail_in_channel_gain_write ||
        (fake_codec::fail_in_channel_gain_mask != 0 && mask == fake_codec::fail_in_channel_gain_mask)) {
        return -1;
    }
    fake_codec::last_in_gain_db = 0.0f;
    return ESP_CODEC_DEV_OK;
}

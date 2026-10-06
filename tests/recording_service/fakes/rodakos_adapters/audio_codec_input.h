#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>
namespace rodakos {
class AudioCodecInput {
public:
    bool OpenForOwner(const char* next, int priority, uint32_t rate, uint16_t channels,
                      uint16_t bits, int gain, uint16_t mask = 0) {
        ++open_calls;
        if (open_hook) open_hook();
        if (fail_open) return false;
        std::lock_guard<std::mutex> lock(mutex);
        owner = next; last_priority = priority; sample_rate = rate; input_channels = channels;
        bits_per_sample = bits; configured_gain = gain; channel_mask = mask;
        return true;
    }
    bool ReadForOwner(const char* current, void* data, int bytes) {
        ++read_calls;
        { std::lock_guard<std::mutex> lock(mutex); if (owner != current) return false; }
        if (read_hook) return read_hook(data, bytes);
        if (fail_read) return false;
        std::memset(data, 0x3c, static_cast<size_t>(bytes));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return true;
    }
    void CloseForOwner(const char* current) {
        std::lock_guard<std::mutex> lock(mutex);
        if (owner == current) { owner.clear(); ++close_calls; }
    }
    std::mutex mutex;
    std::string owner;
    std::atomic<bool> fail_open{false}, fail_read{false};
    std::atomic<unsigned> open_calls{0}, read_calls{0}, close_calls{0};
    std::function<void()> open_hook;
    std::function<bool(void*, int)> read_hook;
    int last_priority = 0, configured_gain = 0;
    uint32_t sample_rate = 0;
    uint16_t input_channels = 0, bits_per_sample = 0, channel_mask = 0;
};
}

#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace rodakos {
class AudioOutputService {
public:
    bool Init() { ready = true; return true; }
    void Deinit() { std::lock_guard<std::mutex> lock(mutex); ready = false; owner.clear(); ++deinit_calls; }
    bool IsReady() const { return ready; }
    bool OpenForOwner(const char* next, uint32_t, uint16_t, uint16_t) {
        ++open_calls;
        if (open_hook) open_hook();
        if (fail_open) return false;
        std::lock_guard<std::mutex> lock(mutex); owner = next; return true;
    }
    void CloseForOwner(const char* current) {
        std::lock_guard<std::mutex> lock(mutex);
        if (owner == current) { owner.clear(); ++close_calls; }
    }
    bool IsOpenForOwner(const char* current) {
        std::lock_guard<std::mutex> lock(mutex); return owner == current;
    }
    bool WriteForOwner(const char* current, const void* data, int bytes) {
        ++write_calls;
        if (write_hook) write_hook();
        if (fail_write) return false;
        std::lock_guard<std::mutex> lock(mutex);
        if (owner != current || bytes <= 0) return false;
        const auto* start = static_cast<const uint8_t*>(data);
        written.insert(written.end(), start, start + bytes);
        return true;
    }
    bool SetVolume(int next) { configured_volume = next; return true; }
    int volume() const { return configured_volume; }
    std::mutex mutex;
    std::string owner;
    std::vector<uint8_t> written;
    std::function<void()> open_hook, write_hook;
    std::atomic<bool> ready{false}, fail_open{false}, fail_write{false};
    std::atomic<int> configured_volume{60};
    std::atomic<unsigned> open_calls{0}, close_calls{0}, write_calls{0}, deinit_calls{0};
};
}

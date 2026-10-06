#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
namespace rodakos {
enum class AudioFocusGain { kDuck, kPause, kExclusive };
struct AudioFocusRequest {
    std::string owner;
    AudioFocusGain gain = AudioFocusGain::kPause;
    int duck_volume = 20;
    bool resume_on_release = true;
    bool release_playback_hardware = false;
};
class AudioFocusService {
public:
    bool RequestFocus(const AudioFocusRequest& request, uint32_t& result) {
        ++requests;
        if (request_hook) request_hook();
        if (fail_request) return false;
        std::lock_guard<std::mutex> lock(mutex);
        last_request = request; result = ++next_token; token = result; return true;
    }
    bool ReleaseFocus(uint32_t current) {
        std::lock_guard<std::mutex> lock(mutex);
        ++releases;
        if (current == 0 || current != token) { ++invalid_releases; return false; }
        token = 0; return true;
    }
    std::mutex mutex;
    AudioFocusRequest last_request;
    uint32_t token = 0, next_token = 0;
    std::atomic<unsigned> requests{0}, releases{0}, invalid_releases{0};
    std::atomic<bool> fail_request{false};
    std::function<void()> request_hook;
};
}

#pragma once
#include <cstdint>
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
    bool RequestFocus(const AudioFocusRequest& request, uint32_t& token) {
        last_request = request;
        ++requests;
        token = allow ? ++next_token : 0;
        return allow;
    }
    bool ReleaseFocus(uint32_t token) { ++releases; last_released = token; return true; }
    AudioFocusRequest last_request;
    bool allow = true;
    int requests = 0;
    int releases = 0;
    uint32_t next_token = 0;
    uint32_t last_released = 0;
};
}

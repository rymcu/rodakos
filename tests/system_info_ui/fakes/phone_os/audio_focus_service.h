#pragma once
#include <cstdint>
#include <string>
namespace rodakos {
enum class AudioFocusGain { kDuck, kPause, kExclusive };
struct AudioFocusState {
    bool active = false;
    uint32_t token = 0;
    std::string owner;
    AudioFocusGain gain = AudioFocusGain::kPause;
};
class AudioFocusService {
public:
    AudioFocusState GetState() { return state; }
    AudioFocusState state;
};
}  // namespace rodakos

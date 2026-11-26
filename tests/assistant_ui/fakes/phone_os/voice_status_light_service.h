#pragma once
// Host fake: only the settings surface the Assistant page reads and writes.
#include <cstdint>
namespace rodakos {
struct VoiceStatusLightSettings {
    bool enabled = true;
    uint8_t max_brightness_percent = 30;
};
class VoiceStatusLightService {
public:
    VoiceStatusLightSettings settings() { return current; }
    bool SetSettings(const VoiceStatusLightSettings& next) {
        ++saves;
        if (!save_ok) return false;
        current = next;
        return true;
    }
    VoiceStatusLightSettings current;
    bool save_ok = true;
    int saves = 0;
};
}  // namespace rodakos

#pragma once

#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace rodakos {

class MusicPlayerService;

enum class AudioFocusGain {
    kDuck,
    kPause,
    kExclusive,
};

struct AudioFocusRequest {
    std::string owner;
    AudioFocusGain gain = AudioFocusGain::kPause;
    int duck_volume = 20;
    bool resume_on_release = true;
};

struct AudioFocusState {
    bool active = false;
    uint32_t token = 0;
    std::string owner;
    AudioFocusGain gain = AudioFocusGain::kPause;
};

class AudioFocusService {
public:
    explicit AudioFocusService(MusicPlayerService& music_player);
    ~AudioFocusService();

    bool RequestFocus(const AudioFocusRequest& request, uint32_t& token);
    bool ReleaseFocus(uint32_t token);
    void ReleaseOwner(const std::string& owner);
    AudioFocusState GetState();

private:
    void ApplyFocusLocked(const AudioFocusRequest& request);
    void RestoreFocusLocked();
    void ClearFocusLocked();

    MusicPlayerService& music_player_;
    SemaphoreHandle_t mutex_ = nullptr;
    bool active_ = false;
    uint32_t active_token_ = 0;
    uint32_t next_token_ = 1;
    std::string owner_;
    AudioFocusGain gain_ = AudioFocusGain::kPause;
    bool resume_on_release_ = true;
    bool music_was_playing_ = false;
    bool music_was_paused_ = false;
    int previous_volume_ = 60;
};

}  // namespace rodakos

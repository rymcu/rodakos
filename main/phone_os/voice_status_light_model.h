#pragma once

#include "phone_os/voice_assistant_state.h"

#include <cstdint>

namespace rodakos {

struct VoiceStatusLightFrame {
    // false: the light shows the user's own setting.
    bool active = false;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    // 0..100, relative to the configured maximum brightness.
    uint8_t level = 0;
};

// Turns assistant snapshots into light frames. Pure and clock-driven so host tests can step it.
class VoiceStatusLightModel {
public:
    static constexpr int64_t kWakeFlashMs = 150;
    static constexpr int64_t kBargeInFlashMs = 120;
    static constexpr int64_t kBlinkPeriodMs = 500;
    static constexpr int64_t kErrorBlinkMs = 150;
    static constexpr int kErrorBlinks = 3;
    static constexpr int64_t kEndFadeMs = 500;
    static constexpr int64_t kFollowUpFadeMs = 5000;

    static constexpr int64_t kBreathPeriodMs = 1500;

    VoiceStatusLightFrame Update(const VoiceStatusLightSnapshot& snapshot, int64_t now_ms);
    void Reset();

    // Maps the RMS of one decoded reply frame to 0..255: -50 dBFS and below is 0, -10 dBFS and
    // above is 255. Speech sits in between, so the light follows syllables, not the volume step.
    static uint8_t LevelFromRmsDbfs(float rms_dbfs);

private:
    VoiceStatusLightFrame StateFrame(const VoiceStatusLightSnapshot& snapshot, int64_t now_ms);

    bool primed_ = false;
    VoiceAssistantPhase previous_phase_ = VoiceAssistantPhase::kIdle;
    uint32_t previous_starts_ = 0;
    uint32_t previous_barge_ins_ = 0;
    int64_t flash_until_ms_ = 0;
    int64_t error_since_ms_ = -1;
    int64_t fade_since_ms_ = -1;
    VoiceStatusLightFrame last_shown_;
};

}  // namespace rodakos

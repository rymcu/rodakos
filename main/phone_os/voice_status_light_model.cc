#include "phone_os/voice_status_light_model.h"

#include <algorithm>

namespace rodakos {
namespace {

constexpr VoiceStatusLightFrame Solid(uint8_t red, uint8_t green, uint8_t blue, uint8_t level) {
    return {true, red, green, blue, level};
}

constexpr VoiceStatusLightFrame kWhite = Solid(255, 255, 255, 100);
constexpr VoiceStatusLightFrame kBlue = Solid(0, 90, 255, 100);
constexpr VoiceStatusLightFrame kAmber = Solid(255, 140, 0, 100);
constexpr VoiceStatusLightFrame kCyan = Solid(0, 200, 200, 100);
constexpr VoiceStatusLightFrame kGreen = Solid(0, 220, 60, 100);
constexpr VoiceStatusLightFrame kRed = Solid(255, 0, 0, 100);
constexpr VoiceStatusLightFrame kPurple = Solid(160, 0, 255, 100);
constexpr uint8_t kBreathMinLevel = 15;

// Listening is dimmer than speech so that the change when the user talks stays visible.
constexpr uint8_t kListeningLevel = 40;
constexpr uint8_t kSpeakingMinLevel = 25;

VoiceStatusLightFrame WithLevel(VoiceStatusLightFrame frame, int level) {
    frame.level = static_cast<uint8_t>(std::clamp(level, 0, 100));
    return frame;
}

bool IsActivePhase(VoiceAssistantPhase phase) {
    return phase == VoiceAssistantPhase::kConnecting || phase == VoiceAssistantPhase::kListening ||
           phase == VoiceAssistantPhase::kSpeaking;
}

}  // namespace

void VoiceStatusLightModel::Reset() {
    *this = VoiceStatusLightModel{};
}

VoiceStatusLightFrame VoiceStatusLightModel::Update(const VoiceStatusLightSnapshot& snapshot,
                                                    int64_t now_ms) {
    if (!primed_) {
        // Counters that were already set before the light started are history, not events.
        primed_ = true;
        previous_starts_ = snapshot.interaction_starts;
        previous_barge_ins_ = snapshot.barge_ins;
        previous_phase_ = snapshot.phase;
    }

    if (snapshot.interaction_starts != previous_starts_) {
        flash_until_ms_ = now_ms + kWakeFlashMs;
        fade_since_ms_ = -1;
        error_since_ms_ = -1;
    } else if (snapshot.barge_ins != previous_barge_ins_) {
        flash_until_ms_ = now_ms + kBargeInFlashMs;
    }
    if (snapshot.phase == VoiceAssistantPhase::kError &&
        previous_phase_ != VoiceAssistantPhase::kError) {
        error_since_ms_ = now_ms;
        fade_since_ms_ = -1;
    }
    if (snapshot.phase == VoiceAssistantPhase::kIdle && IsActivePhase(previous_phase_) &&
        last_shown_.active) {
        fade_since_ms_ = now_ms;
    }
    previous_starts_ = snapshot.interaction_starts;
    previous_barge_ins_ = snapshot.barge_ins;
    previous_phase_ = snapshot.phase;

    if (now_ms < flash_until_ms_) {
        return kWhite;
    }

    if (snapshot.phase == VoiceAssistantPhase::kIdle && fade_since_ms_ >= 0) {
        const int64_t elapsed = now_ms - fade_since_ms_;
        if (elapsed < kEndFadeMs) {
            const int64_t remaining = kEndFadeMs - elapsed;
            return WithLevel(last_shown_, static_cast<int>(last_shown_.level * remaining / kEndFadeMs));
        }
        fade_since_ms_ = -1;
    }

    const VoiceStatusLightFrame frame = StateFrame(snapshot, now_ms);
    if (frame.active && IsActivePhase(snapshot.phase)) {
        last_shown_ = frame;
    }
    return frame;
}

VoiceStatusLightFrame VoiceStatusLightModel::StateFrame(const VoiceStatusLightSnapshot& snapshot,
                                                        int64_t now_ms) {
    switch (snapshot.phase) {
        case VoiceAssistantPhase::kIdle:
            return {};
        case VoiceAssistantPhase::kConnecting: {
            const bool on = (now_ms % kBlinkPeriodMs) < kBlinkPeriodMs / 2;
            return WithLevel(snapshot.reconnecting ? kAmber : kBlue, on ? 100 : 0);
        }
        case VoiceAssistantPhase::kListening: {
            if (snapshot.awaiting_reply) {
                // Triangle wave: no floating point on the 30 Hz path.
                const int64_t phase = now_ms % kBreathPeriodMs;
                const int64_t half = kBreathPeriodMs / 2;
                const int64_t rise = phase < half ? phase : kBreathPeriodMs - phase;
                return WithLevel(kPurple, kBreathMinLevel +
                                              static_cast<int>(rise * (100 - kBreathMinLevel) / half));
            }
            int level = snapshot.user_speech ? 100 : kListeningLevel;
            if (snapshot.follow_up_remaining_ms >= 0 &&
                snapshot.follow_up_remaining_ms < kFollowUpFadeMs) {
                level = static_cast<int>(level * snapshot.follow_up_remaining_ms / kFollowUpFadeMs);
            }
            return WithLevel(kCyan, level);
        }
        case VoiceAssistantPhase::kSpeaking:
            return WithLevel(kGreen, kSpeakingMinLevel +
                                         snapshot.playback_level * (100 - kSpeakingMinLevel) / 255);
        case VoiceAssistantPhase::kError: {
            if (error_since_ms_ < 0) {
                return {};
            }
            const int64_t elapsed = now_ms - error_since_ms_;
            if (elapsed >= kErrorBlinkMs * 2 * kErrorBlinks) {
                return {};
            }
            return WithLevel(kRed, (elapsed / kErrorBlinkMs) % 2 == 0 ? 100 : 0);
        }
    }
    return {};
}

uint8_t VoiceStatusLightModel::LevelFromRmsDbfs(float rms_dbfs) {
    constexpr float kFloor = -50.0f;
    constexpr float kCeiling = -10.0f;
    if (!(rms_dbfs > kFloor)) return 0;
    if (rms_dbfs >= kCeiling) return 255;
    return static_cast<uint8_t>((rms_dbfs - kFloor) * 255.0f / (kCeiling - kFloor));
}

}  // namespace rodakos

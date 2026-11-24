#include "phone_os/voice_mic_selector.h"

#include <algorithm>

namespace rodakos {
namespace {
constexpr int64_t kSpeechLockPower = 25000;
constexpr int64_t kSwitchMargin = 180000;
constexpr int kSwitchFrames = 5;
// Same playback test as the wake AEC: loopback mean square above 1024 (about -60 dBFS).
constexpr int64_t kReferenceActivePower = 1024;
// Switching changes the echo path under a converged AEC, and the residual burst that
// follows can pass as near-end speech. Keep the microphone until the echo tail has gone.
constexpr size_t kPlaybackHoldSamples = 16000;
}

void VoiceMicSelector::Update(const int16_t* tdm, size_t frames) {
    if (tdm == nullptr || frames == 0) return;
    int64_t p1 = 0, p2 = 0, reference = 0;
    for (size_t i = 0; i < frames; ++i) {
        const int64_t a = tdm[i * kChannels];
        const int64_t r = tdm[i * kChannels + 1];
        const int64_t b = tdm[i * kChannels + 2];
        p1 += a * a;
        p2 += b * b;
        reference += r * r;
    }
    const int64_t n = static_cast<int64_t>(frames);
    mic1_power_ = (mic1_power_ * 7 + (p1 / n) * 3) / 10;
    mic2_power_ = (mic2_power_ * 7 + (p2 / n) * 3) / 10;
    if (reference / n >= kReferenceActivePower) {
        hold_samples_ = kPlaybackHoldSamples;
    } else {
        hold_samples_ -= std::min(hold_samples_, frames);
    }
    if (hold_samples_ > 0) {
        switch_frames_ = 0;
        return;
    }
    if (speech_lock_) {
        if (std::max(mic1_power_, mic2_power_) < kSpeechLockPower) {
            speech_lock_ = false;
            switch_frames_ = 0;
        }
        return;
    }
    const int64_t current = selected_mic_ == 1 ? mic1_power_ : mic2_power_;
    const int64_t other = selected_mic_ == 1 ? mic2_power_ : mic1_power_;
    if (other > current + kSwitchMargin) {
        if (++switch_frames_ >= kSwitchFrames) {
            selected_mic_ = selected_mic_ == 1 ? 2 : 1;
            switch_frames_ = 0;
            speech_lock_ = true;
        }
    } else {
        switch_frames_ = 0;
        if (std::max(mic1_power_, mic2_power_) > kSpeechLockPower) speech_lock_ = true;
    }
}

}  // namespace rodakos

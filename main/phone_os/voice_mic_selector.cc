#include "phone_os/voice_mic_selector.h"

#include <algorithm>

namespace rodakos {
namespace {
constexpr int64_t kMinSwitchPower = 4000;
constexpr int64_t kSpeechLockPower = 25000;
constexpr int kSwitchFrames = 5;
// Same playback test as the wake AEC: loopback AC power above 1024 (about -60 dBFS).
constexpr int64_t kReferenceActivePower = 1024;
// Switching changes the echo path under a converged AEC, and the residual burst that
// follows can pass as near-end speech. Keep the microphone until the echo tail has gone.
constexpr size_t kPlaybackHoldSamples = 16000;

int64_t AcPower(int64_t sum, int64_t squares, int64_t count) {
    return (squares - sum * sum / count) / count;
}
}

void VoiceMicSelector::Reset() {
    selected_mic_ = 2;
    ResetSession();
}

void VoiceMicSelector::ResetSession() {
    switch_frames_ = 0;
    mic1_power_ = 0;
    mic2_power_ = 0;
    speech_lock_ = false;
    hold_samples_ = 0;
}

void VoiceMicSelector::Update(const int16_t* tdm, size_t frames) {
    if (tdm == nullptr || frames == 0) return;
    int64_t sum1 = 0, sum2 = 0, sum_ref = 0;
    int64_t sq1 = 0, sq2 = 0, sq_ref = 0;
    for (size_t i = 0; i < frames; ++i) {
        const int64_t a = tdm[i * kChannels];
        const int64_t r = tdm[i * kChannels + 1];
        const int64_t b = tdm[i * kChannels + 2];
        sum1 += a;
        sum_ref += r;
        sum2 += b;
        sq1 += a * a;
        sq_ref += r * r;
        sq2 += b * b;
    }
    const int64_t n = static_cast<int64_t>(frames);
    mic1_power_ = (mic1_power_ * 7 + AcPower(sum1, sq1, n) * 3) / 10;
    mic2_power_ = (mic2_power_ * 7 + AcPower(sum2, sq2, n) * 3) / 10;
    if (AcPower(sum_ref, sq_ref, n) >= kReferenceActivePower) {
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
    if (other >= kMinSwitchPower && other > current * 2) {
        if (++switch_frames_ >= kSwitchFrames) {
            selected_mic_ = selected_mic_ == 1 ? 2 : 1;
            switch_frames_ = 0;
            speech_lock_ = true;
        }
    } else {
        switch_frames_ = 0;
        // Only lock when the currently selected mic already leads; locking while the
        // unselected mic is ramping up traps the whole utterance on the weaker channel.
        if (current >= other && current > kSpeechLockPower) speech_lock_ = true;
    }
}

}  // namespace rodakos

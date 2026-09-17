#include "phone_os/voice_barge_in_policy.h"

#include <algorithm>

namespace rodakos {
namespace {
constexpr uint32_t kSilenceRearmMs = 180;
constexpr uint32_t kSpeechConfirmMs = 180;
constexpr uint32_t kTimestampToleranceMs = 30;
constexpr uint32_t kMinimumSpeechPower = 256 * 256;
}

void VoiceVadEndPolicy::Reset() {
    pending_ = false;
    have_frame_ = false;
    silence_ms_ = 0;
}

void VoiceVadEndPolicy::Start(uint32_t timestamp_ms) {
    Reset();
    pending_ = true;
    started_ms_ = timestamp_ms;
}

bool VoiceVadEndPolicy::Observe(uint32_t frame_end_ms, uint32_t duration_ms,
                                bool vad_valid, bool vad_speech) {
    if (!pending_) return false;
    if (!vad_valid || duration_ms == 0 || duration_ms > 120 ||
        static_cast<int32_t>(frame_end_ms - started_ms_) < static_cast<int32_t>(duration_ms)) {
        have_frame_ = false;
        silence_ms_ = 0;
        return false;
    }
    if (have_frame_) {
        const int32_t delta = static_cast<int32_t>(frame_end_ms - last_frame_ms_);
        if (delta <= 0 ||
            delta + static_cast<int32_t>(kTimestampToleranceMs) < static_cast<int32_t>(duration_ms) ||
            delta > static_cast<int32_t>(duration_ms + kTimestampToleranceMs)) {
            silence_ms_ = 0;
        }
    }
    have_frame_ = true;
    last_frame_ms_ = frame_end_ms;
    silence_ms_ = vad_speech ? 0 : silence_ms_ + duration_ms;
    if (silence_ms_ < kSilenceRearmMs) return false;
    Reset();
    return true;
}

void VoicePlaybackEpochPolicy::Reset() {
    current_epoch_ = 0;
    interrupted_epoch_ = 0;
}

bool VoicePlaybackEpochPolicy::IsStale(uint32_t epoch) const {
    return (interrupted_epoch_ != 0 && epoch <= interrupted_epoch_) ||
           (current_epoch_ != 0 && epoch < current_epoch_);
}

bool VoicePlaybackEpochPolicy::AcceptStart(uint32_t epoch) {
    if (IsStale(epoch)) return false;
    current_epoch_ = epoch;
    return true;
}

bool VoicePlaybackEpochPolicy::AcceptAudioOrStop(uint32_t epoch) const {
    return !IsStale(epoch) && epoch == current_epoch_;
}

void VoicePlaybackEpochPolicy::Interrupt() {
    interrupted_epoch_ = current_epoch_;
}

void VoiceBargeInPolicy::ClearEvidence() {
    armed_ = false;
    have_frame_ = false;
    silence_ms_ = 0;
    speech_ms_ = 0;
}

void VoiceBargeInPolicy::Reset() {
    playing_ = false;
    fired_ = false;
    ClearEvidence();
}

void VoiceBargeInPolicy::StartPlayback(uint32_t timestamp_ms) {
    Reset();
    playing_ = true;
    playback_start_ms_ = timestamp_ms;
}

void VoiceBargeInPolicy::StopPlayback() {
    Reset();
}

uint32_t VoiceBargeInPolicy::CalculatePcmPower(const int16_t* samples, size_t count) {
    if (samples == nullptr || count == 0 || count > 16000) return 0;
    int64_t sum = 0;
    int64_t squares = 0;
    for (size_t i = 0; i < count; ++i) {
        const int64_t sample = samples[i];
        sum += sample;
        squares += sample * sample;
    }
    const int64_t n = static_cast<int64_t>(count);
    return static_cast<uint32_t>((squares - sum * sum / n) / n);
}

bool VoiceBargeInPolicy::Observe(uint32_t frame_end_ms, uint32_t duration_ms,
                               bool vad_valid, bool vad_speech, uint32_t pcm_power,
                               uint32_t& onset_ms) {
    if (!playing_ || fired_) {
        return false;
    }
    // Compare modular device-clock differences so a millis rollover is harmless.
    if (!vad_valid || duration_ms == 0 || duration_ms > 120 ||
        static_cast<int32_t>(frame_end_ms - playback_start_ms_) <
            static_cast<int32_t>(duration_ms)) {
        ClearEvidence();
        return false;
    }
    if (have_frame_) {
        const int32_t delta = static_cast<int32_t>(frame_end_ms - last_frame_ms_);
        if (delta <= 0 ||
            delta + static_cast<int32_t>(kTimestampToleranceMs) < static_cast<int32_t>(duration_ms) ||
            delta > static_cast<int32_t>(duration_ms + kTimestampToleranceMs)) {
            ClearEvidence();
        }
    }
    have_frame_ = true;
    last_frame_ms_ = frame_end_ms;
    // WebRTC VAD also labels low-level residual playback as speech after AEC.
    // Require sustained AC energy; DC offset must not turn a quiet frame into speech.
    if (!vad_speech || pcm_power < kMinimumSpeechPower) {
        speech_ms_ = 0;
        silence_ms_ = std::min(kSilenceRearmMs, silence_ms_ + duration_ms);
        armed_ = silence_ms_ >= kSilenceRearmMs;
        return false;
    }
    silence_ms_ = 0;
    if (!armed_) {
        return false;
    }
    if (speech_ms_ == 0) {
        onset_ms_ = frame_end_ms - duration_ms;
    }
    speech_ms_ += duration_ms;
    if (speech_ms_ < kSpeechConfirmMs) {
        return false;
    }
    fired_ = true;
    onset_ms = onset_ms_;
    return true;
}

}  // namespace rodakos

#include "phone_os/voice_barge_in_policy.h"

#include <algorithm>

namespace rodakos {
namespace {
constexpr uint32_t kSilenceRearmMs = 180;
// 180 ms let a burst of residual echo confirm an interruption on its own.
constexpr uint32_t kSpeechConfirmMs = 360;
// After AEC and NS, injected speech during playback passed both energy gates in only
// 3-4 of the 6 frames of a 360 ms VAD run, and residual echo in at most one.
constexpr uint32_t kSpeechEvidenceMs = 180;
// AEC has only adapted to the wake chime when a reply starts, so the first echo
// passes through it at nearly full level.
constexpr uint32_t kPlaybackGraceMs = 400;
// A pause inside the barge-in utterance must not end it.
constexpr uint32_t kVadEndSilenceMs = 600;
constexpr uint32_t kTimestampToleranceMs = 30;
// On BigSmart, AEC+NS left injected near-end speech 4-29 dB (median 12 dB) below the
// microphone input, and residual echo mostly more than 18 dB below it. Output more than
// 15 dB below the input is treated as echo residual; -12 dB still rejected a third of
// the speech frames on hardware.
constexpr uint64_t kEchoResidualRatio = 32;
// At high speaker volume the raw microphone is dominated by acoustic echo before AEC.
// Strong post-AEC speech (-36 dBFS, 4x the floor) gets a wider echo headroom (-19.8 dB)
// so real human double-talk is not rejected when the speaker is loud.
constexpr uint32_t kStrongSpeechPower = 512 * 512;
constexpr uint64_t kStrongSpeechEchoRatio = 96;
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
            delta > static_cast<int32_t>(duration_ms + kTimestampToleranceMs)) {
            silence_ms_ = 0;
        }
    }
    have_frame_ = true;
    last_frame_ms_ = frame_end_ms;
    silence_ms_ = vad_speech ? 0 : silence_ms_ + duration_ms;
    if (silence_ms_ < kVadEndSilenceMs) return false;
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

bool VoicePlaybackEpochPolicy::AcceptStop(uint32_t epoch) const {
    if (epoch == 0) {
        return !IsStale(current_epoch_);
    }
    return AcceptAudioOrStop(epoch);
}

void VoicePlaybackEpochPolicy::Interrupt() {
    interrupted_epoch_ = current_epoch_;
}

void VoiceBargeInPolicy::ClearEvidence() {
    armed_ = false;
    have_frame_ = false;
    silence_ms_ = 0;
    speech_ms_ = 0;
    ClearSpeechEvidence();
}

void VoiceBargeInPolicy::ClearSpeechEvidence() {
    evidence_.fill({});
    evidence_index_ = 0;
}

uint32_t VoiceBargeInPolicy::RecentSpeechEvidenceMs(uint32_t frame_end_ms) const {
    uint32_t total = 0;
    for (const auto& entry : evidence_) {
        if (entry.evidence_ms != 0 &&
            static_cast<int32_t>(frame_end_ms - entry.frame_end_ms) <
                static_cast<int32_t>(kSpeechConfirmMs)) {
            total += entry.evidence_ms;
        }
    }
    return total;
}

void VoiceBargeInPolicy::Reset() {
    playing_ = false;
    fired_ = false;
    stats_ = {};
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
                               uint32_t input_power, uint32_t& onset_ms) {
    if (!playing_ || fired_) {
        return false;
    }
    // Compare modular device-clock differences so a millis rollover is harmless.
    if (!vad_valid || duration_ms == 0 || duration_ms > 120 ||
        static_cast<int32_t>(frame_end_ms - playback_start_ms_) <
            static_cast<int32_t>(duration_ms + kPlaybackGraceMs)) {
        ClearEvidence();
        return false;
    }
    if (have_frame_) {
        const int32_t delta = static_cast<int32_t>(frame_end_ms - last_frame_ms_);
        if (delta <= 0 ||
            delta > static_cast<int32_t>(duration_ms + kTimestampToleranceMs)) {
            ClearEvidence();
        }
    }
    have_frame_ = true;
    last_frame_ms_ = frame_end_ms;
    // WebRTC VAD also labels low-level residual playback as speech after AEC.
    // Require AC energy; DC offset must not turn a quiet frame into speech.
    const bool quiet = pcm_power < kMinimumSpeechPower;
    const uint64_t allowed_ratio =
        pcm_power >= kStrongSpeechPower ? kStrongSpeechEchoRatio : kEchoResidualRatio;
    const bool echo = !quiet && input_power != 0 &&
                      static_cast<uint64_t>(pcm_power) * allowed_ratio < input_power;
    if (vad_speech) {
        stats_.vad_speech_ms += duration_ms;
        if (quiet) stats_.quiet_rejected_ms += duration_ms;
        if (echo) stats_.echo_rejected_ms += duration_ms;
    }
    if (!vad_speech) {
        speech_ms_ = 0;
        ClearSpeechEvidence();
        silence_ms_ = std::min(kSilenceRearmMs, silence_ms_ + duration_ms);
        armed_ = silence_ms_ >= kSilenceRearmMs;
        return false;
    }
    silence_ms_ = 0;
    if (!armed_) {
        return false;
    }
    const bool passed = !quiet && !echo;
    const uint32_t prior_evidence_ms = RecentSpeechEvidenceMs(frame_end_ms);
    if (speech_ms_ == 0 || (passed && prior_evidence_ms == 0)) {
        onset_ms_ = frame_end_ms - duration_ms;
    }
    speech_ms_ += duration_ms;
    // Speech energy dips between syllables, and AEC/NS cut it further during playback,
    // so the energy gates need only part of the recent VAD run, not every frame.
    evidence_[evidence_index_] = {frame_end_ms, passed ? duration_ms : 0};
    evidence_index_ = (evidence_index_ + 1) % evidence_.size();
    if (speech_ms_ < kSpeechConfirmMs ||
        RecentSpeechEvidenceMs(frame_end_ms) < kSpeechEvidenceMs) {
        return false;
    }
    uint32_t earliest_window_pass_ms = frame_end_ms;
    for (const auto& entry : evidence_) {
        if (entry.evidence_ms == 0 ||
            static_cast<int32_t>(frame_end_ms - entry.frame_end_ms) >=
                static_cast<int32_t>(kSpeechConfirmMs)) {
            continue;
        }
        const uint32_t pass_start_ms = entry.frame_end_ms - entry.evidence_ms;
        if (static_cast<int32_t>(earliest_window_pass_ms - pass_start_ms) > 0) {
            earliest_window_pass_ms = pass_start_ms;
        }
    }
    if (static_cast<int32_t>(earliest_window_pass_ms - onset_ms_) > 0) {
        onset_ms_ = earliest_window_pass_ms;
    }
    fired_ = true;
    onset_ms = onset_ms_;
    return true;
}

}  // namespace rodakos

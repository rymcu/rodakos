#pragma once

#include "phone_os/voice_recorder_service.h"

#include <cstddef>
#include <deque>

namespace rodakos {

class VoicePcmAssembler {
public:
    void Reset();
    void InvalidateContinuity();
    void Append(const int16_t* samples, size_t count, const VoiceRecorderConfig& config,
                int64_t fetched_at_us, bool vad_valid, bool vad_speech,
                std::deque<VoicePcmFrame>& frames, size_t queue_limit);

private:
    std::vector<int16_t> samples_;
    size_t vad_valid_samples_ = 0;
    size_t vad_speech_samples_ = 0;
    int64_t clock_start_us_ = 0;
    uint64_t clock_samples_ = 0;
    bool clock_initialized_ = false;
    bool discontinuity_ = false;
};

}  // namespace rodakos

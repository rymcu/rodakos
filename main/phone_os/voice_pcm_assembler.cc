#include "phone_os/voice_pcm_assembler.h"

#include <algorithm>
#include <utility>

namespace rodakos {
namespace {
// Exceeds the ~154 ms combined I2S DMA + AFE buffer while staying below the 300 ms
// assistant VAD freshness limit, so cumulative sub-100 ms RX drops recover automatically.
constexpr int64_t kMaxClockLagUs = 250 * 1000;
}

void VoicePcmAssembler::Reset() {
    samples_.clear();
    input_power_ = 0;
    last_timestamp_ms_ = 0;
    have_emitted_frame_ = false;
    InvalidateContinuity();
    discontinuity_ = false;
}

void VoicePcmAssembler::InvalidateContinuity() {
    // A missing fetch does not invalidate PCM from earlier successful fetches.
    vad_valid_samples_ = 0;
    vad_speech_samples_ = 0;
    clock_initialized_ = false;
    clock_samples_ = samples_.size();
    discontinuity_ = true;
}

void VoicePcmAssembler::Append(const int16_t* samples, size_t count,
                               const VoiceRecorderConfig& config, int64_t fetched_at_us,
                               bool vad_valid, bool vad_speech,
                               std::deque<VoicePcmFrame>& frames, size_t queue_limit,
                               uint32_t input_power) {
    if (samples == nullptr || count == 0 || config.sample_rate <= 0 ||
        config.frame_duration_ms <= 0 || queue_limit == 0) return;
    const size_t frame_samples = static_cast<size_t>(
        config.sample_rate * config.frame_duration_ms / 1000);
    if (frame_samples == 0) return;
    if (!clock_initialized_) {
        // Include the retained partial frame so resumed timestamps do not run ahead.
        clock_start_us_ = fetched_at_us -
            static_cast<int64_t>(count + samples_.size()) * 1000000 / config.sample_rate;
        clock_initialized_ = true;
    } else {
        // The first fetch after a start can return late, which put every later timestamp
        // about one frame ahead of real time and failed the barge-in freshness check.
        // A sample cannot be fetched before it was captured, so pull the anchor back
        // whenever a fetch proves the clock is ahead.
        const int64_t bound_us = fetched_at_us -
            static_cast<int64_t>(clock_samples_ + count) * 1000000 / config.sample_rate;
        if (bound_us - clock_start_us_ > kMaxClockLagUs) {
            vad_valid_samples_ = 0;
            vad_speech_samples_ = 0;
            clock_samples_ = samples_.size();
            clock_start_us_ = fetched_at_us -
                static_cast<int64_t>(count + samples_.size()) * 1000000 / config.sample_rate;
            discontinuity_ = true;
        } else {
            clock_start_us_ = std::min(clock_start_us_, bound_us);
        }
    }
    size_t offset = 0;
    while (offset < count) {
        const size_t take = std::min(frame_samples - samples_.size(), count - offset);
        samples_.insert(samples_.end(), samples + offset, samples + offset + take);
        vad_valid_samples_ += vad_valid ? take : 0;
        vad_speech_samples_ += vad_valid && vad_speech ? take : 0;
        input_power_ = std::max(input_power_, input_power);
        offset += take;
        clock_samples_ += take;
        if (samples_.size() < frame_samples) continue;

        VoicePcmFrame frame;
        frame.config = config;
        uint32_t timestamp_ms = static_cast<uint32_t>((clock_start_us_ +
            static_cast<int64_t>(clock_samples_) * 1000000 / config.sample_rate) / 1000);
        if (!discontinuity_ && have_emitted_frame_ &&
            static_cast<int32_t>(timestamp_ms - last_timestamp_ms_) <= 0) {
            timestamp_ms = last_timestamp_ms_ + 1;
        }
        frame.timestamp_ms = timestamp_ms;
        last_timestamp_ms_ = timestamp_ms;
        have_emitted_frame_ = true;
        frame.samples = samples_;
        // Fetch and upload boundaries differ; vote using their overlapping samples.
        frame.vad_valid = !discontinuity_ && vad_valid_samples_ == frame_samples;
        frame.vad_speech = frame.vad_valid && vad_speech_samples_ * 2 >= frame_samples;
        frame.input_power = input_power_;
        input_power_ = 0;
        discontinuity_ = false;
        samples_.clear();
        vad_valid_samples_ = 0;
        vad_speech_samples_ = 0;
        if (frames.size() >= queue_limit) frames.pop_front();
        frames.push_back(std::move(frame));
    }
}

}  // namespace rodakos

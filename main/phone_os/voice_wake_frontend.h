#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rodakos {

// Echo cancellation and gain in front of the wake recognizer. The conversation path has
// its own AFE; this one runs synchronously on the capture task between microphone reads,
// so it adds no task, ring buffer or AFE output contract.
class VoiceWakeFrontend {
public:
    struct Stats {
        uint32_t aec_frames = 0;
        // Frames passed through because the loopback carried no playback.
        uint32_t bypassed_frames = 0;
        uint32_t agc_blocks = 0;
        uint32_t failures = 0;
        uint64_t reference_energy = 0;
        // Mean-square energy into and out of AEC, for echo return loss enhancement.
        uint64_t aec_in_energy = 0;
        uint64_t aec_out_energy = 0;
        // Wall time spent in Process(), including AEC and AGC.
        uint64_t process_us = 0;
        uint64_t processed_samples = 0;
    };

    VoiceWakeFrontend() = default;
    ~VoiceWakeFrontend();
    VoiceWakeFrontend(const VoiceWakeFrontend&) = delete;
    VoiceWakeFrontend& operator=(const VoiceWakeFrontend&) = delete;

    // Either stage may be disabled. A stage that fails to create is skipped, not fatal.
    bool Start(bool aec, bool agc);
    void Stop();
    bool IsActive() const { return aec_ != nullptr || agc_ != nullptr; }
    bool aec_active() const { return aec_ != nullptr; }
    bool agc_active() const { return agc_ != nullptr; }

    // Takes `count` microphone samples and the matching loopback samples, each read with its
    // own stride, and emits processed microphone samples into `output`. Samples are delayed
    // by less than one AEC frame plus one AGC block, so output length can differ from input.
    void Process(const int16_t* microphone, size_t microphone_stride, const int16_t* reference,
                 size_t reference_stride, size_t count, std::vector<int16_t>& output);
    void Reset();
    Stats stats() const { return stats_; }

private:
    void ApplyGain(int16_t* samples, size_t count);

    // ESP-SR declares afe_aec_handle_t as an anonymous-struct typedef, which cannot be
    // forward-declared; the .cc owns the real type.
    void* aec_ = nullptr;
    void* agc_ = nullptr;
    size_t aec_frame_ = 0;
    size_t aec_hangover_samples_ = 0;
    std::vector<int16_t> pending_;
    int16_t* aec_output_ = nullptr;
    std::vector<int16_t> agc_pending_;
    Stats stats_;
};

}  // namespace rodakos

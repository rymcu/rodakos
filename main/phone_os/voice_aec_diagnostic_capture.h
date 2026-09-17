#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rodakos {

// The frontend mutex serializes control and short in-memory copies; no I/O runs here.
class VoiceAecDiagnosticCapture {
public:
    enum class State { kEmpty, kArmed, kCapturing, kStopped, kComplete };
    struct Status {
        State state = State::kEmpty;
        size_t capacity_samples = 0;
        size_t raw_samples = 0;
        size_t afe_samples = 0;
        uint32_t generation = 0;
        int64_t raw_first_sample_us = 0;
        int64_t afe_first_sample_us = 0;
        uint32_t raw_discontinuities = 0;
        uint32_t afe_discontinuities = 0;
        int initial_mic_slot = -1;
        int final_mic_slot = -1;
        uint32_t mic_switches = 0;
    };
    static constexpr size_t kMaxChunkSamples = 256;
    ~VoiceAecDiagnosticCapture();
    VoiceAecDiagnosticCapture() = default;
    VoiceAecDiagnosticCapture(const VoiceAecDiagnosticCapture&) = delete;
    VoiceAecDiagnosticCapture& operator=(const VoiceAecDiagnosticCapture&) = delete;
    bool Arm(uint32_t duration_ms);
    void Begin(uint32_t generation);
    void Stop();
    bool Clear();
    Status GetStatus() const { return status_; }
    bool ReadChunk(uint8_t channel, size_t offset, size_t count, std::string& hex) const;
    void AppendRaw(const int16_t* samples, size_t frames, uint32_t generation,
                   int64_t observed_us, int mic_slot);
    void AppendAfe(const int16_t* samples, size_t count, uint32_t generation,
                   int64_t observed_us);
    void MarkDiscontinuity(bool raw, uint32_t generation);
private:
    void CheckComplete();
    Status status_;
    int16_t* raw_ = nullptr;
    int16_t* afe_ = nullptr;
};

}  // namespace rodakos

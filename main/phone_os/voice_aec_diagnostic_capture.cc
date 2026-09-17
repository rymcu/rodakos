#include "phone_os/voice_aec_diagnostic_capture.h"

#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>

namespace rodakos {

VoiceAecDiagnosticCapture::~VoiceAecDiagnosticCapture() {
    heap_caps_free(raw_);
    heap_caps_free(afe_);
}

bool VoiceAecDiagnosticCapture::Arm(uint32_t duration_ms) {
    if (duration_ms == 0 || duration_ms > 6000 || status_.state == State::kArmed ||
        status_.state == State::kCapturing) return false;
    const size_t capacity = static_cast<size_t>(duration_ms) * 16;
    // Reuse an equal-size allocation without requiring a second PSRAM-sized buffer.
    if (capacity != status_.capacity_samples || raw_ == nullptr || afe_ == nullptr) {
        Clear();
        raw_ = static_cast<int16_t*>(heap_caps_malloc(capacity * 4 * sizeof(int16_t),
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        afe_ = static_cast<int16_t*>(heap_caps_malloc(capacity * sizeof(int16_t),
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (raw_ == nullptr || afe_ == nullptr) { Clear(); return false; }
    }
    status_ = {};
    status_.capacity_samples = capacity;
    status_.state = State::kArmed;
    return true;
}

void VoiceAecDiagnosticCapture::Begin(uint32_t generation) {
    if (status_.state != State::kArmed) return;
    status_.generation = generation;
    status_.state = State::kCapturing;
}

void VoiceAecDiagnosticCapture::Stop() {
    if (status_.state == State::kArmed || status_.state == State::kCapturing)
        status_.state = State::kStopped;
}

bool VoiceAecDiagnosticCapture::Clear() {
    if (status_.state == State::kCapturing || status_.state == State::kArmed) return false;
    heap_caps_free(raw_);
    heap_caps_free(afe_);
    raw_ = afe_ = nullptr;
    status_ = {};
    return true;
}

void VoiceAecDiagnosticCapture::CheckComplete() {
    if (status_.raw_samples == status_.capacity_samples &&
        status_.afe_samples == status_.capacity_samples) status_.state = State::kComplete;
}

void VoiceAecDiagnosticCapture::AppendRaw(const int16_t* samples, size_t frames,
                                        uint32_t generation, int64_t observed_us, int mic_slot) {
    if (status_.state != State::kCapturing || generation != status_.generation ||
        samples == nullptr || frames == 0) return;
    const size_t count = std::min(frames, status_.capacity_samples - status_.raw_samples);
    if (count == 0) return;
    if (status_.raw_samples == 0) {
        // Software block arrival estimate, not an ADC clock or AFE alignment guarantee.
        status_.raw_first_sample_us = observed_us - static_cast<int64_t>(frames) * 1000000 / 16000;
        status_.initial_mic_slot = mic_slot;
    } else if (status_.final_mic_slot != mic_slot) ++status_.mic_switches;
    status_.final_mic_slot = mic_slot;
    std::memcpy(raw_ + status_.raw_samples * 4, samples, count * 4 * sizeof(int16_t));
    status_.raw_samples += count;
    CheckComplete();
}

void VoiceAecDiagnosticCapture::AppendAfe(const int16_t* samples, size_t count,
                                        uint32_t generation, int64_t observed_us) {
    if (status_.state != State::kCapturing || generation != status_.generation ||
        samples == nullptr || count == 0) return;
    const size_t copied = std::min(count, status_.capacity_samples - status_.afe_samples);
    if (copied == 0) return;
    if (status_.afe_samples == 0)
        status_.afe_first_sample_us = observed_us - static_cast<int64_t>(count) * 1000000 / 16000;
    std::memcpy(afe_ + status_.afe_samples, samples, copied * sizeof(int16_t));
    status_.afe_samples += copied;
    CheckComplete();
}

void VoiceAecDiagnosticCapture::MarkDiscontinuity(bool raw, uint32_t generation) {
    if (status_.state != State::kCapturing || generation != status_.generation) return;
    if (raw) ++status_.raw_discontinuities;
    else ++status_.afe_discontinuities;
}

bool VoiceAecDiagnosticCapture::ReadChunk(uint8_t channel, size_t offset, size_t count,
                                        std::string& hex) const {
    if (channel > 4 || count == 0 || count > kMaxChunkSamples ||
        (status_.state != State::kStopped && status_.state != State::kComplete)) return false;
    const size_t available = channel == 4 ? status_.afe_samples : status_.raw_samples;
    if (offset > available || count > available - offset) return false;
    static constexpr char digits[] = "0123456789abcdef";
    hex.resize(count * 4);
    for (size_t i = 0; i < count; ++i) {
        const uint16_t value = static_cast<uint16_t>(channel == 4 ? afe_[offset + i]
                                                      : raw_[(offset + i) * 4 + channel]);
        hex[i * 4] = digits[(value >> 4) & 15];
        hex[i * 4 + 1] = digits[value & 15];
        hex[i * 4 + 2] = digits[(value >> 12) & 15];
        hex[i * 4 + 3] = digits[(value >> 8) & 15];
    }
    return true;
}

}  // namespace rodakos

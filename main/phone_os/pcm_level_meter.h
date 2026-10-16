#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace rodakos {

// Measures decoded PCM before the DAC volume stage, so a quiet source can be told apart from a low
// device volume setting. Levels are dBFS relative to int16 full scale.
class PcmLevelMeter {
public:
    static constexpr float kSilenceDbfs = -96.0f;

    void Reset() {
        peak_ = 0;
        sum_squares_ = 0;
        samples_ = 0;
    }

    void Add(const int16_t* samples, size_t count) {
        if (samples == nullptr) return;
        for (size_t i = 0; i < count; ++i) {
            const int32_t value = samples[i];
            peak_ = std::max(peak_, std::abs(value));
            sum_squares_ += static_cast<uint64_t>(value * value);
        }
        samples_ += count;
    }

    uint64_t samples() const { return samples_; }
    int32_t peak() const { return peak_; }

    float PeakDbfs() const {
        return peak_ > 0 ? std::max(kSilenceDbfs, 20.0f * std::log10(peak_ / kFullScale)) : kSilenceDbfs;
    }

    float RmsDbfs() const {
        if (samples_ == 0 || sum_squares_ == 0) return kSilenceDbfs;
        const double mean_square = static_cast<double>(sum_squares_) / static_cast<double>(samples_);
        const double dbfs = 10.0 * std::log10(mean_square / (kFullScale * kFullScale));
        return std::max(kSilenceDbfs, static_cast<float>(dbfs));
    }

private:
    static constexpr float kFullScale = 32768.0f;

    int32_t peak_ = 0;
    uint64_t sum_squares_ = 0;
    uint64_t samples_ = 0;
};

}  // namespace rodakos

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace rodakos {

class VoiceInputPowerHistory {
public:
    void Record(uint32_t power, int64_t read_end_us) {
        reads_[next_] = {read_end_us, power};
        next_ = (next_ + 1) % reads_.size();
    }

    uint32_t Peak(int64_t fetch_end_us) const {
        uint32_t peak = 0;
        for (const auto& read : reads_) {
            const int64_t age_us = fetch_end_us - read.end_us;
            if (age_us >= 0 && age_us < kHistoryUs) peak = std::max(peak, read.power);
        }
        return peak;
    }

private:
    struct Read {
        int64_t end_us = 0;
        uint32_t power = 0;
    };
    // The measured AFE envelope trails raw input by ~140 ms. Cover that delay and a
    // 32 ms fetch block; extra slots tolerate capture bursts without extending the time window.
    static constexpr int64_t kHistoryUs = 200000;
    std::array<Read, 12> reads_{};
    size_t next_ = 0;
};

}  // namespace rodakos

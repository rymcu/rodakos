#pragma once

#include <cstdint>
#include <vector>

namespace fake_opus_decoder {
inline int last_sample_rate = 0;
inline int last_duration_ms = 0;
}  // namespace fake_opus_decoder

// Host decoder: each payload byte becomes a 60 ms (960 sample) frame filled with that
// byte's value, so tests can trace which TTS frame reached the codec. An empty payload
// still fails, as a real decode error would.
class OpusDecoderWrapper {
public:
    OpusDecoderWrapper(int sample_rate, int, int duration_ms) {
        fake_opus_decoder::last_sample_rate = sample_rate;
        fake_opus_decoder::last_duration_ms = duration_ms;
    }
    bool Decode(std::vector<uint8_t>&& payload, std::vector<int16_t>& pcm) {
        if (payload.empty()) return false;
        pcm.clear();
        for (const uint8_t marker : payload) pcm.insert(pcm.end(), 960, static_cast<int16_t>(marker));
        return true;
    }
};

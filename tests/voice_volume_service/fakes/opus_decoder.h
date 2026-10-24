#pragma once

#include <cstdint>
#include <vector>

// Host decoder: each payload byte becomes a 60 ms (960 sample) frame filled with that
// byte's value, so tests can trace which TTS frame reached the codec. An empty payload
// still fails, as a real decode error would.
class OpusDecoderWrapper {
public:
    OpusDecoderWrapper(int, int, int) {}
    bool Decode(std::vector<uint8_t>&& payload, std::vector<int16_t>& pcm) {
        if (payload.empty()) return false;
        pcm.clear();
        for (const uint8_t marker : payload) pcm.insert(pcm.end(), 960, static_cast<int16_t>(marker));
        return true;
    }
};

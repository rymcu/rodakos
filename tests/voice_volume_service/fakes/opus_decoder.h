#pragma once

#include <cstdint>
#include <vector>

class OpusDecoderWrapper {
public:
    OpusDecoderWrapper(int, int, int) {}
    bool Decode(std::vector<uint8_t>&&, std::vector<int16_t>&) { return false; }
};

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

class OpusEncoderWrapper {
public:
    OpusEncoderWrapper(int, int, int) {}
    void SetComplexity(int) {}
    void Encode(std::vector<int16_t>&&,
                const std::function<void(std::vector<uint8_t>&&)>&) {}
};

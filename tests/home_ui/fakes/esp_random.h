#pragma once

#include <cstdint>

inline uint32_t esp_random() {
    static uint32_t value = 0x10203040U;
    value = value * 1664525U + 1013904223U;
    return value;
}

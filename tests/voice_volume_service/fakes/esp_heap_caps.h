#pragma once

#include <cstddef>
#include <cstdint>

constexpr uint32_t MALLOC_CAP_SPIRAM = 1 << 10;
constexpr uint32_t MALLOC_CAP_8BIT = 1 << 2;
constexpr uint32_t MALLOC_CAP_INTERNAL = 1 << 11;
inline size_t heap_caps_get_free_size(uint32_t) { return 1024 * 1024; }
inline size_t heap_caps_get_minimum_free_size(uint32_t) { return 1024 * 1024; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 1024 * 1024; }

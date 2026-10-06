#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
constexpr uint32_t MALLOC_CAP_SPIRAM = 1;
constexpr uint32_t MALLOC_CAP_8BIT = 2;
constexpr uint32_t MALLOC_CAP_INTERNAL = 4;
inline size_t heap_caps_get_free_size(uint32_t) { return 1024 * 1024; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 1024 * 1024; }
inline void* heap_caps_malloc(size_t size, uint32_t) { return std::malloc(size); }
inline void heap_caps_free(void* value) { std::free(value); }

#pragma once
#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include <cstdlib>
constexpr uint32_t MALLOC_CAP_SPIRAM = 4;
constexpr uint32_t MALLOC_CAP_8BIT = 2;
constexpr uint32_t MALLOC_CAP_INTERNAL = 1;
inline size_t heap_caps_get_free_size(uint32_t) { return 1024 * 1024; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 1024 * 1024; }
inline void* heap_caps_malloc(size_t size, uint32_t) { return std::malloc(size); }
inline void* heap_caps_aligned_calloc(size_t alignment, size_t n, size_t size, uint32_t) {
    const size_t bytes = (n * size + alignment - 1) / alignment * alignment;
    void* value = std::aligned_alloc(alignment, bytes);
    if (value != nullptr) __builtin_memset(value, 0, bytes);
    return value;
}
inline void heap_caps_free(void* value) { std::free(value); }
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#endif

#pragma once
#include "../host_runtime.h"
#include <cstdlib>
#include <cstdint>
constexpr uint32_t MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_INTERNAL = 4, MALLOC_CAP_DMA = 8;
inline void* heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t) {
    return camera_host::AllocateAligned(alignment, size);
}
inline void heap_caps_free(void* pointer) { camera_host::FreeAligned(pointer); }
inline size_t heap_caps_get_free_size(uint32_t) { return 1024 * 1024; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 1024 * 1024; }

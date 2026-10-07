#pragma once

#include <cstddef>
#include <cstdlib>

#define MALLOC_CAP_INTERNAL 0x01
#define MALLOC_CAP_8BIT 0x02
#define MALLOC_CAP_SPIRAM 0x04
#define MALLOC_CAP_DMA 0x08

inline void* heap_caps_aligned_alloc(size_t alignment, size_t size, unsigned) {
    void* pointer = nullptr;
    return posix_memalign(&pointer, alignment, size) == 0 ? pointer : nullptr;
}

inline void heap_caps_free(void* pointer) {
    std::free(pointer);
}

inline size_t heap_caps_get_free_size(unsigned) {
    return 1024U * 1024U;
}

inline size_t heap_caps_get_largest_free_block(unsigned) {
    return 512U * 1024U;
}

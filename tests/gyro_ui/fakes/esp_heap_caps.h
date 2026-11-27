#pragma once
// Gyro test: the shared home_ui heap fake plus heap_caps_malloc, with a switch to fail it.
#include <cstddef>
#include <cstdlib>

#define MALLOC_CAP_INTERNAL 0x01
#define MALLOC_CAP_8BIT 0x02
#define MALLOC_CAP_SPIRAM 0x04
#define MALLOC_CAP_DMA 0x08

namespace gyro_test {
inline bool fail_malloc = false;
}

inline void* heap_caps_malloc(size_t size, unsigned) {
    return gyro_test::fail_malloc ? nullptr : std::malloc(size);
}
inline void* heap_caps_calloc(size_t n, size_t size, unsigned) { return std::calloc(n, size); }
inline void heap_caps_free(void* pointer) { std::free(pointer); }
inline size_t heap_caps_get_free_size(unsigned) { return 4 * 1024 * 1024; }
inline size_t heap_caps_get_largest_free_block(unsigned) { return 2 * 1024 * 1024; }

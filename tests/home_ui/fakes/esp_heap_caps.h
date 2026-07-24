#pragma once

#include <cstddef>

#define MALLOC_CAP_INTERNAL 0x01
#define MALLOC_CAP_8BIT 0x02

inline size_t heap_caps_get_free_size(unsigned) {
    return 1024U * 1024U;
}

inline size_t heap_caps_get_largest_free_block(unsigned) {
    return 512U * 1024U;
}

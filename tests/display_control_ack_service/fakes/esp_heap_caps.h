#pragma once
#include <cstddef>
constexpr unsigned MALLOC_CAP_SPIRAM = 1;
constexpr unsigned MALLOC_CAP_INTERNAL = 2;
constexpr unsigned MALLOC_CAP_8BIT = 4;
inline size_t heap_caps_get_free_size(unsigned) { return 1024 * 1024; }
inline size_t heap_caps_get_largest_free_block(unsigned) { return 1024 * 1024; }

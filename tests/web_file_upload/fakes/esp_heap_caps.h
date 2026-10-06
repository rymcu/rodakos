#pragma once
#include <cstdlib>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_SPIRAM 2
#define MALLOC_CAP_8BIT 4
inline void* heap_caps_malloc(size_t size, unsigned) { return std::malloc(size); }
inline void heap_caps_free(void* p) { std::free(p); }

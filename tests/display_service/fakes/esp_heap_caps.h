#pragma once
#include <cstddef>
constexpr unsigned MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_SPIRAM = 4,
                   MALLOC_CAP_DMA = 8;
void* heap_caps_aligned_alloc(size_t alignment, size_t bytes, unsigned caps);
extern "C" void* heap_caps_calloc(size_t n, size_t size, unsigned caps);
extern "C" void* heap_caps_aligned_calloc(size_t alignment, size_t n, size_t size, unsigned caps);
void heap_caps_free(void* pointer);
size_t heap_caps_get_free_size(unsigned);
size_t heap_caps_get_largest_free_block(unsigned);

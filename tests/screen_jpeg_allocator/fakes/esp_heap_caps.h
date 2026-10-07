#pragma once
#include <cstddef>
#define MALLOC_CAP_8BIT (1U << 2)
#define MALLOC_CAP_SPIRAM (1U << 10)
#define MALLOC_CAP_INTERNAL (1U << 11)
extern "C" void* heap_caps_calloc(size_t nmemb, size_t size, unsigned caps);
extern "C" void* heap_caps_aligned_calloc(size_t alignment, size_t nmemb, size_t size, unsigned caps);
extern "C" void heap_caps_free(void* pointer);

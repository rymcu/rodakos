#pragma once
#include <stddef.h>
#define MALLOC_CAP_INTERNAL 1U
#define MALLOC_CAP_8BIT 2U
#define MALLOC_CAP_SPIRAM 4U
#ifdef __cplusplus
extern "C" {
#endif
void* heap_caps_malloc(size_t, unsigned);
void* heap_caps_calloc(size_t, size_t, unsigned);
void heap_caps_free(void*);
#ifdef __cplusplus
}
#endif

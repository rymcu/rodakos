#include "host_heap.h"
#include <esp_heap_caps.h>

namespace {
constexpr unsigned kExternal = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr unsigned kInternal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
void* Prefer(size_t n, size_t size, unsigned first, unsigned second) {
    auto* pointer = heap_caps_calloc(n, size, first);
    if (pointer == nullptr && size != 0) pointer = heap_caps_calloc(n, size, second);
    return pointer;
}
void* AlignPrefer(size_t size, int alignment, unsigned first, unsigned second) {
    auto* pointer = heap_caps_aligned_calloc(static_cast<size_t>(alignment), 1, size, first);
    if (pointer == nullptr)
        pointer = heap_caps_aligned_calloc(static_cast<size_t>(alignment), 1, size, second);
    return pointer;
}
}

extern "C" {
void* jpeg_calloc(size_t n, size_t size) {
    NoteRealCall(0, n, size);
    return Prefer(n, size, kExternal, kInternal);
}
void* jpeg_calloc_inner(size_t size) {
    NoteRealCall(1, 1, size);
    return Prefer(1, size, kInternal, kExternal);
}
void* jpeg_calloc_align(size_t size, int alignment) {
    NoteRealCall(2, 1, size, alignment);
    return AlignPrefer(size, alignment, kExternal, kInternal);
}
void* jpeg_calloc_align_inner(size_t size, int alignment) {
    NoteRealCall(3, 1, size, alignment);
    return AlignPrefer(size, alignment, kInternal, kExternal);
}
void jpeg_free(void* pointer) { heap_caps_free(pointer); }
void jpeg_free_align(void* pointer) { heap_caps_free(pointer); }
}

#pragma once
#include <array>
#include <cstddef>

struct HeapStats {
    size_t external_calls = 0;
    size_t internal_calls = 0;
    size_t live = 0;
    std::array<size_t, 4> real_calls{};
    std::array<size_t, 4> real_n{};
    std::array<size_t, 4> real_size{};
    std::array<int, 4> real_alignment{};
};
void ResetHeap();
void RejectExternal(bool value);
void FailExternalAt(size_t call);
HeapStats GetHeapStats();
unsigned AllocationCaps(void* pointer);
void NoteRealCall(size_t index, size_t n, size_t size, int alignment = 0);

extern "C" {
void* jpeg_calloc(size_t nmemb, size_t size);
void* jpeg_calloc_inner(size_t size);
void* jpeg_calloc_align(size_t size, int alignment);
void* jpeg_calloc_align_inner(size_t size, int alignment);
void jpeg_free(void* pointer);
void jpeg_free_align(void* pointer);
}

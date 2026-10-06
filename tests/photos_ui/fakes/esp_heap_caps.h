#pragma once
#include <cstddef>
#include <cstdlib>
#include <set>
#define MALLOC_CAP_INTERNAL 0x01
#define MALLOC_CAP_8BIT 0x02
#define MALLOC_CAP_SPIRAM 0x04
namespace photo_test {
inline std::set<void*> image_buffers;
inline int fail_allocations = 0;
}
inline void* heap_caps_malloc(size_t size, unsigned) {
    if (photo_test::fail_allocations > 0) { --photo_test::fail_allocations; return nullptr; }
    auto* pointer = std::malloc(size);
    if (pointer != nullptr) photo_test::image_buffers.insert(pointer);
    return pointer;
}
inline void heap_caps_free(void* pointer) {
    photo_test::image_buffers.erase(pointer);
    std::free(pointer);
}
inline void* heap_caps_aligned_alloc(size_t alignment, size_t size, unsigned) {
    void* pointer = nullptr;
    return posix_memalign(&pointer, alignment, size) == 0 ? pointer : nullptr;
}
inline size_t heap_caps_get_free_size(unsigned) { return 1024 * 1024; }
inline size_t heap_caps_get_largest_free_block(unsigned) { return 512 * 1024; }

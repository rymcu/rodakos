#pragma once
#include <cstdlib>
#include <cstdint>

constexpr uint32_t MALLOC_CAP_SPIRAM = 1;
constexpr uint32_t MALLOC_CAP_8BIT = 2;
inline int fake_heap_allocations_until_failure = -1;
inline void* heap_caps_malloc(size_t bytes, uint32_t) {
    if (fake_heap_allocations_until_failure == 0) return nullptr;
    if (fake_heap_allocations_until_failure > 0) --fake_heap_allocations_until_failure;
    return std::malloc(bytes);
}
inline void heap_caps_free(void* pointer) { std::free(pointer); }

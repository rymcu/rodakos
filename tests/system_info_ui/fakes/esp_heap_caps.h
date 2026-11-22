#pragma once
#include <cstddef>
#define MALLOC_CAP_INTERNAL 0x01
#define MALLOC_CAP_8BIT 0x02
#define MALLOC_CAP_SPIRAM 0x04
// Values the test sets per scenario; internal and PSRAM are looked up by the SPIRAM bit.
struct FakeHeap {
    size_t internal_free = 142U * 1024U;
    size_t internal_total = 313U * 1024U;
    size_t psram_free = 6502U * 1024U;
    size_t psram_total = 8192U * 1024U - 300U * 1024U;
};
inline FakeHeap& fake_heap() { static FakeHeap heap; return heap; }
inline size_t heap_caps_get_free_size(unsigned caps) {
    return caps & MALLOC_CAP_SPIRAM ? fake_heap().psram_free : fake_heap().internal_free;
}
inline size_t heap_caps_get_total_size(unsigned caps) {
    return caps & MALLOC_CAP_SPIRAM ? fake_heap().psram_total : fake_heap().internal_total;
}
inline size_t heap_caps_get_largest_free_block(unsigned caps) {
    return heap_caps_get_free_size(caps) / 2;
}

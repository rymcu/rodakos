#include "host_heap.h"
#include <esp_heap_caps.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace {
struct Allocation { void* pointer = nullptr; unsigned caps = 0; };
std::array<Allocation, 128> allocations;
std::mutex mutex;
HeapStats stats;
bool reject_external = false;
size_t fail_external_at = 0;
}

void ResetHeap() {
    std::lock_guard<std::mutex> lock(mutex);
    if (stats.live != 0) throw std::runtime_error("live allocations between tests");
    stats = {};
    reject_external = false;
    fail_external_at = 0;
}
void RejectExternal(bool value) { std::lock_guard<std::mutex> lock(mutex); reject_external = value; }
void FailExternalAt(size_t call) { std::lock_guard<std::mutex> lock(mutex); fail_external_at = call; }
HeapStats GetHeapStats() { std::lock_guard<std::mutex> lock(mutex); return stats; }
void NoteRealCall(size_t index, size_t n, size_t size, int alignment) {
    std::lock_guard<std::mutex> lock(mutex);
    ++stats.real_calls.at(index);
    stats.real_n.at(index) = n;
    stats.real_size.at(index) = size;
    stats.real_alignment.at(index) = alignment;
}
unsigned AllocationCaps(void* pointer) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& item : allocations) if (item.pointer == pointer) return item.caps;
    return 0;
}

extern "C" void* heap_caps_aligned_calloc(size_t alignment, size_t n, size_t size, unsigned caps) {
    std::lock_guard<std::mutex> lock(mutex);
    const bool external = (caps & MALLOC_CAP_SPIRAM) != 0;
    if (external) ++stats.external_calls;
    else ++stats.internal_calls;
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        (n != 0 && size > std::numeric_limits<size_t>::max() / n) || n == 0 || size == 0)
        return nullptr;
    if (external && (reject_external || (fail_external_at != 0 && stats.external_calls == fail_external_at)))
        return nullptr;
    void* pointer = nullptr;
    const size_t host_alignment = alignment < sizeof(void*) ? sizeof(void*) : alignment;
    if (posix_memalign(&pointer, host_alignment, n * size) != 0) return nullptr;
    std::memset(pointer, 0, n * size);
    for (auto& item : allocations) {
        if (item.pointer == nullptr) {
            item = {pointer, caps};
            ++stats.live;
            return pointer;
        }
    }
    std::free(pointer);
    return nullptr;
}
extern "C" void* heap_caps_calloc(size_t n, size_t size, unsigned caps) {
    return heap_caps_aligned_calloc(alignof(std::max_align_t), n, size, caps);
}
extern "C" void heap_caps_free(void* pointer) {
    if (pointer == nullptr) return;
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& item : allocations) {
        if (item.pointer == pointer) {
            item = {};
            --stats.live;
            std::free(pointer);
            return;
        }
    }
    std::abort();
}

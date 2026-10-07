#include "screen_jpeg_allocation.h"

#include <cstddef>
#include <esp_heap_caps.h>
#include <sdkconfig.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3) || !CONFIG_IDF_TARGET_ESP32S3
#error "The reviewed JPEG allocator ABI is restricted to ESP32-S3"
#endif
#if defined(CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS) && CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS
#error "Screen JPEG OOM recovery requires heap allocation failure to return null"
#endif

namespace {
// Trivial constant-initialized GCC TLS uses the IDF Xtensa task-stack TLS area.
// Never replace it with a process-wide flag or a dynamically initialized object.
thread_local bool g_screen_jpeg_scope_active = false;
constexpr unsigned kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
}

namespace rodakos {
ScreenJpegAllocationScope::ScreenJpegAllocationScope() noexcept
    : previous_(g_screen_jpeg_scope_active) {
    g_screen_jpeg_scope_active = true;
}

ScreenJpegAllocationScope::~ScreenJpegAllocationScope() noexcept {
    g_screen_jpeg_scope_active = previous_;
}
}  // namespace rodakos

// Private declarations are pinned by check_screen_jpeg_allocator.py to the reviewed 0.6.1 archive,
// DWARF prototypes, allocation implementations and call-site relocations.
extern "C" {
void* __real_jpeg_calloc(size_t nmemb, size_t size);
void* __real_jpeg_calloc_inner(size_t size);
void* __real_jpeg_calloc_align(size_t size, int aligned);
void* __real_jpeg_calloc_align_inner(size_t size, int aligned);

void* __wrap_jpeg_calloc(size_t nmemb, size_t size) {
    if (!g_screen_jpeg_scope_active) return __real_jpeg_calloc(nmemb, size);
    return heap_caps_calloc(nmemb, size, kExternalCaps);
}

void* __wrap_jpeg_calloc_inner(size_t size) {
    if (!g_screen_jpeg_scope_active) return __real_jpeg_calloc_inner(size);
    return heap_caps_calloc(1, size, kExternalCaps);
}

void* __wrap_jpeg_calloc_align(size_t size, int aligned) {
    if (!g_screen_jpeg_scope_active) return __real_jpeg_calloc_align(size, aligned);
    return heap_caps_aligned_calloc(static_cast<size_t>(aligned), 1, size, kExternalCaps);
}

void* __wrap_jpeg_calloc_align_inner(size_t size, int aligned) {
    if (!g_screen_jpeg_scope_active) return __real_jpeg_calloc_align_inner(size, aligned);
    return heap_caps_aligned_calloc(static_cast<size_t>(aligned), 1, size, kExternalCaps);
}
}

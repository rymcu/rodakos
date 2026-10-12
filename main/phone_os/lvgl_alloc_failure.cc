#include "phone_os/lvgl_alloc_failure.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <esp_attr.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Release-test arbitrary allocation failure: after arming with (skip, count), the
// LVGL task's malloc/calloc/realloc calls pass `skip` times, then fail `count` times.
// Only the task named "taskLVGL" is affected, so other subsystems keep their memory.
// The wrappers run inside the allocator path: they use atomics and ROM printf only.
extern "C" {
void* __real_heap_caps_malloc_default(size_t size);
void* __real_heap_caps_realloc_default(void* ptr, size_t size);
}

namespace rodakos {
namespace {
constexpr const char* kLvglTaskName = "taskLVGL";
std::atomic<uint32_t> g_skip{0};
std::atomic<uint32_t> g_remaining{0};
std::atomic<uint32_t> g_failed{0};
std::atomic<TaskHandle_t> g_lvgl_task{nullptr};

IRAM_ATTR bool IsLvglTask() {
    if (xPortInIsrContext()) return false;
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (current == nullptr) return false;
    TaskHandle_t cached = g_lvgl_task.load(std::memory_order_relaxed);
    if (cached != nullptr) return cached == current;
    const char* name = pcTaskGetName(current);
    if (name == nullptr || std::strcmp(name, kLvglTaskName) != 0) return false;
    g_lvgl_task.store(current, std::memory_order_relaxed);
    return true;
}

IRAM_ATTR bool ShouldFail(size_t size) {
    if (g_remaining.load(std::memory_order_relaxed) == 0 || size == 0 || !IsLvglTask()) return false;
    uint32_t skip = g_skip.load(std::memory_order_relaxed);
    while (skip > 0) {
        if (g_skip.compare_exchange_weak(skip, skip - 1, std::memory_order_relaxed)) return false;
    }
    uint32_t remaining = g_remaining.load(std::memory_order_relaxed);
    while (remaining > 0) {
        if (g_remaining.compare_exchange_weak(remaining, remaining - 1, std::memory_order_relaxed)) {
            const uint32_t index = g_failed.fetch_add(1, std::memory_order_relaxed) + 1;
            esp_rom_printf("RODAK_LVGL_ALLOC_FAIL index=%u size=%u\n", static_cast<unsigned>(index),
                           static_cast<unsigned>(size));
            return true;
        }
    }
    return false;
}
}  // namespace

void ArmLvglAllocationFailure(uint32_t skip, uint32_t count) {
    g_remaining.store(0, std::memory_order_relaxed);
    g_failed.store(0, std::memory_order_relaxed);
    g_skip.store(skip, std::memory_order_relaxed);
    g_remaining.store(count, std::memory_order_relaxed);
    std::fprintf(stdout, "RODAK_RELEASE_TEST_RESULT {\"lvgl_alloc_fail\":[%u,%u]}\n",
                 static_cast<unsigned>(skip), static_cast<unsigned>(count));
    std::fflush(stdout);
}

}  // namespace rodakos

extern "C" IRAM_ATTR void* __wrap_heap_caps_malloc_default(size_t size) {
    if (rodakos::ShouldFail(size)) return nullptr;
    return __real_heap_caps_malloc_default(size);
}

extern "C" IRAM_ATTR void* __wrap_heap_caps_realloc_default(void* ptr, size_t size) {
    // On failure realloc keeps the old block, so the caller still owns it.
    if (rodakos::ShouldFail(size)) return nullptr;
    return __real_heap_caps_realloc_default(ptr, size);
}

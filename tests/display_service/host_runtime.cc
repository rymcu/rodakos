#include "host_runtime.h"
#include "host_heap.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_enc.h"
#include "esp_lvgl_port.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "task_retirement_host.h"
#include "phone_os/task-retirement.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <thread>

struct HostSemaphore {
    std::mutex mutex;
    std::condition_variable condition;
    bool available = false;
};

namespace {
using Clock = std::chrono::steady_clock;
using rodakos_test::display_service_host::Resources;
struct Allocation { void* pointer = nullptr; size_t bytes = 0; unsigned caps = 0; };
struct HostEncoder { void* parts[3]; };
struct Async { void (*callback)(void*) = nullptr; void* data = nullptr; };
std::mutex host_mutex;
std::recursive_timed_mutex ui_mutex;
std::array<Allocation, 32> allocations;
std::array<Async, 16> async_calls;
std::array<int64_t, 128> new_failure_times{}, process_times{};
size_t new_time_count = 0, process_time_count = 0;
Resources resources;
std::atomic<size_t> new_size{0};
size_t new_nth = 0, new_count = 0;
bool new_worker_only = false;
size_t heap_size = 0, heap_count = 0;
bool heap_after_open = false;
bool fail_open = false, fail_process = false, allow_async = true;
bool throw_process = false, reject_codec_external = false;
size_t encoded_size = 4096;
thread_local size_t ui_lock_depth = 0;

int64_t Now() {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
}

bool RejectNew(size_t bytes) {
    const size_t expected = new_size.load(std::memory_order_relaxed);
    if (expected == 0 || (expected != bytes && expected != SIZE_MAX)) return false;
    std::lock_guard<std::mutex> lock(host_mutex);
    if ((new_size.load() != bytes && new_size.load() != SIZE_MAX) || new_count == 0 ||
        (new_worker_only && !retirement_host::IsWorkerTask())) return false;
    if (new_nth > 1) { --new_nth; return false; }
    --new_count;
    ++resources.new_failures;
    if (new_time_count < new_failure_times.size()) new_failure_times[new_time_count++] = Now();
    if (new_count == 0) new_size.store(0);
    return true;
}
}

// Link wrapping keeps the allocator/deallocator pair supplied by libstdc++ and
// ASan. Only the selected allocation throws; no test-only Encode replacement.
extern "C" void* __real__Znwm(size_t bytes);
extern "C" void* __real__Znam(size_t bytes);
extern "C" void* __wrap__Znwm(size_t bytes) {
    if (RejectNew(bytes)) throw std::bad_alloc();
    return __real__Znwm(bytes);
}
extern "C" void* __wrap__Znam(size_t bytes) {
    if (RejectNew(bytes)) throw std::bad_alloc();
    return __real__Znam(bytes);
}

namespace rodakos_test::display_service_host {
void JoinTasks() {
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (retirement_host::Snapshot().live_tasks != 0) {
        rodakos::PumpTaskRetirements();
        if (Clock::now() >= deadline) {
            std::fputs("display fixture left a live business task\n", stderr);
            std::abort();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    retirement_host::JoinTasks();
}
void Reset() {
    JoinTasks();
    retirement_host::Reset();
    retirement_host::SetAutoStart(true);
    std::lock_guard<std::mutex> lock(host_mutex);
    if (resources.buffers || resources.encoders) {
        std::fputs("previous test leaked encoder or heap-caps resources\n", stderr);
        std::abort();
    }
    resources = {};
    new_size.store(0);
    new_nth = new_count = heap_size = heap_count = 0;
    new_time_count = process_time_count = 0;
    new_worker_only = heap_after_open = fail_open = fail_process = false;
    throw_process = reject_codec_external = false;
    encoded_size = 4096;
    allow_async = true;
    async_calls = {};
}
void FailNew(size_t bytes, size_t nth, size_t count, bool worker_only) {
    std::lock_guard<std::mutex> lock(host_mutex);
    new_nth = nth;
    new_count = count;
    new_worker_only = worker_only;
    new_size.store(bytes);
}
void FailHeap(size_t bytes, size_t count, bool after_encoder_open) {
    std::lock_guard<std::mutex> lock(host_mutex);
    heap_size = bytes;
    heap_count = count;
    heap_after_open = after_encoder_open;
}
void ClearFailures() {
    std::lock_guard<std::mutex> lock(host_mutex);
    new_size.store(0);
    new_count = heap_count = 0;
    fail_open = fail_process = false;
    throw_process = reject_codec_external = false;
}
void FailEncoderOpen(bool fail) { std::lock_guard<std::mutex> lock(host_mutex); fail_open = fail; }
void FailEncoderProcess(bool fail) { std::lock_guard<std::mutex> lock(host_mutex); fail_process = fail; }
void ThrowEncoderProcess(bool fail) { std::lock_guard<std::mutex> lock(host_mutex); throw_process = fail; }
void RejectCodecExternal(bool fail) { std::lock_guard<std::mutex> lock(host_mutex); reject_codec_external = fail; }
void SetEncodedSize(size_t bytes) { std::lock_guard<std::mutex> lock(host_mutex); encoded_size = bytes; }
void AllowTaskCreation(bool allow) { retirement_host::SetCreationAllowed(allow); }
void AllowAsync(bool allow) { std::lock_guard<std::mutex> lock(host_mutex); allow_async = allow; }
Resources Snapshot() { std::lock_guard<std::mutex> lock(host_mutex); return resources; }
std::vector<int64_t> NewFailureTimes() {
    std::lock_guard<std::mutex> lock(host_mutex);
    return {new_failure_times.begin(), new_failure_times.begin() + new_time_count};
}
std::vector<int64_t> ProcessTimes() {
    std::lock_guard<std::mutex> lock(host_mutex);
    return {process_times.begin(), process_times.begin() + process_time_count};
}
size_t PendingAsync() {
    std::lock_guard<std::mutex> lock(host_mutex);
    return std::count_if(async_calls.begin(), async_calls.end(), [](const Async& call) { return call.callback != nullptr; });
}
void Flush(lv_display_t& display, uint8_t* pixels, const lv_area_t& area, bool last) {
    std::lock_guard<std::recursive_timed_mutex> lock(ui_mutex);
    display.draw_buf.data = pixels;
    display.draw_buf.header.stride = static_cast<unsigned>(area.x2 - area.x1 + 1) * 2;
    display.flush_last = last;
    if (!display.callback) return;
    auto mutable_area = area;
    lv_event_t event{LV_EVENT_FLUSH_START, display.user_data, &display, &mutable_area};
    display.callback(&event);
    event.code = LV_EVENT_FLUSH_FINISH;
    display.callback(&event);
    display.draw_buf.data = nullptr;
}
}

SemaphoreHandle_t xSemaphoreCreateMutex() {
    auto* semaphore = new HostSemaphore;
    semaphore->available = true;
    return semaphore;
}
SemaphoreHandle_t xSemaphoreCreateBinary() { return new HostSemaphore; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    if (!semaphore) return pdFALSE;
    std::unique_lock<std::mutex> lock(semaphore->mutex);
    if (ticks == portMAX_DELAY) {
        // Production uses an unbounded mutex wait. A leaked lock must fail this
        // suite explicitly, rather than leave a hung process in the CI runner.
        if (!semaphore->condition.wait_for(lock, std::chrono::seconds(3), [&] { return semaphore->available; })) {
            std::fputs("semaphore deadlock: production lock was not released\n", stderr);
            std::abort();
        }
    } else if (!semaphore->condition.wait_for(lock, std::chrono::milliseconds(ticks), [&] { return semaphore->available; })) {
        return pdFALSE;
    }
    semaphore->available = false;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    if (!semaphore) return pdFALSE;
    std::lock_guard<std::mutex> lock(semaphore->mutex);
    semaphore->available = true;
    semaphore->condition.notify_one();
    return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t semaphore) { delete semaphore; }
int64_t esp_timer_get_time() { return Now(); }

void* heap_caps_aligned_alloc(size_t alignment, size_t bytes, unsigned caps) {
    std::lock_guard<std::mutex> lock(host_mutex);
    if (bytes == 0) return nullptr;
    if (heap_count && bytes == heap_size && (!heap_after_open || resources.encoders != 0)) {
        --heap_count;
        ++resources.heap_failures;
        return nullptr;
    }
    void* pointer = nullptr;
    if (posix_memalign(&pointer, alignment, bytes) != 0) return nullptr;
    auto slot = std::find_if(allocations.begin(), allocations.end(), [](const Allocation& value) { return value.pointer == nullptr; });
    if (slot == allocations.end()) std::abort();
    *slot = {pointer, bytes, caps};
    ++resources.buffers;
    resources.bytes += bytes;
    resources.peak_bytes = std::max(resources.peak_bytes, resources.bytes);
    return pointer;
}
void NoteRealCall(size_t index, size_t, size_t, int) {
    std::lock_guard<std::mutex> lock(host_mutex);
    ++resources.original_allocator_calls.at(index);
}
unsigned AllocationCaps(void* pointer) {
    std::lock_guard<std::mutex> lock(host_mutex);
    for (const auto& allocation : allocations)
        if (allocation.pointer == pointer) return allocation.caps;
    return 0;
}
extern "C" void* heap_caps_aligned_calloc(size_t alignment, size_t n, size_t size, unsigned caps) {
    {
        std::lock_guard<std::mutex> lock(host_mutex);
        if (caps & MALLOC_CAP_SPIRAM) {
            ++resources.codec_external_calls;
            if (reject_codec_external) return nullptr;
        } else ++resources.codec_internal_calls;
    }
    if (!alignment || (alignment & (alignment - 1)) || n == 0 || size == 0 || size > SIZE_MAX / n)
        return nullptr;
    auto* pointer = heap_caps_aligned_alloc(std::max(alignment, sizeof(void*)), n * size, caps);
    if (pointer) std::memset(pointer, 0, n * size);
    return pointer;
}
extern "C" void* heap_caps_calloc(size_t n, size_t size, unsigned caps) {
    return heap_caps_aligned_calloc(alignof(std::max_align_t), n, size, caps);
}
void heap_caps_free(void* pointer) {
    if (!pointer) return;
    std::lock_guard<std::mutex> lock(host_mutex);
    auto slot = std::find_if(allocations.begin(), allocations.end(), [=](const Allocation& value) { return value.pointer == pointer; });
    if (slot == allocations.end()) std::abort();
    --resources.buffers;
    resources.bytes -= slot->bytes;
    *slot = {};
    std::free(pointer);
}
size_t heap_caps_get_free_size(unsigned) { return 1024 * 1024; }
size_t heap_caps_get_largest_free_block(unsigned) { return 512 * 1024; }

jpeg_error_t jpeg_enc_open(jpeg_enc_config_t* config, jpeg_enc_handle_t* output) {
    { std::lock_guard<std::mutex> lock(host_mutex); if (fail_open) return -1; }
    if (config->width != 320 || config->height != 240 || config->task_enable) return -1;
    auto* encoder = static_cast<HostEncoder*>(jpeg_calloc_inner(
        rodakos_test::display_service_host::kCodecContextBytes));
    if (!encoder) return -1;
    encoder->parts[0] = jpeg_calloc(2, 64);
    if (encoder->parts[0]) encoder->parts[1] = jpeg_calloc_align_inner(1024, 16);
    if (encoder->parts[1]) encoder->parts[2] = jpeg_calloc_align(2048, 32);
    if (!encoder->parts[2]) {
        for (auto* part : encoder->parts) jpeg_free(part);
        jpeg_free(encoder);
        return -1;
    }
    *output = encoder;
    std::lock_guard<std::mutex> lock(host_mutex);
    ++resources.opens;
    ++resources.encoders;
    return JPEG_ERR_OK;
}
jpeg_error_t jpeg_enc_process(jpeg_enc_handle_t encoder, const uint8_t* input, int input_size,
                              uint8_t* output, int output_capacity, int* output_size) {
    std::lock_guard<std::mutex> lock(host_mutex);
    ++resources.processes;
    if (throw_process) throw std::bad_alloc();
    resources.last_output_capacity = static_cast<size_t>(output_capacity);
    const auto pack_rgb = [](const uint8_t* pixel) {
        return (static_cast<uint32_t>(pixel[0]) << 16) |
               (static_cast<uint32_t>(pixel[1]) << 8) | pixel[2];
    };
    if (input != nullptr && input_size >= 3) {
        const size_t pixel_count = static_cast<size_t>(input_size) / 3;
        resources.first_rgb888 = pack_rgb(input);
        resources.middle_rgb888 = pack_rgb(input + (pixel_count / 2) * 3);
        resources.last_rgb888 = pack_rgb(input + (pixel_count - 1) * 3);
    }
    if (process_time_count < process_times.size()) process_times[process_time_count++] = Now();
    if (fail_process || !encoder || input_size != 320 * 240 * 3 ||
        static_cast<size_t>(output_capacity) < encoded_size) return -1;
    // The fake stands at the codec boundary; the real RGB565 conversion and
    // allocation/copy/cleanup paths execute before and after this call.
    std::memset(output, input[0], encoded_size);
    *output_size = static_cast<int>(encoded_size);
    return JPEG_ERR_OK;
}
jpeg_error_t jpeg_enc_close(jpeg_enc_handle_t encoder) {
    // 零大小探针不改变工作区峰值；原 allocator 计数可发现 scope 过早退出。
    if (jpeg_calloc_inner(0) != nullptr) std::abort();
    auto* workspace = static_cast<HostEncoder*>(encoder);
    for (auto* part : workspace->parts) jpeg_free_align(part);
    jpeg_free(workspace);
    std::lock_guard<std::mutex> lock(host_mutex);
    if (!encoder || !resources.encoders) std::abort();
    ++resources.close_scope_checks;
    --resources.encoders;
    ++resources.closes;
    return JPEG_ERR_OK;
}
bool lvgl_port_lock(uint32_t timeout) {
    bool locked = false;
    if (!timeout) {
        ui_mutex.lock();
        locked = true;
    } else {
        locked = ui_mutex.try_lock_for(std::chrono::milliseconds(timeout));
    }
    if (locked) ++ui_lock_depth;
    return locked;
}
void lvgl_port_unlock() {
    if (ui_lock_depth == 0) std::abort();
    --ui_lock_depth;
    ui_mutex.unlock();
}
void lvgl_port_task_wake(int, void*) {}
void lv_display_add_event_cb(lv_display_t* display, lv_event_cb_t callback, int, void* data) {
    {
        std::lock_guard<std::mutex> lock(host_mutex);
        ++resources.event_adds;
        if (ui_lock_depth == 0) ++resources.event_ops_without_lvgl_lock;
    }
    display->callback = callback;
    display->user_data = data;
}
void lv_display_remove_event_cb_with_user_data(lv_display_t* display, lv_event_cb_t callback,
                                                void* data) {
    {
        std::lock_guard<std::mutex> lock(host_mutex);
        ++resources.event_removes;
        if (ui_lock_depth == 0) ++resources.event_ops_without_lvgl_lock;
    }
    if (display->callback == callback && display->user_data == data) {
        display->callback = nullptr;
        display->user_data = nullptr;
    }
}
int lv_async_call(void (*callback)(void*), void* data) {
    std::lock_guard<std::mutex> lock(host_mutex);
    if (!allow_async) return LV_RESULT_INVALID;
    auto slot = std::find_if(async_calls.begin(), async_calls.end(), [](const Async& call) { return call.callback == nullptr; });
    if (slot == async_calls.end()) return LV_RESULT_INVALID;
    *slot = {callback, data};
    return LV_RESULT_OK;
}
void lv_async_call_cancel(void (*callback)(void*), void* data) {
    std::lock_guard<std::mutex> lock(host_mutex);
    for (auto& call : async_calls) if (call.callback == callback && call.data == data) call = {};
}

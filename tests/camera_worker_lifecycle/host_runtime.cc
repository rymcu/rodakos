#include "host_runtime.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace {
using namespace std::chrono_literals;
struct TaskExit {};
struct Task {
    std::thread thread;
    bool deleted = false;
    bool suspended = false;
    bool exited = false;
};
struct Queue {
    std::deque<dvp_cam_event_t> values;
    size_t capacity;
    bool deleted = false;
};
struct Allocation {
    bool live = true;
    bool freed = false;
};
std::mutex mutex;
std::condition_variable condition;
std::map<void*, Allocation> allocations;
std::vector<std::unique_ptr<Queue>> queues;
std::vector<std::unique_ptr<Task>> tasks;
std::map<portMUX_TYPE*, std::unique_ptr<std::recursive_mutex>> spinlocks;
std::mutex stdout_mutex;
thread_local Task* current_task = nullptr;
thread_local char caller_task;
bool block_log = false, log_entered = false, release_log = false, log_held = false;
bool block_receive = false, receive_entered = false, release_receive = false;
bool block_after_receive = false, after_receive_entered = false, release_after_receive = false;
bool block_callback = false, callback_entered = false, release_callback = false;
bool delete_from_callback = false;
int callback_delete_result = -999;
size_t callback_calls = 0;
bool block_final_unlock = false, final_unlock_entered = false, release_final_unlock = false;
bool unsafe_final_unlock = false;
size_t owner_lock_waiting = 0;
bool early_task_run = false;
bool provide_frame = false;
uint8_t callback_frame[64]{};
bool block_capture_start = false, capture_start_entered = false, release_capture_start = false;
bool capture_start_delete = false;
bool unsafe_delete = false, callback_delete = false, owner_waiting = false;
bool fail_task = false, fail_queue = false;
bool fail_task_stack = false, fail_task_tcb = false;
int fail_allocation = 0, allocation_count = 0;
size_t delete_calls = 0, with_caps_deletes = 0, full_wakeups = 0;
size_t stack_bytes = 0;
unsigned stack_caps = 0, priority = 0;
std::vector<std::string> calls;

void Record(const char* name) {
    std::lock_guard<std::mutex> lock(mutex);
    calls.emplace_back(name);
}
void CheckDeleted() {
    if (current_task && current_task->deleted) throw TaskExit{};
}
bool Wait(std::function<bool()> predicate) {
    std::unique_lock<std::mutex> lock(mutex);
    return condition.wait_for(lock, 2s, predicate);
}
void Delete(TaskHandle_t handle, bool caps) {
    auto* task = static_cast<Task*>(handle);
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++delete_calls;
        if (caps) ++with_caps_deletes;
        unsafe_delete = unsafe_delete || log_held;
        callback_delete = callback_delete || (callback_entered && !release_callback);
        unsafe_final_unlock =
            unsafe_final_unlock || (final_unlock_entered && !release_final_unlock);
        capture_start_delete =
            capture_start_delete || (capture_start_entered && !release_capture_start);
        task->deleted = true;
        calls.emplace_back(caps ? "delete-with-caps" : "delete-plain");
        condition.notify_all();
    }
    // Model the reviewed owner WithCaps synchronization, not its kernel internals.
    // The old plain-delete red control intentionally returns while the worker lives.
    if (caps && task->thread.joinable()) task->thread.join();
}
BaseType_t Create(void (*entry)(void*), size_t bytes, void* arg, unsigned prio,
                  TaskHandle_t* output, unsigned caps) {
    std::unique_lock<std::mutex> lock(mutex);
    stack_bytes = bytes;
    stack_caps = caps;
    priority = prio;
    if (fail_task || fail_task_stack || fail_task_tcb) return pdFALSE;
    auto task = std::make_unique<Task>();
    auto* raw = task.get();
    tasks.push_back(std::move(task));
    if (!early_task_run) *output = raw;
    raw->thread = std::thread([raw, entry, arg] {
        current_task = raw;
        try {
            entry(arg);
        } catch (const TaskExit&) {
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            raw->exited = true;
            condition.notify_all();
        }
        current_task = nullptr;
    });
    if (early_task_run) {
        condition.wait(lock, [] { return receive_entered; });
        *output = raw;
    }
    return pdPASS;
}
}  // namespace

namespace worker_host {
void Join() {
    for (auto& task : tasks)
        if (task->thread.joinable()) task->thread.join();
}
void Reset() {
    Join();
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& [pointer, allocation] : allocations)
        if (!allocation.freed) std::free(pointer);
    allocations.clear();
    tasks.clear();
    queues.clear();
    spinlocks.clear();
    calls.clear();
    block_log = log_entered = release_log = log_held = false;
    block_receive = receive_entered = release_receive = false;
    block_after_receive = after_receive_entered = release_after_receive = false;
    block_callback = callback_entered = release_callback = false;
    delete_from_callback = false;
    callback_delete_result = -999;
    callback_calls = 0;
    block_final_unlock = final_unlock_entered = release_final_unlock = unsafe_final_unlock = false;
    owner_lock_waiting = 0;
    early_task_run = provide_frame = false;
    block_capture_start = capture_start_entered = release_capture_start = capture_start_delete =
        false;
    unsafe_delete = callback_delete = owner_waiting = fail_task = fail_queue = false;
    fail_task_stack = fail_task_tcb = false;
    fail_allocation = allocation_count = 0;
    delete_calls = with_caps_deletes = full_wakeups = 0;
    stack_bytes = stack_caps = priority = 0;
}
void BlockLog() {
    std::lock_guard<std::mutex> lock(mutex);
    block_log = true;
}
bool WaitLog() {
    return Wait([] { return log_entered; });
}
void ReleaseLog() {
    std::lock_guard<std::mutex> lock(mutex);
    release_log = true;
    condition.notify_all();
}
void QueueEvent(esp_cam_ctlr_handle_t handle, int type) {
    auto* ctlr = static_cast<dvp_cam_ctlr_t*>(handle);
    std::lock_guard<std::mutex> lock(mutex);
    static_cast<Queue*>(ctlr->event_queue)
        ->values.push_back({static_cast<dvp_cam_event_type_t>(type)});
    condition.notify_all();
}
bool WaitOwnerProgress() {
    return Wait([] { return owner_waiting || delete_calls > 0; });
}
bool DeletedWhileLogHeld() {
    std::lock_guard<std::mutex> lock(mutex);
    return unsafe_delete;
}
size_t DeleteCalls() {
    std::lock_guard<std::mutex> lock(mutex);
    return delete_calls;
}
size_t WithCapsDeleteCalls() {
    std::lock_guard<std::mutex> lock(mutex);
    return with_caps_deletes;
}
size_t LiveAllocations() {
    std::lock_guard<std::mutex> lock(mutex);
    size_t count = 0;
    for (auto [p, allocation] : allocations) {
        (void)p;
        count += allocation.live;
    }
    return count;
}
size_t LiveQueues() {
    std::lock_guard<std::mutex> lock(mutex);
    size_t count = 0;
    for (auto& q : queues) count += !q->deleted;
    return count;
}
void FailAllocation(int index) { fail_allocation = index; }
void FailTaskCreation(bool value) { fail_task = value; }
void FailTaskStack(bool value) { fail_task_stack = value; }
void FailTaskTcb(bool value) { fail_task_tcb = value; }
void FailQueueCreation(bool value) { fail_queue = value; }
unsigned CreatedStackCaps() { return stack_caps; }
size_t CreatedStackBytes() { return stack_bytes; }
unsigned CreatedPriority() { return priority; }
void SetCurrentTask(TaskHandle_t handle) { current_task = static_cast<Task*>(handle); }
void BlockReceive() {
    std::lock_guard<std::mutex> lock(mutex);
    block_receive = true;
}
bool WaitReceive() {
    return Wait([] { return receive_entered; });
}
void ReleaseReceive() {
    std::lock_guard<std::mutex> lock(mutex);
    release_receive = true;
    condition.notify_all();
}
void BlockAfterReceive() {
    std::lock_guard<std::mutex> lock(mutex);
    block_after_receive = true;
}
bool WaitAfterReceive() {
    return Wait([] { return after_receive_entered; });
}
void ReleaseAfterReceive() {
    std::lock_guard<std::mutex> lock(mutex);
    release_after_receive = true;
    condition.notify_all();
}
void BlockCallback() {
    std::lock_guard<std::mutex> lock(mutex);
    block_callback = true;
}
bool WaitCallback() {
    return Wait([] { return callback_entered; });
}
void ReleaseCallback() {
    std::lock_guard<std::mutex> lock(mutex);
    release_callback = true;
    condition.notify_all();
}
bool Callback(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t* trans, void*) {
    std::unique_lock<std::mutex> lock(mutex);
    ++callback_calls;
    callback_entered = true;
    condition.notify_all();
    if (block_callback) condition.wait(lock, [] { return release_callback; });
    if (delete_from_callback) {
        lock.unlock();
        const int result = worker_delete(handle);
        lock.lock();
        callback_delete_result = result;
        condition.notify_all();
    }
    CheckDeleted();
    trans->buffer = provide_frame ? callback_frame : nullptr;
    trans->buflen = provide_frame ? sizeof(callback_frame) : 0;
    return false;
}
bool DeletedDuringCallback() { return callback_delete; }
void DeleteFromCallback(bool value) { delete_from_callback = value; }
int CallbackDeleteResult() {
    std::lock_guard<std::mutex> lock(mutex);
    return callback_delete_result;
}
size_t CallbackCalls() {
    std::lock_guard<std::mutex> lock(mutex);
    return callback_calls;
}
void BlockFinalUnlock() {
    std::lock_guard<std::mutex> lock(mutex);
    block_final_unlock = true;
}
bool WaitFinalUnlock() {
    return Wait([] { return final_unlock_entered; });
}
void ReleaseFinalUnlock() {
    std::lock_guard<std::mutex> lock(mutex);
    release_final_unlock = true;
    condition.notify_all();
}
bool DeletedBeforeFinalUnlock() { return unsafe_final_unlock; }
bool WaitOwnerAtFinalUnlock() {
    return Wait([] { return owner_lock_waiting != 0 || delete_calls != 0; });
}
void RunBeforeHandlePublished(bool value) { early_task_run = value; }
void ProvideFrame(bool value) { provide_frame = value; }
void BlockCaptureStart() { block_capture_start = true; }
bool WaitCaptureStart() {
    return Wait([] { return capture_start_entered; });
}
void ReleaseCaptureStart() {
    std::lock_guard<std::mutex> lock(mutex);
    release_capture_start = true;
    condition.notify_all();
}
bool DeletedDuringCaptureStart() { return capture_start_delete; }
size_t QueueFullWakeups() { return full_wakeups; }
std::vector<std::string> Calls() {
    std::lock_guard<std::mutex> lock(mutex);
    return calls;
}
}  // namespace worker_host

extern "C" {
void worker_lock(portMUX_TYPE* key) {
    std::recursive_mutex* value;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto& item = spinlocks[key];
        if (!item) item = std::make_unique<std::recursive_mutex>();
        value = item.get();
        if (!current_task) {
            ++owner_lock_waiting;
            condition.notify_all();
        }
    }
    value->lock();
    if (!current_task) {
        std::lock_guard<std::mutex> lock(mutex);
        --owner_lock_waiting;
    }
}
void worker_unlock(portMUX_TYPE* key) {
    std::recursive_mutex* value;
    {
        std::unique_lock<std::mutex> lock(mutex);
        value = spinlocks.at(key).get();
#ifdef RODAK_WORKER_COOPERATIVE
        if (current_task && block_final_unlock) {
            auto* ctlr = reinterpret_cast<dvp_cam_ctlr_t*>(reinterpret_cast<char*>(key) -
                                                           offsetof(dvp_cam_ctlr_t, spinlock));
            if (ctlr->worker_quiesced) {
                final_unlock_entered = true;
                condition.notify_all();
                condition.wait(lock, [] { return release_final_unlock; });
            }
        }
#endif
    }
    value->unlock();
}
void worker_log(const char*, const char* format, ...) {
    if (std::string(format).find("invalid state") == std::string::npos) return;
    std::unique_lock<std::mutex> output_lock(stdout_mutex);
    std::unique_lock<std::mutex> lock(mutex);
    log_held = true;
    log_entered = true;
    condition.notify_all();
    if (block_log) condition.wait(lock, [] { return release_log; });
    log_held = false;
    CheckDeleted();
}
uint32_t xPortGetCoreID() { return current_task ? 1u : 0u; }
TaskHandle_t xTaskGetCurrentTaskHandle() {
    return current_task ? static_cast<void*>(current_task) : &caller_task;
}
void vTaskDelete(TaskHandle_t handle) { Delete(handle, false); }
void vTaskDeleteWithCaps(TaskHandle_t handle) { Delete(handle, true); }
void vTaskSuspend(TaskHandle_t) {
    std::unique_lock<std::mutex> lock(mutex);
    current_task->suspended = true;
    condition.notify_all();
    condition.wait(lock, [] { return current_task->deleted; });
    throw TaskExit{};
}
void vTaskDelay(TickType_t) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        owner_waiting = true;
        condition.notify_all();
    }
    std::this_thread::sleep_for(1ms);
}
BaseType_t xTaskCreate(void (*e)(void*), const char*, size_t s, void* a, unsigned p,
                       TaskHandle_t* o) {
    return Create(e, s, a, p, o, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
BaseType_t xTaskCreateWithCaps(void (*e)(void*), const char*, size_t s, void* a, unsigned p,
                               TaskHandle_t* o, unsigned c) {
    return Create(e, s, a, p, o, c);
}
QueueHandle_t xQueueCreate(unsigned count, unsigned size) {
    std::lock_guard<std::mutex> lock(mutex);
    if (fail_queue) return nullptr;
    if (size != sizeof(dvp_cam_event_t)) std::abort();
    auto q = std::make_unique<Queue>();
    q->capacity = count;
    auto* raw = q.get();
    queues.push_back(std::move(q));
    return raw;
}
BaseType_t xQueueReceive(QueueHandle_t handle, void* output, TickType_t) {
    std::unique_lock<std::mutex> lock(mutex);
    auto* q = static_cast<Queue*>(handle);
    receive_entered = true;
    condition.notify_all();
    if (block_receive) condition.wait(lock, [] { return release_receive; });
    condition.wait(lock, [&] { return !q->values.empty() || current_task->deleted; });
    CheckDeleted();
    *static_cast<dvp_cam_event_t*>(output) = q->values.front();
    q->values.pop_front();
    after_receive_entered = true;
    condition.notify_all();
    if (block_after_receive) condition.wait(lock, [] { return release_after_receive; });
    CheckDeleted();
    return pdPASS;
}
BaseType_t xQueueSendToFront(QueueHandle_t handle, const void* input, TickType_t ticks) {
    std::lock_guard<std::mutex> lock(mutex);
    auto* q = static_cast<Queue*>(handle);
    if (ticks != 0) std::abort();
    if (q->values.size() >= q->capacity) {
        ++full_wakeups;
        return pdFALSE;
    }
    q->values.push_front(*static_cast<const dvp_cam_event_t*>(input));
    condition.notify_all();
    return pdPASS;
}
void vQueueDelete(QueueHandle_t handle) {
    std::lock_guard<std::mutex> lock(mutex);
    static_cast<Queue*>(handle)->deleted = true;
    calls.emplace_back("free-queue");
}
void* heap_caps_calloc(size_t count, size_t size, unsigned) {
    std::lock_guard<std::mutex> lock(mutex);
    if (++allocation_count == fail_allocation) return nullptr;
    auto* value = std::calloc(count, size);
    allocations[value] = {};
    return value;
}
void* heap_caps_aligned_alloc(size_t, size_t size, unsigned caps) {
    return heap_caps_calloc(1, size, caps);
}
void heap_caps_free(void* value) {
    if (!value) return;
    std::lock_guard<std::mutex> lock(mutex);
    auto& allocation = allocations.at(value);
    if (!allocation.live) std::abort();
    allocation.live = false;
    bool still_running = false;
    for (const auto& task : tasks) still_running = still_running || !task->exited;
    // Do not turn the deliberate old-delete boundary failure into host UAF.
    // Correct owner deletion has joined the worker and releases memory immediately.
    if (!still_running) {
        std::free(value);
        allocation.freed = true;
    }
    calls.emplace_back("free-heap");
}
uint32_t rodak_camera_teardown_record(uint32_t, uint32_t, int32_t) { return 1; }
esp_err_t gdma_disconnect(gdma_channel_handle_t) {
    Record("dma-disconnect");
    return ESP_OK;
}
esp_err_t gdma_del_channel(gdma_channel_handle_t) {
    Record("dma-delete");
    return ESP_OK;
}
esp_err_t gdma_stop(gdma_channel_handle_t) { return ESP_OK; }
esp_err_t gpio_intr_disable(int) { return ESP_OK; }
esp_err_t gpio_intr_enable(int) { return ESP_OK; }
esp_err_t gpio_isr_handler_remove(int) { return ESP_OK; }
esp_err_t gpio_install_isr_service(int) { return ESP_OK; }
esp_err_t gpio_set_intr_type(int, int) { return ESP_OK; }
esp_err_t gpio_isr_handler_add(int, void*, void*) { return ESP_OK; }
esp_err_t gdma_get_channel_id(gdma_channel_handle_t, int* id) {
    *id = 0;
    return ESP_OK;
}
esp_err_t gdma_register_rx_event_callbacks(gdma_channel_handle_t, const gdma_rx_event_callbacks_t*,
                                           void*) {
    return ESP_OK;
}
void cam_hal_stop_streaming(cam_hal_context_t*) {}
void cam_hal_deinit(cam_hal_context_t*) {}
void cam_hal_init_ext(cam_hal_context_t*, const cam_hal_config_t*) {}
esp_err_t dvp_get_frame_size(const esp_cam_ctlr_dvp_config_t*, size_t* size) {
    *size = 153600;
    return ESP_OK;
}
esp_err_t dvp_dma_init(gdma_channel_handle_t* channel) {
    *channel = &caller_task;
    return ESP_OK;
}
void dvp_config_dma_desc(dma_descriptor_t*, size_t, uint8_t*, size_t, dma_descriptor_t*) {}
uint32_t get_next_dma_desc_addr(dvp_cam_ctlr_t*) { return 0; }
uint32_t dvp_get_dma_valid_size(dvp_cam_ctlr_t*, uint32_t) { return 0; }
uint32_t dvp_calculate_jpeg_size(const uint8_t*, size_t) { return 0; }
esp_err_t dvp_start_capturing(dvp_cam_ctlr_t*) {
    std::unique_lock<std::mutex> lock(mutex);
    capture_start_entered = true;
    condition.notify_all();
    if (block_capture_start) condition.wait(lock, [] { return release_capture_start; });
    CheckDeleted();
    return ESP_OK;
}
}

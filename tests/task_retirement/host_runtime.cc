#include "task_retirement_host.h"
#include "phone_os/task-retirement.h"
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
struct Task {
    std::thread thread;
    std::mutex mutex;
    std::condition_variable condition;
    std::string name;
    StackType_t* stack = nullptr;
    StaticTask_t* tcb = nullptr;
    UBaseType_t priority = 0;
    BaseType_t core = 0;
    bool released = false;
    bool entered = false;
    bool publication_wait = false;
    bool parked = false;
    bool deleted = false;
    bool kernel_complete = false;
    bool core_current = true;
    bool delete_claimed = false;
    bool dynamic = false;
    bool notification_wait = false;
    uint32_t notifications = 0;
};
struct Allocation { void* pointer = nullptr; size_t bytes = 0; bool pool = false; };
std::mutex state_mutex;
std::vector<std::unique_ptr<Task>> tasks;
std::array<Allocation, 128> allocations{};
retirement_host::Resources resources;
bool auto_start = true, creation_allowed = true, reject_cleanup = true;
bool dynamic_tasks_allowed = false;
bool fail_allocation = false;
void (*delay_hook)(TickType_t) = nullptr;
void (*before_create_returns)(TaskHandle_t) = nullptr;
void (*before_delete)(TaskHandle_t) = nullptr;
void (*before_suspend)(TaskHandle_t) = nullptr;
retirement_host::Gate* suspend_gate = nullptr;
thread_local Task* current_task = nullptr;
thread_local Task* queried_task = nullptr;
thread_local int critical_depth = 0;
thread_local unsigned char external_task = 0;

[[noreturn]] void Fail(const char* reason) {
    std::fprintf(stderr, "RETIREMENT_HOST_ASSERT: %s\n", reason);
    std::fflush(stderr);
    std::abort();
}
void Check(bool value, const char* reason) { if (!value) Fail(reason); }
void* Allocate(size_t bytes, bool pool, unsigned caps) {
    std::lock_guard<std::mutex> lock(state_mutex);
    ++resources.allocation_calls;
    if (fail_allocation) { fail_allocation = false; return nullptr; }
    void* pointer = std::calloc(1, bytes);
    if (!pointer) return nullptr;
    for (auto& allocation : allocations) {
        if (allocation.pointer) continue;
        allocation = {pointer, bytes, pool};
        if (pool) {
            ++resources.pool_allocations;
            resources.pool_bytes += bytes;
            resources.pool_caps = caps;
        } else {
            ++resources.live_task_buffers;
        }
        return pointer;
    }
    Fail("allocation ledger exhausted");
}
void Free(void* pointer) {
    if (!pointer) return;
    std::lock_guard<std::mutex> lock(state_mutex);
    for (auto& allocation : allocations) {
        if (allocation.pointer != pointer) continue;
        if (allocation.pool) resources.pool_bytes -= allocation.bytes;
        else --resources.live_task_buffers;
        allocation = {};
        std::free(pointer);
        return;
    }
    Fail("double or unknown task buffer free");
}
}

namespace retirement_host {
void Gate::Enter() {
    std::unique_lock<std::mutex> lock(mutex_);
    entered_ = true;
    condition_.notify_all();
    condition_.wait(lock, [&] { return released_; });
}
bool Gate::Wait(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_.wait_for(lock, timeout, [&] { return entered_; });
}
void Gate::Release() {
    std::lock_guard<std::mutex> lock(mutex_);
    released_ = true;
    condition_.notify_all();
}
void Reset() {
    JoinTasks();
    std::lock_guard<std::mutex> lock(state_mutex);
    Check(resources.live_tasks == 0 && resources.live_task_buffers == 0,
          "Reset with unreclaimed WithCaps tasks");
    const auto pool_count = resources.pool_allocations;
    const auto pool_bytes = resources.pool_bytes;
    const auto pool_caps = resources.pool_caps;
    resources = {};
    resources.pool_allocations = pool_count;
    resources.pool_bytes = pool_bytes;
    resources.pool_caps = pool_caps;
    tasks.clear();
    auto_start = creation_allowed = reject_cleanup = true;
    dynamic_tasks_allowed = false;
    fail_allocation = false;
    delay_hook = nullptr;
    before_create_returns = nullptr;
    before_delete = nullptr;
    before_suspend = nullptr;
    suspend_gate = nullptr;
}
void SetAutoStart(bool enabled) { std::lock_guard<std::mutex> lock(state_mutex); auto_start = enabled; }
void SetCreationAllowed(bool allowed) { std::lock_guard<std::mutex> lock(state_mutex); creation_allowed = allowed; }
void SetDynamicTasksAllowed(bool allowed) { std::lock_guard<std::mutex> lock(state_mutex); dynamic_tasks_allowed = allowed; }
void SetDelayHook(void (*hook)(TickType_t)) { std::lock_guard<std::mutex> lock(state_mutex); delay_hook = hook; }
void SetBeforeCreateReturnsHook(void (*hook)(TaskHandle_t)) {
    std::lock_guard<std::mutex> lock(state_mutex); before_create_returns = hook;
}
void RejectCleanupTask(bool reject) { std::lock_guard<std::mutex> lock(state_mutex); reject_cleanup = reject; }
void FailNextAllocation() { std::lock_guard<std::mutex> lock(state_mutex); fail_allocation = true; }
void HoldCoreAfterSuspend(Gate* gate) { std::lock_guard<std::mutex> lock(state_mutex); suspend_gate = gate; }
void SetBeforeDeleteHook(void (*hook)(TaskHandle_t)) { std::lock_guard<std::mutex> lock(state_mutex); before_delete = hook; }
void SetBeforeExternalSuspendHook(void (*hook)(TaskHandle_t)) { std::lock_guard<std::mutex> lock(state_mutex); before_suspend = hook; }
bool IsWorkerTask() { return current_task != nullptr; }
const char* CurrentTaskName() { return current_task ? current_task->name.c_str() : "external"; }
Resources Snapshot() { std::lock_guard<std::mutex> lock(state_mutex); return resources; }
void RunTasks() {
    std::lock_guard<std::mutex> lock(state_mutex);
    for (auto& task : tasks) {
        std::lock_guard<std::mutex> task_lock(task->mutex);
        task->released = true;
        task->condition.notify_all();
    }
}
void JoinTasks() {
    std::vector<Task*> snapshot;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        for (auto& task : tasks) snapshot.push_back(task.get());
    }
    for (auto* task : snapshot) {
        if (task == current_task) continue;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (;;) {
            rodakos::PumpTaskRetirements();
            { std::lock_guard<std::mutex> lock(task->mutex); if (task->kernel_complete) break; }
            Check(std::chrono::steady_clock::now() < deadline,
                  "JoinTasks requires business workers to stop before drain");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
}

extern "C" {
void* retirement_host_heap_malloc(size_t bytes, unsigned caps) { return Allocate(bytes, false, caps); }
void* retirement_host_heap_calloc(size_t count, size_t size, unsigned caps) {
    if (size && count > SIZE_MAX / size) return nullptr;
    return Allocate(count * size, true, caps);
}
void retirement_host_heap_free(void* pointer) { Free(pointer); }
void* pvPortMalloc(size_t bytes) { return Allocate(bytes, false, 1); }
void vPortFree(void* pointer) { Free(pointer); }
void retirement_host_enter_critical(portMUX_TYPE* mux) {
    Check(pthread_mutex_lock(&mux->mutex) == 0, "critical lock failed");
    ++critical_depth;
}
void retirement_host_exit_critical(portMUX_TYPE* mux) {
    Check(critical_depth > 0, "critical unlock without ownership");
    --critical_depth;
    Check(pthread_mutex_unlock(&mux->mutex) == 0, "critical unlock failed");
}
TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t entry, const char* name,
    configSTACK_DEPTH_TYPE depth, void* context, UBaseType_t priority, StackType_t* stack,
    StaticTask_t* tcb, BaseType_t core) {
    Task* task;
    void (*hook)(TaskHandle_t);
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (!creation_allowed) return nullptr;
        auto next = std::make_unique<Task>();
        task = next.get();
        task->name = name;
        task->stack = stack;
        task->tcb = tcb;
        task->priority = priority;
        task->core = core == tskNO_AFFINITY ? 0 : core;
        task->released = auto_start;
        hook = before_create_returns;
        tasks.push_back(std::move(next));
        ++resources.tasks_created;
        ++resources.live_tasks;
        resources.last_stack_bytes = static_cast<size_t>(depth) * sizeof(StackType_t);
    }
    task->thread = std::thread([task, entry, context] {
        current_task = task;
        {
            std::unique_lock<std::mutex> lock(task->mutex);
            task->condition.wait(lock, [&] { return task->released; });
            task->entered = true;
            task->condition.notify_all();
        }
        entry(context);
        Fail("task entry returned instead of retiring");
    });
    if (hook) {
        std::unique_lock<std::mutex> lock(task->mutex);
        Check(task->condition.wait_for(lock, std::chrono::seconds(3), [&] {
            return task->publication_wait;
        }), "worker did not reach create-before-publication gate");
        lock.unlock();
        hook(task);
    }
    return task;
}
BaseType_t xTaskCreateWithCaps(TaskFunction_t entry, const char* name,
    configSTACK_DEPTH_TYPE depth, void* context, UBaseType_t priority,
    TaskHandle_t* output, UBaseType_t caps) {
    return xTaskCreatePinnedToCoreWithCaps(entry, name, depth, context, priority,
                                         output, tskNO_AFFINITY, caps);
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char* name,
    configSTACK_DEPTH_TYPE depth, void* context, UBaseType_t priority,
    TaskHandle_t* output, BaseType_t core) {
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (std::strcmp(name, "prvTaskDeleteWithCapsTask") == 0) {
            ++resources.cleanup_create_attempts;
            std::fprintf(stderr, "EXPECTED_IDF_CLEANUP_CREATE_REJECTED name=%s\n", name);
            std::fflush(stderr);
            if (reject_cleanup) return pdFAIL;
            Fail("cleanup task must remain rejected in retirement fixture");
        }
        Check(dynamic_tasks_allowed, "unexpected dynamic task creation in retirement fixture");
        if (!creation_allowed) return pdFAIL;
    }
    auto* stack = static_cast<StackType_t*>(Allocate(depth, false, 1));
    auto* tcb = static_cast<StaticTask_t*>(Allocate(sizeof(StaticTask_t), false, 1));
    if (!stack || !tcb) { Free(stack); Free(tcb); return pdFAIL; }
    Task* task;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        auto next = std::make_unique<Task>();
        task = next.get();
        task->name = name;
        task->stack = stack;
        task->tcb = tcb;
        task->priority = priority;
        task->core = core == tskNO_AFFINITY ? 0 : core;
        task->released = auto_start;
        task->dynamic = true;
        tasks.push_back(std::move(next));
        ++resources.tasks_created;
        ++resources.dynamic_tasks_created;
        ++resources.live_tasks;
    }
    task->thread = std::thread([task, entry, context] {
        current_task = task;
        bool deleted;
        {
            std::unique_lock<std::mutex> lock(task->mutex);
            task->condition.wait(lock, [&] { return task->released || task->deleted; });
            deleted = task->deleted;
            if (!deleted) task->entered = true;
            task->condition.notify_all();
        }
        if (deleted) return;
        entry(context);
        Fail("dynamic task entry returned instead of parking");
    });
    *output = task;
    return pdPASS;
}
BaseType_t xTaskCreate(TaskFunction_t entry, const char* name, configSTACK_DEPTH_TYPE depth,
    void* context, UBaseType_t priority, TaskHandle_t* output) {
    return xTaskCreatePinnedToCore(entry, name, depth, context, priority, output, tskNO_AFFINITY);
}
eTaskState eTaskGetState(TaskHandle_t handle) {
    auto* task = static_cast<Task*>(handle);
    Check(task != nullptr, "state query requires a worker");
    std::lock_guard<std::mutex> lock(task->mutex);
    if (task->deleted) return eDeleted;
    if (task->parked) return eSuspended;
    if (task->notification_wait) return eBlocked;
    return task->entered ? eRunning : eReady;
}
BaseType_t xTaskNotifyGive(TaskHandle_t handle) {
    auto* task = static_cast<Task*>(handle);
    Check(task != nullptr, "notification requires a worker");
    std::lock_guard<std::mutex> lock(task->mutex);
    Check(!task->deleted, "notification of deleted task");
    ++task->notifications;
    task->condition.notify_all();
    return pdPASS;
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks) {
    Check(current_task != nullptr, "notification wait requires a worker");
    uint32_t count;
    bool deleted;
    {
        std::unique_lock<std::mutex> lock(current_task->mutex);
        current_task->notification_wait = true;
        current_task->condition.notify_all();
        const auto ready = [] { return current_task->notifications || current_task->deleted; };
        if (ticks == portMAX_DELAY) current_task->condition.wait(lock, ready);
        else current_task->condition.wait_for(lock, std::chrono::milliseconds(ticks), ready);
        current_task->notification_wait = false;
        deleted = current_task->deleted;
        count = current_task->notifications;
        if (count) current_task->notifications = clear ? 0 : count - 1;
    }
    if (deleted) pthread_exit(nullptr);
    return count;
}
// No real ESP stack is sampled by a host thread.
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 4096; }
TaskHandle_t xTaskGetCurrentTaskHandle() {
    return current_task ? static_cast<void*>(current_task) : &external_task;
}
TaskHandle_t xTaskGetCurrentTaskHandleForCore(BaseType_t core) {
    { std::lock_guard<std::mutex> lock(state_mutex); ++resources.cross_core_queries; }
    if (!queried_task || core != queried_task->core) return nullptr;
    std::lock_guard<std::mutex> lock(queried_task->mutex);
    return queried_task->core_current ? queried_task : nullptr;
}
void vTaskDelay(TickType_t ticks) {
    Check(critical_depth == 0, "delay while retirement critical lock held");
    if (current_task) {
        std::lock_guard<std::mutex> lock(current_task->mutex);
        current_task->publication_wait = true;
        current_task->condition.notify_all();
    }
    void (*hook)(TickType_t);
    { std::lock_guard<std::mutex> lock(state_mutex); hook = delay_hook; }
    if (hook) hook(ticks);
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks ? ticks : 1));
}
void vTaskSuspend(TaskHandle_t handle) {
    Check(critical_depth == 0, "suspend while retirement critical lock held");
    auto* task = handle ? static_cast<Task*>(handle) : current_task;
    Check(task != nullptr, "cannot suspend external test task");
    if (task != current_task) {
        void (*hook)(TaskHandle_t);
        { std::lock_guard<std::mutex> lock(state_mutex); hook = before_suspend; ++resources.external_suspends; }
        if (hook) hook(task);
        {
            std::lock_guard<std::mutex> lock(task->mutex);
            Check(!task->delete_claimed, "duplicate external retirement claim");
            task->delete_claimed = true;
        }
        queried_task = task;
        return;
    }
    retirement_host::Gate* gate;
    { std::lock_guard<std::mutex> lock(state_mutex); gate = suspend_gate; }
    if (gate) gate->Enter();
    {
        std::unique_lock<std::mutex> lock(task->mutex);
        task->parked = true;
        task->core_current = false;
        task->condition.notify_all();
        task->condition.wait(lock, [&] { return task->deleted; });
    }
    // The production body already returned. Assertions are made before this
    // host-thread boundary; the legacy self-delete red case aborts the process.
    pthread_exit(nullptr);
}
void vTaskDelete(TaskHandle_t handle) {
    Check(critical_depth == 0, "delete while retirement critical lock held");
    Check(handle && handle != current_task, "unexpected kernel self delete");
    auto* task = static_cast<Task*>(handle);
    void (*hook)(TaskHandle_t);
    { std::lock_guard<std::mutex> lock(state_mutex); hook = before_delete; }
    if (hook) hook(handle);
    {
        std::unique_lock<std::mutex> lock(task->mutex);
        if (task->dynamic) {
            Check(task->condition.wait_for(lock, std::chrono::seconds(3), [&] {
                return !task->entered || task->parked || task->notification_wait;
            }), "dynamic delete requires a blocked or parked worker");
        } else {
            Check(task->parked && !task->core_current, "delete before worker core convergence");
        }
        Check(!task->deleted, "duplicate task delete");
        task->deleted = true;
        task->condition.notify_all();
    }
    // Model synchronous kernel deletion: the worker must really stop executing
    // before the real WithCaps function can retrieve and free its stack/TCB.
    // Exactly one retirement claimer owns this join; JoinTasks only observes.
    task->thread.join();
    if (task->dynamic) {
        Free(task->stack);
        Free(task->tcb);
        task->stack = nullptr;
        task->tcb = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(task->mutex);
        task->kernel_complete = true;
    }
    { std::lock_guard<std::mutex> lock(state_mutex); ++resources.task_deletes; --resources.live_tasks; }
}
BaseType_t xTaskGetStaticBuffers(TaskHandle_t handle, StackType_t** stack, StaticTask_t** tcb) {
    auto* task = static_cast<Task*>(handle);
    std::lock_guard<std::mutex> lock(task->mutex);
    Check(task->kernel_complete, "static buffers fetched before worker execution stopped");
    *stack = task->stack;
    *tcb = task->tcb;
    return pdTRUE;
}
UBaseType_t uxTaskPriorityGet(TaskHandle_t handle) {
    auto* task = handle ? static_cast<Task*>(handle) : current_task;
    return task ? task->priority : 1;
}
BaseType_t xPortGetCoreID() { return current_task ? current_task->core : 0; }
void vPortAssertIfInISR() {}
void retirement_host_yield() {
    { std::lock_guard<std::mutex> lock(state_mutex); ++resources.yields; }
    std::this_thread::yield();
}
void retirement_host_log(const char* tag, const char* format, ...) {
    std::fprintf(stderr, "%s: ", tag);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}
}

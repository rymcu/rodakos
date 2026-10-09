#pragma once
#include "freertos/task.h"
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace retirement_host {
struct Resources {
    size_t tasks_created = 0;
    size_t task_deletes = 0;
    size_t live_tasks = 0;
    size_t live_task_buffers = 0;
    size_t pool_allocations = 0;
    size_t pool_bytes = 0;
    unsigned pool_caps = 0;
    size_t allocation_calls = 0;
    size_t cleanup_create_attempts = 0;
    size_t cross_core_queries = 0;
    size_t yields = 0;
    size_t external_suspends = 0;
    size_t dynamic_tasks_created = 0;
    size_t last_stack_bytes = 0;
};

class Gate {
public:
    void Enter();
    bool Wait(std::chrono::milliseconds timeout = std::chrono::seconds(3));
    void Release();
private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool entered_ = false;
    bool released_ = false;
};

void Reset();
void SetAutoStart(bool enabled);
void RunTasks();
void JoinTasks();
void SetCreationAllowed(bool allowed);
// Opt in only for fixtures that also exercise ordinary internal-stack tasks.
// Their deletion is modeled at a blocked notification or final park boundary.
void SetDynamicTasksAllowed(bool allowed);
void SetDelayHook(void (*hook)(TickType_t));
bool IsWorkerTask();
const char* CurrentTaskName();
Resources Snapshot();
// Next task's thread enters the common Entry before create returns. The hook
// runs on the creator once that worker reaches its publication wait.
void SetBeforeCreateReturnsHook(void (*hook)(TaskHandle_t));
void RejectCleanupTask(bool reject);
void FailNextAllocation();
void HoldCoreAfterSuspend(Gate* gate);
void SetBeforeDeleteHook(void (*hook)(TaskHandle_t));
void SetBeforeExternalSuspendHook(void (*hook)(TaskHandle_t));
}

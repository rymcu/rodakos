#include "host_runtime.h"
#include "task_retirement_host.h"

#include <freertos/task.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {
std::mutex pause_mutex;
std::condition_variable pause_condition;
bool paused = false;
unsigned parked = 0;
std::mutex hook_mutex;
std::function<void()> after_give;
thread_local bool inside_give_hook = false;
void DelayHook(TickType_t) {
    if (retirement_host::IsWorkerTask()) {
        std::unique_lock<std::mutex> lock(pause_mutex);
        if (paused) {
            ++parked;
            pause_condition.notify_all();
            pause_condition.wait(lock, []() { return !paused; });
            --parked;
        }
    }
}
}

namespace rodakos_test {
void ResetWorkers() {
    retirement_host::Reset();
    retirement_host::SetDelayHook(DelayHook);
    SetAfterSemaphoreGiveHook({});
}
bool PauseWorkers() {
    std::unique_lock<std::mutex> lock(pause_mutex);
    paused = true;
    return pause_condition.wait_for(lock, std::chrono::seconds(2), []() { return parked != 0; });
}
void ResumeWorkers() {
    std::lock_guard<std::mutex> lock(pause_mutex);
    paused = false;
    pause_condition.notify_all();
}
void JoinWorkers() {
    ResumeWorkers();
    retirement_host::JoinTasks();
}
void SetAfterSemaphoreGiveHook(std::function<void()> hook) {
    std::lock_guard<std::mutex> lock(hook_mutex);
    after_give = std::move(hook);
}
void AfterSemaphoreGive() {
    if (inside_give_hook) return;
    std::function<void()> callback;
    { std::lock_guard<std::mutex> lock(hook_mutex); callback = after_give; }
    if (callback) {
        inside_give_hook = true;
        callback();
        inside_give_hook = false;
    }
}
}

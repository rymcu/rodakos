#include "host_runtime.h"

#include <freertos/task.h>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {
struct HostTask { std::thread thread; };
thread_local HostTask* current_task = nullptr;
thread_local int external_task_identity = 0;
std::vector<std::unique_ptr<HostTask>> tasks;
std::mutex pause_mutex;
std::condition_variable pause_condition;
bool paused = false;
unsigned parked = 0;
}

TaskHandle_t xTaskGetCurrentTaskHandle() {
    return current_task != nullptr ? static_cast<void*>(current_task)
                                  : static_cast<void*>(&external_task_identity);
}

BaseType_t xTaskCreate(TaskFunction_t entry, const char*, uint32_t, void* argument,
                       UBaseType_t, TaskHandle_t* handle) {
    auto task = std::make_unique<HostTask>();
    HostTask* identity = task.get();
    *handle = identity;
    task->thread = std::thread([entry, argument, identity]() {
        current_task = identity;
        entry(argument);
        current_task = nullptr;
    });
    tasks.push_back(std::move(task));
    return pdPASS;
}

void vTaskDelay(TickType_t ticks) {
    if (current_task != nullptr) {
        std::unique_lock<std::mutex> lock(pause_mutex);
        if (paused) {
            ++parked;
            pause_condition.notify_all();
            pause_condition.wait(lock, []() { return !paused; });
            --parked;
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks == 0 ? 1 : ticks));
}

namespace rodakos_test {
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
    for (auto& task : tasks) {
        if (task->thread.joinable()) task->thread.join();
    }
    tasks.clear();
}
}

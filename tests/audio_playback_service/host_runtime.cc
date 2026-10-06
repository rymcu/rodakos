#include "host_runtime.h"
#include "freertos/task.h"
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
namespace audio_host {
std::atomic<bool> fail_task_creation{false};
std::function<void()> task_start_hook;
std::mutex task_mutex;
std::vector<std::unique_ptr<std::thread>> tasks;
void JoinTasks() {
    std::vector<std::unique_ptr<std::thread>> pending;
    { std::lock_guard<std::mutex> lock(task_mutex); pending.swap(tasks); }
    for (auto& task : pending) if (task->joinable()) task->join();
}
}
BaseType_t xTaskCreate(TaskFunction_t function, const char*, uint32_t, void* argument,
                       uint32_t, TaskHandle_t* handle) {
    if (audio_host::fail_task_creation) return pdFAIL;
    std::lock_guard<std::mutex> lock(audio_host::task_mutex);
    auto task = std::make_unique<std::thread>();
    *handle = task.get();
    const auto hook = audio_host::task_start_hook;
    *task = std::thread([=]() { if (hook) hook(); function(argument); });
    audio_host::tasks.push_back(std::move(task));
    return pdPASS;
}
void vTaskDelay(TickType_t) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }

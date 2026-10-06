#pragma once
#include "FreeRTOS.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using TaskFunction_t = void (*)(void*);
namespace music_test {
inline std::mutex task_mutex;
inline std::vector<std::thread> threads;
inline std::atomic<bool> fail_monitor{false};
inline void JoinTasks() {
    std::vector<std::thread> pending;
    { std::lock_guard<std::mutex> lock(task_mutex); pending.swap(threads); }
    for (auto& thread : pending) if (thread.joinable()) thread.join();
}
}
inline BaseType_t xTaskCreate(TaskFunction_t entry, const char* name, uint32_t, void* data,
                             uint32_t, TaskHandle_t* handle) {
    if (std::string(name) == "music_player" && music_test::fail_monitor) return pdFAIL;
    if (handle) *handle = reinterpret_cast<void*>(1);
    std::lock_guard<std::mutex> lock(music_test::task_mutex);
    music_test::threads.emplace_back([=] { entry(data); });
    return pdPASS;
}
inline void vTaskDelay(TickType_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms * 10)); }
inline void vTaskDelete(TaskHandle_t) {}
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return nullptr; }

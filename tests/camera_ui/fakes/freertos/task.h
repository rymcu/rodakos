#pragma once
#include "FreeRTOS.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
using TaskFunction_t = void (*)(void*);
namespace camera_test {
inline thread_local bool capture_worker = false;
inline std::atomic<bool> fail_task_creation{false};
inline std::atomic<size_t> internal_task_creations{0};
inline std::atomic<size_t> with_caps_task_creations{0};
inline std::atomic<uint32_t> last_task_caps{0};
inline std::atomic<uint32_t> last_task_stack_bytes{0};
inline std::atomic<uintptr_t> next_task_handle{1};
inline std::mutex task_mutex;
inline std::vector<std::thread> threads;
inline void ResetTaskStats() {
    fail_task_creation = false;
    internal_task_creations = 0;
    with_caps_task_creations = 0;
    last_task_caps = 0;
    last_task_stack_bytes = 0;
}
inline void JoinTasks() {
    std::vector<std::thread> pending;
    {
        std::lock_guard<std::mutex> lock(task_mutex);
        pending.swap(threads);
    }
    for (auto& thread : pending) {
        if (thread.joinable()) thread.join();
    }
}
}
inline BaseType_t xTaskCreate(TaskFunction_t entry, const char*, uint32_t stack_bytes, void* data,
                              uint32_t, TaskHandle_t* handle) {
    ++camera_test::internal_task_creations;
    if (camera_test::fail_task_creation) return pdFAIL;
    camera_test::last_task_stack_bytes = stack_bytes;
    if (handle != nullptr) {
        *handle = reinterpret_cast<TaskHandle_t>(camera_test::next_task_handle.fetch_add(1));
    }
    std::lock_guard<std::mutex> lock(camera_test::task_mutex);
    camera_test::threads.emplace_back([=] {
        camera_test::capture_worker = true;
        entry(data);
    });
    return pdPASS;
}
inline BaseType_t xTaskCreateWithCaps(TaskFunction_t entry, const char*, uint32_t stack_bytes,
                                      void* data, uint32_t, TaskHandle_t* handle, uint32_t caps) {
    ++camera_test::with_caps_task_creations;
    camera_test::last_task_caps = caps;
    camera_test::last_task_stack_bytes = stack_bytes;
    if (camera_test::fail_task_creation) return pdFAIL;
    if (handle != nullptr) {
        *handle = reinterpret_cast<TaskHandle_t>(camera_test::next_task_handle.fetch_add(1));
    }
    std::lock_guard<std::mutex> lock(camera_test::task_mutex);
    camera_test::threads.emplace_back([=] {
        camera_test::capture_worker = true;
        entry(data);
    });
    return pdPASS;
}
inline void vTaskDelete(TaskHandle_t) {}
inline void vTaskDeleteWithCaps(TaskHandle_t) {}
inline void vTaskDelay(TickType_t ticks) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks * 10));
}

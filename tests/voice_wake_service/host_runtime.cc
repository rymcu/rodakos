#include "freertos/FreeRTOS.h"
#include <memory>
#include <sys/time.h>

namespace wake_host {
std::mutex tasks_mutex;
std::vector<std::unique_ptr<std::thread>> tasks;
std::atomic<int64_t> unix_us{1800000000000000};
std::atomic<unsigned> wall_reads{0};
void JoinTasks() {
    std::vector<std::unique_ptr<std::thread>> pending;
    { std::lock_guard<std::mutex> lock(tasks_mutex); pending.swap(tasks); }
    for (auto& task : pending) if (task->joinable()) task->join();
}
}
int xTaskCreate(TaskFunction_t function, const char*, uint32_t, void* argument,
                uint32_t, TaskHandle_t* task) {
    std::lock_guard<std::mutex> lock(wake_host::tasks_mutex);
    auto thread = std::make_unique<std::thread>();
    *task = thread.get();
    *thread = std::thread([=]() { function(argument); });
    wake_host::tasks.push_back(std::move(thread));
    return pdPASS;
}
void vTaskDelay(TickType_t) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
extern "C" int __wrap_gettimeofday(timeval* value, void*) {
    ++wake_host::wall_reads;
    const int64_t us = wake_host::unix_us.load();
    value->tv_sec = us / 1000000;
    value->tv_usec = us % 1000000;
    return 0;
}

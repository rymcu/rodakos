#pragma once
#include "../../../task_retirement/fakes/freertos/task.h"
#include <chrono>
inline TickType_t xTaskGetTickCount() {
    return static_cast<TickType_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

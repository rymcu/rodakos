#pragma once

#include <chrono>
#include <cstdint>

inline int64_t esp_timer_get_time() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

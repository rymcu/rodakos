#pragma once

#include <cstdint>

extern "C" {

inline bool lvgl_port_lock(uint32_t) {
    return true;
}

inline void lvgl_port_unlock() {}

constexpr int LVGL_PORT_EVENT_DISPLAY = 0;

inline void lvgl_port_task_wake(int, void*) {}

}

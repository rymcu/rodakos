#pragma once
#include <cstdint>
namespace cloud_ui_test {
inline bool ui_lock_available = true;
}
extern "C" {
inline bool lvgl_port_lock(uint32_t) { return cloud_ui_test::ui_lock_available; }
inline void lvgl_port_unlock() {}
constexpr int LVGL_PORT_EVENT_DISPLAY = 0;
inline void lvgl_port_task_wake(int, void*) {}
}

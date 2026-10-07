#pragma once
#include <cstdint>
constexpr int LVGL_PORT_EVENT_DISPLAY = 1;
bool lvgl_port_lock(uint32_t timeout);
void lvgl_port_unlock();
void lvgl_port_task_wake(int, void*);

#pragma once

#include "freertos/FreeRTOS.h"

inline constexpr int taskSCHEDULER_SUSPENDED = 0;
inline constexpr int taskSCHEDULER_RUNNING = 1;
int frontend_observer_scheduler_state();
void* frontend_observer_current_handle();
#define xTaskGetSchedulerState frontend_observer_scheduler_state
#define xTaskGetCurrentTaskHandle frontend_observer_current_handle

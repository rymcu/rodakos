#pragma once
#include "freertos/FreeRTOS.h"
inline constexpr int taskSCHEDULER_SUSPENDED = 0;
inline constexpr int taskSCHEDULER_RUNNING = 1;
int xTaskGetSchedulerState();
using TaskHandle_t = void*;
TaskHandle_t xTaskGetCurrentTaskHandle();

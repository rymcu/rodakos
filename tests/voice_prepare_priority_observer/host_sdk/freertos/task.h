#pragma once
#include <freertos/FreeRTOS.h>
TaskHandle_t xTaskGetCurrentTaskHandle();
UBaseType_t uxTaskPriorityGet(TaskHandle_t task);
const char* pcTaskGetName(TaskHandle_t task);

#pragma once
#include "FreeRTOS.h"
BaseType_t xTaskCreateWithCaps(void (*entry)(void*), const char*, uint32_t, void*,
                             uint32_t, TaskHandle_t*, uint32_t);
TaskHandle_t xTaskGetCurrentTaskHandle();
void vTaskDelay(TickType_t ticks);
void vTaskDeleteWithCaps(TaskHandle_t);

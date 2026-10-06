#pragma once
#include "freertos/FreeRTOS.h"
#include <cstddef>
BaseType_t xTaskCreateWithCaps(void (*entry)(void*), const char*, size_t, void*,
                              unsigned, TaskHandle_t*, unsigned);
TaskHandle_t xTaskGetCurrentTaskHandle();
void vTaskDelay(TickType_t);
void vTaskDeleteWithCaps(TaskHandle_t);

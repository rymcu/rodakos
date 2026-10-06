#pragma once
#include "FreeRTOS.h"
using TaskHandle_t = void*;
using TaskFunction_t = void (*)(void*);
BaseType_t xTaskCreate(TaskFunction_t function, const char*, uint32_t, void* argument,
                       uint32_t, TaskHandle_t* handle);
inline void vTaskDelete(TaskHandle_t) {}
void vTaskDelay(TickType_t ticks);

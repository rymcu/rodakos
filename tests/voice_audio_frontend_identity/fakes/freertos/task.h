#pragma once
#include "freertos/FreeRTOS.h"
using TaskHandle_t = void*;
using TaskFunction_t = void (*)(void*);
enum eTaskState { eRunning, eReady, eBlocked, eSuspended, eDeleted };
TaskHandle_t xTaskGetCurrentTaskHandle();
BaseType_t xTaskCreate(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*);
BaseType_t xTaskCreateWithCaps(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*, uint32_t);
BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*, BaseType_t, uint32_t);
void vTaskDelay(TickType_t);
void vTaskDelete(TaskHandle_t);
void vTaskDeleteWithCaps(TaskHandle_t);
void vTaskSuspend(TaskHandle_t);
eTaskState eTaskGetState(TaskHandle_t);
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t clear_count_on_exit, TickType_t ticks_to_wait);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t);

#pragma once
#include "freertos/FreeRTOS.h"
typedef enum { eRunning, eReady, eBlocked, eSuspended, eDeleted, eInvalid } eTaskState;
#ifdef __cplusplus
extern "C" {
#endif
BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t, const char*, configSTACK_DEPTH_TYPE,
    void*, UBaseType_t, TaskHandle_t*, BaseType_t, UBaseType_t);
BaseType_t xTaskCreateWithCaps(TaskFunction_t, const char*, configSTACK_DEPTH_TYPE,
    void*, UBaseType_t, TaskHandle_t*, UBaseType_t);
TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t, const char*, configSTACK_DEPTH_TYPE,
    void*, UBaseType_t, StackType_t*, StaticTask_t*, BaseType_t);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, configSTACK_DEPTH_TYPE,
    void*, UBaseType_t, TaskHandle_t*, BaseType_t);
BaseType_t xTaskCreate(TaskFunction_t, const char*, configSTACK_DEPTH_TYPE,
    void*, UBaseType_t, TaskHandle_t*);
eTaskState eTaskGetState(TaskHandle_t);
BaseType_t xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TaskHandle_t xTaskGetCurrentTaskHandleForCore(BaseType_t);
BaseType_t xTaskGetStaticBuffers(TaskHandle_t, StackType_t**, StaticTask_t**);
UBaseType_t uxTaskPriorityGet(TaskHandle_t);
BaseType_t xPortGetCoreID(void);
void vPortAssertIfInISR(void);
void vTaskDelay(TickType_t);
void vTaskSuspend(TaskHandle_t);
void vTaskDelete(TaskHandle_t);
void vTaskDeleteWithCaps(TaskHandle_t);
void retirement_host_yield(void);
#ifdef __cplusplus
}
#endif
#define taskYIELD() retirement_host_yield()

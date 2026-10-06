#pragma once

#include "FreeRTOS.h"

using TaskHandle_t = void*;
using TaskFunction_t = void (*)(void*);

// 音量测试不启动播放任务，也不依赖调度器时序。
inline BaseType_t xTaskCreate(TaskFunction_t, const char*, uint32_t, void*,
                             uint32_t, TaskHandle_t*) { return pdFAIL; }
inline void vTaskDelete(TaskHandle_t) {}
inline void vTaskDelay(TickType_t) {}

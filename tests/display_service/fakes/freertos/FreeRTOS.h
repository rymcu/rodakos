#pragma once
#include <cstddef>
#include <cstdint>
using BaseType_t = int;
using TickType_t = uint32_t;
using TaskHandle_t = void*;
constexpr BaseType_t pdTRUE = 1, pdFALSE = 0, pdPASS = 1, pdFAIL = 0;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
constexpr TickType_t pdMS_TO_TICKS(uint32_t milliseconds) { return milliseconds; }

#pragma once

#include <cstdint>

using BaseType_t = int;
using TickType_t = uint32_t;
using TaskHandle_t = void*;

constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t pdFAIL = 0;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr TickType_t portMAX_DELAY = 0xffffffffU;

#define pdMS_TO_TICKS(milliseconds) \
    (static_cast<TickType_t>((milliseconds) < 10 ? 0 : ((milliseconds) / 10)))

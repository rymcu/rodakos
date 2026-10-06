#pragma once
#include <cstdint>
using BaseType_t = int;
using UBaseType_t = unsigned;
using StackType_t = uint32_t;
using TickType_t = uint32_t;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t pdFAIL = 0;
constexpr TickType_t portMAX_DELAY = 0xffffffffU;
constexpr TickType_t pdMS_TO_TICKS(uint32_t value) { return value; }

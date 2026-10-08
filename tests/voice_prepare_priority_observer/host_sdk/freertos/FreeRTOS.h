#pragma once
#include <cstdint>
using UBaseType_t = uint32_t;
using TaskHandle_t = void*;
struct portMUX_TYPE { uint32_t unused; };
#define portMUX_INITIALIZER_UNLOCKED {0}
void PreparePriorityHostEnter(portMUX_TYPE*);
void PreparePriorityHostExit(portMUX_TYPE*);
#define portENTER_CRITICAL(mux) PreparePriorityHostEnter(mux)
#define portEXIT_CRITICAL(mux) PreparePriorityHostExit(mux)

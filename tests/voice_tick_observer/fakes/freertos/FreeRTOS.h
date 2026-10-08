#pragma once
#include <cstdint>
using UBaseType_t = unsigned;
struct portMUX_TYPE { uint32_t locked; uint32_t unused; };
#define portMUX_INITIALIZER_UNLOCKED {0, 0}
#define portMUX_TRY_LOCK 0
bool observer_try_lock(portMUX_TYPE*, int);
void observer_unlock(portMUX_TYPE*);
#define portTRY_ENTER_CRITICAL(m, t) observer_try_lock(m, t)
#define portTRY_ENTER_CRITICAL_ISR(m, t) observer_try_lock(m, t)
#define portEXIT_CRITICAL(m) observer_unlock(m)
#define portEXIT_CRITICAL_ISR(m) observer_unlock(m)

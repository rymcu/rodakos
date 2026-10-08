#pragma once

#include <cstdint>

using UBaseType_t = unsigned;
using BaseType_t = int;
struct FrontendObserverHostMux { uint32_t locked; uint32_t reserved; };
static_assert(sizeof(FrontendObserverHostMux) == 8);

bool frontend_observer_try_lock(FrontendObserverHostMux*, int);
void frontend_observer_lock(FrontendObserverHostMux*);
void frontend_observer_unlock(FrontendObserverHostMux*);

// Isolated names avoid merging functions/types with retirement's pthread-backed fake.
#define portMUX_TYPE FrontendObserverHostMux
#define portMUX_INITIALIZER_UNLOCKED {0, 0}
#define portMUX_TRY_LOCK 0
#define portTRY_ENTER_CRITICAL(m, t) frontend_observer_try_lock(m, t)
#define portTRY_ENTER_CRITICAL_ISR(m, t) frontend_observer_try_lock(m, t)
#define portENTER_CRITICAL(m) frontend_observer_lock(m)
#define portEXIT_CRITICAL(m) frontend_observer_unlock(m)
#define portEXIT_CRITICAL_ISR(m) frontend_observer_unlock(m)

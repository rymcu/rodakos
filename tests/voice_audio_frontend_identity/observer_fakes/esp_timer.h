#pragma once
#include <cstdint>
int64_t frontend_observer_now_us();
#define esp_timer_get_time frontend_observer_now_us

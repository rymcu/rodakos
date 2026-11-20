#pragma once
#include <cstdint>
// Microseconds; tests advance it through web_host_clock_us().
inline int64_t& web_host_clock_us() { static int64_t now = 1000000; return now; }
inline int64_t esp_timer_get_time() { return web_host_clock_us(); }

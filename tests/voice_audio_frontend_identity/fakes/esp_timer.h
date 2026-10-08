#pragma once
#include "observation_control.h"
inline int64_t esp_timer_get_time() {
    return rodakos_test::afe_observation::NowUs();
}

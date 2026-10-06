#pragma once
#include "i2s_types.h"
// Matches the pinned IDF 6.0.2 ESP32-S3 HAL. SOC_I2S_NUM was removed.
#define I2S_LL_GET(attribute) I2S_LL_##attribute
#define I2S_LL_INST_NUM 2

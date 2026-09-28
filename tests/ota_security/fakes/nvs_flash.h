#pragma once
#include "nvs.h"
inline int nvs_flash_init_partition(const char*) { return ESP_OK; }
inline int nvs_flash_init() { return ESP_OK; }

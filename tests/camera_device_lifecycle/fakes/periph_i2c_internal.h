#pragma once
#include "esp_err.h"
esp_err_t periph_i2c_get_effective_addr_internal(const char*, unsigned short*);
esp_err_t periph_i2c_set_effective_addr_internal(const char*, const char*, unsigned short);
esp_err_t periph_i2c_clear_effective_addr_internal(const char*);

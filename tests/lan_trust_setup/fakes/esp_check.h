#pragma once
#include "esp_log.h"
#define ESP_RETURN_ON_FALSE(a, err_code, log_tag, ...)     do { if (!(a)) { (void)(log_tag); return err_code; } } while (0)
#define ESP_GOTO_ON_FALSE(a, err_code, goto_tag, log_tag, ...)     do { if (!(a)) { (void)(log_tag); ret = err_code; goto goto_tag; } } while (0)

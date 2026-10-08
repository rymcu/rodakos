#pragma once
void observer_log(const char*, const char*, ...);
#define ESP_LOGI observer_log

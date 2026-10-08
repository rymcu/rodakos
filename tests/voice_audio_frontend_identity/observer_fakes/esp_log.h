#pragma once
void frontend_observer_log(const char*, const char*, ...);
#define ESP_LOGI frontend_observer_log

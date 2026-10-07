#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void retirement_host_log(const char*, const char*, ...);
#ifdef __cplusplus
}
#endif
#define ESP_LOGE(tag, format, ...) retirement_host_log(tag, format, ##__VA_ARGS__)

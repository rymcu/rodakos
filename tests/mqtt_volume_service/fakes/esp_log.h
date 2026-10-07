#pragma once

namespace mqtt_host {
void CaptureLog(char level, const char* tag, const char* format, ...)
    __attribute__((format(printf, 3, 4)));
}

#define ESP_LOGE(tag, ...) ::mqtt_host::CaptureLog('E', tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) ::mqtt_host::CaptureLog('W', tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) ::mqtt_host::CaptureLog('I', tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) ::mqtt_host::CaptureLog('D', tag, __VA_ARGS__)

#pragma once
#include "host_runtime.h"
#define ESP_LOGE(...) ::rodakos_test::display_host::CaptureLog(__VA_ARGS__)
#define ESP_LOGW(...) ::rodakos_test::display_host::CaptureLog(__VA_ARGS__)
#define ESP_LOGI(...) ::rodakos_test::display_host::CaptureLog(__VA_ARGS__)
#define ESP_LOGD(...) ::rodakos_test::display_host::CaptureLog(__VA_ARGS__)

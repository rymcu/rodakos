#pragma once
#ifdef __cplusplus
namespace rodakos_test::afe_fetch { void CaptureLog(char, const char*, const char*, ...); }
#define ESP_LOGE(...) ::rodakos_test::afe_fetch::CaptureLog('E', __VA_ARGS__)
#define ESP_LOGW(...) ::rodakos_test::afe_fetch::CaptureLog('W', __VA_ARGS__)
#define ESP_LOGI(...) ::rodakos_test::afe_fetch::CaptureLog('I', __VA_ARGS__)
#define ESP_LOGD(...) ::rodakos_test::afe_fetch::CaptureLog('D', __VA_ARGS__)
#else
#include <stdio.h>
#define ESP_LOGE(tag, ...) do { fprintf(stderr, "%s: ", tag); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGD(tag, ...) ((void)(tag))
#endif

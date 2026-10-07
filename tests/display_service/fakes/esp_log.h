#pragma once
namespace rodakos_test::display_service_host {
template <typename... T> void IgnoreLog(const char*, const char*, T&&...) {}
}
#define ESP_LOGI(tag, format, ...) ::rodakos_test::display_service_host::IgnoreLog(tag, format __VA_OPT__(,) __VA_ARGS__)
#define ESP_LOGW(tag, format, ...) ::rodakos_test::display_service_host::IgnoreLog(tag, format __VA_OPT__(,) __VA_ARGS__)
#define ESP_LOGE(tag, format, ...) ::rodakos_test::display_service_host::IgnoreLog(tag, format __VA_OPT__(,) __VA_ARGS__)

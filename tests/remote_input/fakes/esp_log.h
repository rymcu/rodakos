#pragma once
namespace rodakos_test {
template <typename... T>
inline void IgnoreRemoteInputLog(const char*, const char*, const T&...) {}
}
#define ESP_LOGI(...) ::rodakos_test::IgnoreRemoteInputLog(__VA_ARGS__)

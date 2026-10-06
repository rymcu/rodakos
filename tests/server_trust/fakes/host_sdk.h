#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

using esp_err_t = int;
using nvs_handle_t = unsigned;
constexpr int ESP_OK = 0;
constexpr int ESP_FAIL = -1;
constexpr size_t NVS_KEY_NAME_MAX_SIZE = 16;
constexpr int ESP_MAC_WIFI_STA = 0;
using wifi_second_chan_t = int;
constexpr int WIFI_SECOND_CHAN_NONE = 0;
inline const char* esp_err_to_name(int code) { return code == 0 ? "ESP_OK" : "ESP_FAIL"; }
inline int esp_read_mac(uint8_t* mac, int) {
    const uint8_t value[] = {0x44, 0x1b, 0xf6, 0xc3, 0xb4, 0x30};
    std::memcpy(mac, value, sizeof(value)); return 0;
}
inline void esp_fill_random(void* value, size_t size) { std::memset(value, 0xa5, size); }
inline int esp_wifi_get_home_channel(uint8_t*, wifi_second_chan_t*) { return 0; }
struct esp_app_desc_t { const char* version = "test"; };
inline const esp_app_desc_t* esp_app_get_description() { static esp_app_desc_t value; return &value; }
inline int64_t fake_clock_us = 100000;
inline int64_t esp_timer_get_time() { return ++fake_clock_us; }
inline void vTaskDelay(unsigned) {}
#define pdMS_TO_TICKS(value) (value)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
inline int esp_crt_bundle_attach(void*) { return 0; }
enum esp_http_client_method_t { HTTP_METHOD_GET, HTTP_METHOD_POST };
struct esp_http_client_config_t {
    const char* url = nullptr;
    esp_http_client_method_t method = HTTP_METHOD_GET;
    int timeout_ms = 0;
    int buffer_size = 0;
    int buffer_size_tx = 0;
    const char* user_agent = nullptr;
    const char* cert_pem = nullptr;
    size_t cert_len = 0;
    const char* common_name = nullptr;
    bool disable_auto_redirect = false;
    bool skip_cert_common_name_check = false;
    bool use_global_ca_store = false;
    int (*crt_bundle_attach)(void*) = nullptr;
};
struct FakeHttp;
using esp_http_client_handle_t = FakeHttp*;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*);
int esp_http_client_set_timeout_ms(esp_http_client_handle_t, int);
int esp_http_client_set_header(esp_http_client_handle_t, const char*, const char*);
int esp_http_client_open(esp_http_client_handle_t, int);
int esp_http_client_write(esp_http_client_handle_t, const char*, int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read_response(esp_http_client_handle_t, char*, int);
int esp_http_client_read(esp_http_client_handle_t, char*, int);
int esp_http_client_close(esp_http_client_handle_t);
int esp_http_client_cleanup(esp_http_client_handle_t);

#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

using esp_err_t = int;
inline constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_STATE = -2,
    ESP_ERR_NOT_FOUND = -3, ESP_ERR_NO_MEM = -4, ESP_ERR_WIFI_NOT_CONNECT = -5,
    ESP_ERR_WIFI_NOT_INIT = -6, ESP_ERR_WIFI_STATE = -7, ESP_ERR_NVS_NO_FREE_PAGES = -8,
    ESP_ERR_NVS_NEW_VERSION_FOUND = -9, ESP_ERR_WIFI_NOT_STARTED = -10;
using esp_event_base_t = const char*;
using esp_event_handler_instance_t = void*;
using esp_event_handler_t = void (*)(void*, esp_event_base_t, int32_t, void*);
extern esp_event_base_t WIFI_EVENT;
extern esp_event_base_t IP_EVENT;
#define ESP_EVENT_DEFINE_BASE(name) esp_event_base_t name = #name
inline constexpr int ESP_EVENT_ANY_ID = -1, WIFI_EVENT_STA_START = 1,
    WIFI_EVENT_STA_CONNECTED = 2, WIFI_EVENT_STA_DISCONNECTED = 3, WIFI_EVENT_SCAN_DONE = 4,
    IP_EVENT_STA_GOT_IP = 5, IP_EVENT_STA_LOST_IP = 6, WIFI_MODE_STA = 1,
    WIFI_IF_STA = 1, WIFI_SCAN_TYPE_ACTIVE = 1, WIFI_AUTH_OPEN = 0;
struct esp_netif_t {};
struct esp_ip4_addr_t { uint32_t addr = 0; };
struct esp_netif_ip_info_t { esp_ip4_addr_t ip; };
struct ip_event_got_ip_t { esp_netif_t* esp_netif = nullptr; esp_netif_ip_info_t ip_info; };
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) ((ip)->addr >> 24), (((ip)->addr >> 16) & 255), (((ip)->addr >> 8) & 255), ((ip)->addr & 255)
struct wifi_init_config_t {};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
struct wifi_config_t { struct { uint8_t ssid[32]{}; uint8_t password[64]{}; } sta; };
struct wifi_scan_config_t { bool show_hidden = false; int scan_type = 0; };
struct wifi_ap_record_t { uint8_t ssid[33]{}; int8_t rssi = -40; uint8_t authmode = 0; };
struct wifi_event_sta_connected_t { uint8_t ssid[32]{}; uint8_t ssid_len = 0; };
using wifi_event_sta_disconnected_t = wifi_event_sta_connected_t;
struct wifi_event_sta_scan_done_t { uint32_t status = 0; };
using TickType_t = uint32_t;
using BaseType_t = int;
using TaskHandle_t = void*;
struct HostTimer;
using TimerHandle_t = HostTimer*;
using TimerCallbackFunction_t = void (*)(TimerHandle_t);
using PendedFunction_t = void (*)(void*, uint32_t);
inline constexpr BaseType_t pdPASS = 1, pdTRUE = 1;
inline constexpr TickType_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(ms) (ms)
const char* esp_err_to_name(esp_err_t);
esp_err_t nvs_flash_init();
esp_err_t nvs_flash_erase();
esp_err_t esp_netif_init();
esp_err_t esp_event_loop_create_default();
esp_netif_t* esp_netif_create_default_wifi_sta();
void esp_netif_destroy_default_wifi(void*);
esp_err_t esp_netif_get_ip_info(esp_netif_t*, esp_netif_ip_info_t*);
esp_err_t esp_wifi_init(const wifi_init_config_t*);
esp_err_t esp_wifi_deinit();
esp_err_t esp_wifi_start();
esp_err_t esp_wifi_stop();
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int, const wifi_config_t*);
esp_err_t esp_wifi_connect();
esp_err_t esp_wifi_disconnect();
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t*);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*, bool);
esp_err_t esp_wifi_scan_stop();
esp_err_t esp_wifi_clear_ap_list();
esp_err_t esp_wifi_scan_get_ap_num(uint16_t*);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t*, wifi_ap_record_t*);
esp_err_t esp_event_handler_instance_register(esp_event_base_t, int32_t, esp_event_handler_t,
                                             void*, esp_event_handler_instance_t*);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t, int32_t, esp_event_handler_instance_t);
esp_err_t esp_event_post(esp_event_base_t, int32_t, const void*, size_t, TickType_t);
int64_t esp_timer_get_time();
TaskHandle_t xTaskGetCurrentTaskHandle();
void vTaskDelay(TickType_t);
TimerHandle_t xTimerCreate(const char*, TickType_t, BaseType_t, void*, TimerCallbackFunction_t);
BaseType_t xTimerStart(TimerHandle_t, TickType_t);
BaseType_t xTimerDelete(TimerHandle_t, TickType_t);
BaseType_t xTimerPendFunctionCall(PendedFunction_t, void*, uint32_t, TickType_t);
void* pvTimerGetTimerID(TimerHandle_t);

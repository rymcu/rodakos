#include "rodakos_adapters/wifi_adapter.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <string.h>

static const char* TAG = "WiFiAdapter";

/**
 * ESP32-S3 WiFi 适配器实现
 */
class ESP32WiFiAdapter : public WiFiAdapter {
public:
    ESP32WiFiAdapter() = default;
    ~ESP32WiFiAdapter() override { Deinit(); }

    bool Init() override;
    void Deinit() override;
    bool StartScan(std::function<void(const std::vector<WiFiScanResult>&)> callback) override;
    bool Connect(const std::string& ssid,
                const std::string& password,
                std::function<void(WiFiStatus)> callback) override;
    void Disconnect() override;
    WiFiStatus GetStatus() const override { return status_; }
    std::string GetConnectedSSID() const override { return connected_ssid_; }
    std::string GetIPAddress() const override { return ip_address_; }

private:
    static void WiFiEventHandler(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data);
    void HandleWiFiEvent(esp_event_base_t event_base, int32_t event_id, void* event_data);

    bool initialized_ = false;
    WiFiStatus status_ = WiFiStatus::kDisconnected;
    std::string connected_ssid_;
    std::string ip_address_;
    std::function<void(const std::vector<WiFiScanResult>&)> scan_callback_;
    std::function<void(WiFiStatus)> connect_callback_;
    int retry_count_ = 0;
    static constexpr int kMaxRetries = 3;
};

bool ESP32WiFiAdapter::Init() {
    if (initialized_) {
        return true;
    }

    // 初始化 NVS（WiFi 需要）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化网络接口
    ESP_ERROR_CHECK(esp_netif_init());

    // 创建默认事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 创建默认 WiFi STA
    esp_netif_create_default_wifi_sta();

    // WiFi 初始化配置
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理器
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &ESP32WiFiAdapter::WiFiEventHandler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &ESP32WiFiAdapter::WiFiEventHandler, this, nullptr));

    // 设置 WiFi 模式为 STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    initialized_ = true;
    ESP_LOGI(TAG, "WiFi initialized");
    return true;
}

void ESP32WiFiAdapter::Deinit() {
    if (!initialized_) {
        return;
    }

    esp_wifi_stop();
    esp_wifi_deinit();
    initialized_ = false;
    ESP_LOGI(TAG, "WiFi deinitialized");
}

bool ESP32WiFiAdapter::StartScan(std::function<void(const std::vector<WiFiScanResult>&)> callback) {
    if (!initialized_) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return false;
    }

    scan_callback_ = callback;

    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    esp_err_t err = esp_wifi_scan_start(&scan_config, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "WiFi scan started");
    return true;
}

bool ESP32WiFiAdapter::Connect(const std::string& ssid,
                               const std::string& password,
                               std::function<void(WiFiStatus)> callback) {
    if (!initialized_) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return false;
    }

    connect_callback_ = callback;
    retry_count_ = 0;
    status_ = WiFiStatus::kConnecting;

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    if (!password.empty()) {
        strncpy((char*)wifi_config.sta.password, password.c_str(), sizeof(wifi_config.sta.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid.c_str());
    return true;
}

void ESP32WiFiAdapter::Disconnect() {
    if (initialized_ && status_ != WiFiStatus::kDisconnected) {
        esp_wifi_disconnect();
        status_ = WiFiStatus::kDisconnected;
        connected_ssid_.clear();
        ip_address_.clear();
        ESP_LOGI(TAG, "WiFi disconnected");
    }
}

void ESP32WiFiAdapter::WiFiEventHandler(void* arg, esp_event_base_t event_base,
                                       int32_t event_id, void* event_data) {
    auto* self = static_cast<ESP32WiFiAdapter*>(arg);
    self->HandleWiFiEvent(event_base, event_id, event_data);
}

void ESP32WiFiAdapter::HandleWiFiEvent(esp_event_base_t event_base,
                                      int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started");
                break;

            case WIFI_EVENT_STA_CONNECTED: {
                auto* event = static_cast<wifi_event_sta_connected_t*>(event_data);
                connected_ssid_ = std::string((char*)event->ssid, event->ssid_len);
                ESP_LOGI(TAG, "Connected to WiFi: %s", connected_ssid_.c_str());
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED:
                status_ = WiFiStatus::kDisconnected;
                connected_ssid_.clear();
                ip_address_.clear();

                if (retry_count_ < kMaxRetries && connect_callback_) {
                    retry_count_++;
                    ESP_LOGI(TAG, "Retrying connection (%d/%d)", retry_count_, kMaxRetries);
                    esp_wifi_connect();
                } else {
                    ESP_LOGI(TAG, "WiFi disconnected");
                    status_ = WiFiStatus::kFailed;
                    if (connect_callback_) {
                        connect_callback_(WiFiStatus::kFailed);
                    }
                }
                break;

            case WIFI_EVENT_SCAN_DONE: {
                uint16_t ap_count = 0;
                esp_wifi_scan_get_ap_num(&ap_count);

                if (ap_count == 0) {
                    ESP_LOGI(TAG, "No APs found");
                    if (scan_callback_) {
                        scan_callback_({});
                    }
                    return;
                }

                std::vector<wifi_ap_record_t> ap_records(ap_count);
                ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records.data()));

                std::vector<WiFiScanResult> results;
                for (const auto& ap : ap_records) {
                    WiFiScanResult result;
                    result.ssid = std::string((char*)ap.ssid);
                    result.rssi = ap.rssi;
                    result.auth_mode = ap.authmode;
                    result.is_secured = (ap.authmode != WIFI_AUTH_OPEN);
                    results.push_back(result);
                }

                ESP_LOGI(TAG, "Scan complete, found %d APs", ap_count);
                if (scan_callback_) {
                    scan_callback_(results);
                }
                break;
            }

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ip_address_ = ip_str;
        status_ = WiFiStatus::kConnected;

        ESP_LOGI(TAG, "Got IP address: %s", ip_address_.c_str());
        if (connect_callback_) {
            connect_callback_(WiFiStatus::kConnected);
        }
    }
}

// 工厂函数
WiFiAdapter* CreateWiFiAdapter() {
    return new ESP32WiFiAdapter();
}

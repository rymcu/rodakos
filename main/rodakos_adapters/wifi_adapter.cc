#include "rodakos_adapters/wifi_adapter.h"
#include "rodakos_adapters/wifi_connection_policy.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

static const char* TAG = "WiFiAdapter";

namespace {

// esp_event runs handlers synchronously on its event task.  This flag is used
// to reject waits that would require that same task to dispatch another event.
thread_local bool g_in_wifi_event_handler = false;

constexpr uint32_t kConnectDisconnectTimeoutMs = 2000;

bool EventSSIDMatchesOrUnknown(const std::string& target_ssid,
                               const uint8_t* event_ssid,
                               size_t event_ssid_length) {
    if (event_ssid == nullptr) {
        return false;
    }
    // Some IDF disconnect paths omit the SSID.  The driver-state verification
    // performed after this match is then the authority.
    if (event_ssid_length == 0) {
        return true;
    }
    return WiFiEventMatchesTargetSSID(target_ssid, event_ssid, event_ssid_length);
}

bool IsExplicitlyDisconnected(esp_err_t error) {
    // Do not treat an unknown/control-block error as proof that an in-flight
    // station operation has stopped.
    return error == ESP_ERR_WIFI_NOT_CONNECT;
}

}  // namespace

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
    bool DisconnectAndWait(uint32_t timeout_ms) override;
    WiFiStatus GetStatus() const override;
    std::string GetConnectedSSID() const override;
    std::string GetIPAddress() const override;

private:
    static void WiFiEventHandler(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data);
    void HandleWiFiEvent(esp_event_base_t event_base, int32_t event_id, void* event_data);

    // Public calls are serialized separately from state protection.  No
    // esp_wifi_* call is made while state_mutex_ is held; api_mutex_ is the
    // single owner lock for the driver API and is also used by event retries.
    std::mutex operation_mutex_;
    mutable std::mutex state_mutex_;
    std::mutex api_mutex_;
    std::condition_variable disconnect_condition_;
    std::condition_variable event_idle_condition_;

    bool initialized_ = false;
    bool wifi_driver_initialized_ = false;
    bool wifi_driver_started_ = false;
    void* sta_netif_ = nullptr;
    esp_event_handler_instance_t wifi_event_instance_ = nullptr;
    esp_event_handler_instance_t ip_event_instance_ = nullptr;

    WiFiStatus status_ = WiFiStatus::kDisconnected;
    uint32_t connection_generation_ = 0;
    uint32_t sta_connected_generation_ = 0;
    bool connect_command_pending_ = false;
    bool connect_callback_notified_ = false;
    std::string target_ssid_;
    std::string connected_ssid_;
    std::string ip_address_;
    std::function<void(WiFiStatus)> connect_callback_;
    int retry_count_ = 0;
    static constexpr int kMaxRetries = 3;

    bool disconnect_pending_ = false;
    bool disconnect_command_pending_ = false;
    bool disconnect_event_seen_ = false;
    bool disconnect_was_associated_ = false;
    bool disconnect_verify_in_flight_ = false;
    uint32_t disconnect_event_serial_ = 0;
    std::string disconnect_target_ssid_;

    bool scan_pending_ = false;
    bool scan_command_pending_ = false;
    bool scan_processing_ = false;
    bool scan_event_seen_ = false;
    uint32_t scan_generation_ = 0;
    std::function<void(const std::vector<WiFiScanResult>&)> scan_callback_;

    std::atomic<void*> event_task_handle_{nullptr};
    size_t event_callbacks_in_flight_ = 0;

    uint32_t NextConnectionGenerationLocked();
    uint32_t NextScanGenerationLocked();
    bool IsEventTaskContext() const;
    bool HasActiveAttemptLocked() const;
    void CompleteDisconnectLocked();
    void CancelPendingScan();
    bool DisconnectAndWaitWithOperation(uint32_t timeout_ms);
    bool StartDisconnectWithoutOperation();
    void VerifyDisconnect(uint32_t generation, uint32_t event_serial);
    void ProcessScanDone(const wifi_event_sta_scan_done_t* event,
                         uint32_t expected_generation = 0);
};

uint32_t ESP32WiFiAdapter::NextConnectionGenerationLocked() {
    ++connection_generation_;
    if (connection_generation_ == 0) {
        ++connection_generation_;
    }
    return connection_generation_;
}

uint32_t ESP32WiFiAdapter::NextScanGenerationLocked() {
    ++scan_generation_;
    if (scan_generation_ == 0) {
        ++scan_generation_;
    }
    return scan_generation_;
}

bool ESP32WiFiAdapter::IsEventTaskContext() const {
    if (g_in_wifi_event_handler) {
        return true;
    }
    void* event_task = event_task_handle_.load(std::memory_order_acquire);
    return event_task != nullptr &&
           event_task == static_cast<void*>(xTaskGetCurrentTaskHandle());
}

bool ESP32WiFiAdapter::HasActiveAttemptLocked() const {
    return status_ != WiFiStatus::kDisconnected ||
           connect_command_pending_ ||
           sta_connected_generation_ != 0 ||
           !target_ssid_.empty() ||
           !connected_ssid_.empty() ||
           static_cast<bool>(connect_callback_);
}

void ESP32WiFiAdapter::CompleteDisconnectLocked() {
    disconnect_pending_ = false;
    disconnect_command_pending_ = false;
    disconnect_event_seen_ = false;
    disconnect_was_associated_ = false;
    disconnect_verify_in_flight_ = false;
    disconnect_target_ssid_.clear();
    status_ = WiFiStatus::kDisconnected;
    connect_command_pending_ = false;
    connect_callback_notified_ = false;
    sta_connected_generation_ = 0;
    target_ssid_.clear();
    connected_ssid_.clear();
    ip_address_.clear();
    connect_callback_ = nullptr;
    retry_count_ = kMaxRetries;
    disconnect_condition_.notify_all();
}

bool ESP32WiFiAdapter::Init() {
    std::unique_lock<std::mutex> operation_lock(operation_mutex_);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (initialized_) {
            return true;
        }
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ret = nvs_flash_erase();
        if (ret == ESP_OK) {
            ret = nvs_flash_init();
        }
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Network interface initialization failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Default event loop initialization failed: %s", esp_err_to_name(ret));
        return false;
    }

    esp_netif_t* sta_netif = nullptr;
    {
        std::lock_guard<std::mutex> api_lock(api_mutex_);
        sta_netif = esp_netif_create_default_wifi_sta();
    }
    if (sta_netif == nullptr) {
        ESP_LOGE(TAG, "Failed to create default WiFi STA interface");
        return false;
    }

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    {
        std::lock_guard<std::mutex> api_lock(api_mutex_);
        ret = esp_wifi_init(&wifi_init);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi driver initialization failed: %s", esp_err_to_name(ret));
        {
            std::lock_guard<std::mutex> api_lock(api_mutex_);
            esp_netif_destroy_default_wifi(sta_netif);
        }
        return false;
    }

    esp_event_handler_instance_t wifi_instance = nullptr;
    esp_event_handler_instance_t ip_instance = nullptr;
    ret = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &ESP32WiFiAdapter::WiFiEventHandler,
        this, &wifi_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi event registration failed: %s", esp_err_to_name(ret));
        {
            std::lock_guard<std::mutex> api_lock(api_mutex_);
            (void)esp_wifi_deinit();
            esp_netif_destroy_default_wifi(sta_netif);
        }
        return false;
    }

    // Register all IP events so DHCP renewals and LOST_IP can be associated
    // with the same station generation.
    ret = esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, &ESP32WiFiAdapter::WiFiEventHandler,
        this, &ip_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IP event registration failed: %s", esp_err_to_name(ret));
        (void)esp_event_handler_instance_unregister(
            WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_instance);
        {
            std::lock_guard<std::mutex> api_lock(api_mutex_);
            (void)esp_wifi_deinit();
            esp_netif_destroy_default_wifi(sta_netif);
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> api_lock(api_mutex_);
        ret = esp_wifi_set_mode(WIFI_MODE_STA);
        if (ret == ESP_OK) {
            ret = esp_wifi_start();
        }
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi driver: %s", esp_err_to_name(ret));
        (void)esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ip_instance);
        (void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_instance);
        {
            std::lock_guard<std::mutex> api_lock(api_mutex_);
            (void)esp_wifi_deinit();
            esp_netif_destroy_default_wifi(sta_netif);
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        sta_netif_ = sta_netif;
        wifi_event_instance_ = wifi_instance;
        ip_event_instance_ = ip_instance;
        wifi_driver_initialized_ = true;
        wifi_driver_started_ = true;
        initialized_ = true;
        status_ = WiFiStatus::kDisconnected;
        sta_connected_generation_ = 0;
        connect_command_pending_ = false;
        connect_callback_notified_ = false;
        disconnect_pending_ = false;
        disconnect_command_pending_ = false;
        disconnect_event_seen_ = false;
        disconnect_was_associated_ = false;
        disconnect_verify_in_flight_ = false;
        target_ssid_.clear();
        disconnect_target_ssid_.clear();
        connected_ssid_.clear();
        ip_address_.clear();
        scan_pending_ = false;
        scan_command_pending_ = false;
        scan_processing_ = false;
        scan_event_seen_ = false;
        scan_callback_ = nullptr;
        connect_callback_ = nullptr;
        retry_count_ = 0;
        event_task_handle_.store(nullptr, std::memory_order_release);
    }

    ESP_LOGI(TAG, "WiFi initialized");
    return true;
}

void ESP32WiFiAdapter::Deinit() {
    const bool called_from_event_task = IsEventTaskContext();
    std::unique_lock<std::mutex> operation_lock(operation_mutex_);
    // Reserve the driver owner before invalidating state.  An event retry
    // holding api_mutex_ must finish before Deinit publishes the new epoch.
    std::unique_lock<std::mutex> api_lock(api_mutex_);

    esp_event_handler_instance_t wifi_instance = nullptr;
    esp_event_handler_instance_t ip_instance = nullptr;
    void* sta_netif = nullptr;
    bool driver_initialized = false;
    bool driver_started = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!initialized_ && !wifi_driver_initialized_ &&
            wifi_event_instance_ == nullptr && ip_event_instance_ == nullptr) {
            return;
        }

        // Invalidate queued events before unregistering handlers.
        initialized_ = false;
        (void)NextConnectionGenerationLocked();
        sta_connected_generation_ = 0;
        connect_command_pending_ = false;
        disconnect_pending_ = false;
        disconnect_command_pending_ = false;
        disconnect_event_seen_ = false;
        disconnect_was_associated_ = false;
        disconnect_verify_in_flight_ = false;
        target_ssid_.clear();
        disconnect_target_ssid_.clear();
        connected_ssid_.clear();
        ip_address_.clear();
        scan_pending_ = false;
        scan_command_pending_ = false;
        scan_processing_ = false;
        scan_event_seen_ = false;
        scan_callback_ = nullptr;
        connect_callback_ = nullptr;
        status_ = WiFiStatus::kDisconnected;
        wifi_instance = wifi_event_instance_;
        ip_instance = ip_event_instance_;
        wifi_event_instance_ = nullptr;
        ip_event_instance_ = nullptr;
        sta_netif = sta_netif_;
        sta_netif_ = nullptr;
        driver_initialized = wifi_driver_initialized_;
        driver_started = wifi_driver_started_;
        wifi_driver_initialized_ = false;
        wifi_driver_started_ = false;
        disconnect_condition_.notify_all();
    }

    api_lock.unlock();

    if (ip_instance != nullptr) {
        const esp_err_t err = esp_event_handler_instance_unregister(
            IP_EVENT, ESP_EVENT_ANY_ID, ip_instance);
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to unregister IP event handler: %s", esp_err_to_name(err));
        }
    }
    if (wifi_instance != nullptr) {
        const esp_err_t err = esp_event_handler_instance_unregister(
            WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_instance);
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to unregister WiFi event handler: %s", esp_err_to_name(err));
        }
    }

    // Handler unregistration is queued by esp_event.  Keep a heap adapter
    // alive until all callbacks already in flight have returned.
    if (!called_from_event_task) {
        std::unique_lock<std::mutex> lock(state_mutex_);
        event_idle_condition_.wait(lock, [this]() {
            return event_callbacks_in_flight_ == 0;
        });
    }

    api_lock.lock();
    {
        if (driver_started) {
            const esp_err_t err = esp_wifi_stop();
            if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
                ESP_LOGW(TAG, "Failed to stop WiFi: %s", esp_err_to_name(err));
            }
        }
        if (driver_initialized) {
            const esp_err_t err = esp_wifi_deinit();
            if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
                ESP_LOGW(TAG, "Failed to deinitialize WiFi: %s", esp_err_to_name(err));
            }
        }
    }
    if (sta_netif != nullptr) {
        esp_netif_destroy_default_wifi(sta_netif);
    }
    api_lock.unlock();
    event_task_handle_.store(nullptr, std::memory_order_release);
    ESP_LOGI(TAG, "WiFi deinitialized");
}

void ESP32WiFiAdapter::CancelPendingScan() {
    std::lock_guard<std::mutex> api_lock(api_mutex_);
    bool stop_scan = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!scan_pending_ && !scan_processing_) {
            return;
        }
        stop_scan = true;
        scan_pending_ = false;
        scan_command_pending_ = false;
        scan_processing_ = false;
        scan_event_seen_ = false;
        scan_callback_ = nullptr;
        (void)NextScanGenerationLocked();
    }

    if (stop_scan) {
        const esp_err_t err = esp_wifi_scan_stop();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT &&
            err != ESP_ERR_WIFI_NOT_STARTED) {
            ESP_LOGD(TAG, "WiFi scan stop returned: %s", esp_err_to_name(err));
        }
    }
}

bool ESP32WiFiAdapter::StartScan(
    std::function<void(const std::vector<WiFiScanResult>&)> callback) {
    bool process_deferred_event = false;
    uint32_t generation = 0;
    esp_err_t err = ESP_OK;
    {
        std::unique_lock<std::mutex> operation_lock(operation_mutex_);
        std::lock_guard<std::mutex> api_lock(api_mutex_);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!initialized_) {
                ESP_LOGE(TAG, "WiFi not initialized");
                return false;
            }
            if (scan_pending_ || scan_processing_) {
                ESP_LOGW(TAG, "WiFi scan already in progress");
                return false;
            }
            if (disconnect_pending_ || status_ == WiFiStatus::kConnecting ||
                connect_command_pending_) {
                ESP_LOGW(TAG, "Cannot scan while WiFi operation is in progress");
                return false;
            }
            generation = NextScanGenerationLocked();
            scan_pending_ = true;
            scan_command_pending_ = true;
            scan_event_seen_ = false;
            scan_callback_ = std::move(callback);
        }

        wifi_scan_config_t scan_config = {};
        scan_config.show_hidden = false;
        scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        {
            err = esp_wifi_scan_start(&scan_config, false);
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (scan_generation_ == generation && scan_pending_) {
                scan_command_pending_ = false;
                if (err != ESP_OK) {
                    scan_pending_ = false;
                    scan_event_seen_ = false;
                    scan_callback_ = nullptr;
                } else if (scan_event_seen_) {
                    process_deferred_event = true;
                }
            }
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "WiFi scan started");
    if (process_deferred_event) {
        ProcessScanDone(nullptr, generation);
    }
    return true;
}

bool ESP32WiFiAdapter::Connect(const std::string& ssid,
                               const std::string& password,
                               std::function<void(WiFiStatus)> callback) {
    wifi_config_t wifi_config = {};
    if (ssid.empty() || ssid.size() > sizeof(wifi_config.sta.ssid) ||
        password.size() >= sizeof(wifi_config.sta.password)) {
        ESP_LOGE(TAG, "WiFi credentials exceed ESP-IDF limits");
        return false;
    }

    // Preserve all 32 SSID octets.  STA events carry an explicit length, so
    // truncating to 31 bytes would reject a legal full-length network.
    memcpy(wifi_config.sta.ssid, ssid.data(), ssid.size());
    if (!password.empty()) {
        memcpy(wifi_config.sta.password, password.data(), password.size());
    }

    std::unique_lock<std::mutex> operation_lock(operation_mutex_);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!initialized_) {
            ESP_LOGE(TAG, "WiFi not initialized");
            return false;
        }
    }

    // A prior attempt (including a timed-out barrier) must settle before IDF
    // accepts a new station configuration.
    if (!DisconnectAndWaitWithOperation(kConnectDisconnectTimeoutMs)) {
        ESP_LOGW(TAG, "Cannot start WiFi connection before prior disconnect settles");
        return false;
    }
    CancelPendingScan();

    uint32_t generation = 0;
    esp_err_t err = ESP_OK;
    {
        std::lock_guard<std::mutex> api_lock(api_mutex_);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!initialized_ || disconnect_pending_) {
                return false;
            }
            generation = NextConnectionGenerationLocked();
            sta_connected_generation_ = 0;
            connect_command_pending_ = true;
            connect_callback_notified_ = false;
            target_ssid_ = ssid;
            connected_ssid_.clear();
            ip_address_.clear();
            disconnect_target_ssid_.clear();
            connect_callback_ = std::move(callback);
            retry_count_ = 0;
            status_ = WiFiStatus::kConnecting;
        }
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if (err == ESP_OK) {
            err = esp_wifi_connect();
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (connection_generation_ == generation) {
                connect_command_pending_ = false;
                if (err != ESP_OK) {
                    status_ = WiFiStatus::kFailed;
                    connect_callback_ = nullptr;
                }
            }
        }
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start WiFi connection: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid.c_str());
    return true;
}

void ESP32WiFiAdapter::Disconnect() {
    (void)DisconnectAndWait(0);
}

bool ESP32WiFiAdapter::StartDisconnectWithoutOperation() {
    CancelPendingScan();
    std::lock_guard<std::mutex> api_lock(api_mutex_);
    bool need_issue = false;
    uint32_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!initialized_) {
            return true;
        }
        if (!disconnect_pending_ && !HasActiveAttemptLocked()) {
            return true;
        }
        if (!disconnect_pending_) {
            disconnect_was_associated_ =
                status_ == WiFiStatus::kConnected ||
                sta_connected_generation_ != 0 ||
                !connected_ssid_.empty();
            generation = NextConnectionGenerationLocked();
            sta_connected_generation_ = 0;
            disconnect_target_ssid_ = connected_ssid_.empty() ? target_ssid_ : connected_ssid_;
            disconnect_pending_ = true;
            disconnect_command_pending_ = true;
            disconnect_event_seen_ = false;
            status_ = WiFiStatus::kDisconnected;
            connect_command_pending_ = false;
            connect_callback_ = nullptr;
            retry_count_ = kMaxRetries;
            target_ssid_.clear();
            connected_ssid_.clear();
            ip_address_.clear();
            need_issue = true;
        } else {
            generation = connection_generation_;
            disconnect_command_pending_ = true;
            need_issue = true;
        }
    }

    if (!need_issue) {
        return false;
    }
    const esp_err_t err = esp_wifi_disconnect();
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (disconnect_pending_ && connection_generation_ == generation) {
            disconnect_command_pending_ = false;
            if (IsExplicitlyDisconnected(err)) {
                CompleteDisconnectLocked();
            }
        }
    }
    if (err != ESP_OK && !IsExplicitlyDisconnected(err)) {
        ESP_LOGW(TAG, "Failed to disconnect WiFi: %s", esp_err_to_name(err));
    }
    return IsExplicitlyDisconnected(err);
}

void ESP32WiFiAdapter::VerifyDisconnect(uint32_t generation, uint32_t event_serial) {
    std::lock_guard<std::mutex> api_lock(api_mutex_);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!initialized_ || !disconnect_pending_ ||
            connection_generation_ != generation ||
            disconnect_verify_in_flight_) {
            return;
        }
        disconnect_verify_in_flight_ = true;
    }

    esp_err_t ap_error = ESP_FAIL;
    esp_err_t disconnect_error = ESP_OK;
    wifi_ap_record_t ap_info = {};
    ap_error = esp_wifi_sta_get_ap_info(&ap_info);
    if (ap_error == ESP_OK) {
        // A same-SSID event may be stale while the station is still
        // associated.  Re-arm the asynchronous disconnect and keep the
        // barrier pending.
        disconnect_error = esp_wifi_disconnect();
    }
    const bool driver_disconnected = IsExplicitlyDisconnected(ap_error) ||
                                     IsExplicitlyDisconnected(disconnect_error);

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        disconnect_verify_in_flight_ = false;
        if (!initialized_ || !disconnect_pending_ ||
            connection_generation_ != generation) {
            return;
        }
        // A newer event still belongs to this barrier; a direct driver query
        // proving NOT_CONNECT is sufficient even if its serial changed.
        if (driver_disconnected) {
            CompleteDisconnectLocked();
        } else if (event_serial != disconnect_event_serial_) {
            // Leave the barrier armed.  The newer event will schedule another
            // verification, or the next explicit wait will retry it.
            disconnect_event_seen_ = true;
        }
    }
}

bool ESP32WiFiAdapter::DisconnectAndWaitWithOperation(uint32_t timeout_ms) {
    if (timeout_ms > 0 && IsEventTaskContext()) {
        ESP_LOGW(TAG, "Cannot synchronously wait for disconnect from WiFi event task");
        return false;
    }

    CancelPendingScan();

    uint32_t generation = 0;
    uint32_t event_serial = 0;
    bool issue_disconnect = false;
    bool verify_after_command = false;
    esp_err_t err = ESP_OK;
    {
        // Reserve the driver command before publishing a new generation.  A
        // retry handler holding api_mutex_ cannot then be overtaken by a new
        // Connect/Disconnect state transition.
        std::lock_guard<std::mutex> api_lock(api_mutex_);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!initialized_) {
                return true;
            }
            if (!disconnect_pending_ && !HasActiveAttemptLocked()) {
                return true;
            }

            if (!disconnect_pending_) {
                disconnect_was_associated_ =
                    status_ == WiFiStatus::kConnected ||
                    sta_connected_generation_ != 0 ||
                    !connected_ssid_.empty();
                generation = NextConnectionGenerationLocked();
                sta_connected_generation_ = 0;
                disconnect_target_ssid_ = connected_ssid_.empty() ? target_ssid_ : connected_ssid_;
                disconnect_pending_ = true;
                disconnect_event_seen_ = false;
                disconnect_command_pending_ = true;
                status_ = WiFiStatus::kDisconnected;
                connect_command_pending_ = false;
                connect_callback_ = nullptr;
                retry_count_ = kMaxRetries;
                target_ssid_.clear();
                connected_ssid_.clear();
                ip_address_.clear();
            } else {
                generation = connection_generation_;
                disconnect_command_pending_ = true;
            }
            event_serial = disconnect_event_serial_;
            issue_disconnect = true;
        }

        if (issue_disconnect) {
            err = esp_wifi_disconnect();
        }
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (disconnect_pending_ && connection_generation_ == generation) {
                disconnect_command_pending_ = false;
                if (IsExplicitlyDisconnected(err)) {
                    CompleteDisconnectLocked();
                } else {
                    verify_after_command = disconnect_event_seen_;
                }
            }
        }
    }
    if (issue_disconnect) {
        if (err != ESP_OK && !IsExplicitlyDisconnected(err)) {
            ESP_LOGW(TAG, "Failed to disconnect WiFi: %s", esp_err_to_name(err));
            return false;
        }
        if (IsExplicitlyDisconnected(err)) {
            return true;
        }
        if (verify_after_command) {
            VerifyDisconnect(generation, event_serial);
        }
    }

    if (timeout_ms == 0) {
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    std::unique_lock<std::mutex> lock(state_mutex_);
    while (disconnect_pending_) {
        if (disconnect_condition_.wait_until(lock, deadline) !=
            std::cv_status::timeout) {
            continue;
        }

        const bool should_verify = disconnect_event_seen_ || disconnect_was_associated_;
        const uint32_t current_generation = connection_generation_;
        const uint32_t current_serial = disconnect_event_serial_;
        lock.unlock();
        if (should_verify) {
            VerifyDisconnect(current_generation, current_serial);
        }
        lock.lock();
        break;
    }

    if (disconnect_pending_) {
        ESP_LOGW(TAG, "Timed out waiting for WiFi disconnect");
        return false;
    }
    return true;
}

bool ESP32WiFiAdapter::DisconnectAndWait(uint32_t timeout_ms) {
    if (IsEventTaskContext()) {
        if (timeout_ms > 0) {
            return false;
        }
        std::unique_lock<std::mutex> operation_lock(operation_mutex_, std::try_to_lock);
        if (!operation_lock.owns_lock()) {
            return StartDisconnectWithoutOperation();
        }
        return DisconnectAndWaitWithOperation(0);
    }

    std::unique_lock<std::mutex> operation_lock(operation_mutex_);
    return DisconnectAndWaitWithOperation(timeout_ms);
}

WiFiStatus ESP32WiFiAdapter::GetStatus() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return status_;
}

std::string ESP32WiFiAdapter::GetConnectedSSID() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return connected_ssid_;
}

std::string ESP32WiFiAdapter::GetIPAddress() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return ip_address_;
}

void ESP32WiFiAdapter::ProcessScanDone(const wifi_event_sta_scan_done_t* event,
                                       uint32_t expected_generation) {
    uint32_t generation = 0;
    std::vector<wifi_ap_record_t> records;
    std::function<void(const std::vector<WiFiScanResult>&)> callback;
    esp_err_t scan_error = ESP_OK;
    uint16_t ap_count = 0;

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!initialized_ || !scan_pending_ || scan_processing_) {
            return;
        }
        generation = scan_generation_;
        if (expected_generation != 0 && expected_generation != generation) {
            return;
        }
        if (scan_command_pending_) {
            scan_event_seen_ = true;
            return;
        }
        scan_processing_ = true;
        if (event != nullptr && event->status != 0) {
            scan_error = ESP_FAIL;
        }
    }

    if (scan_error == ESP_OK) {
        std::lock_guard<std::mutex> api_lock(api_mutex_);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!initialized_ || !scan_pending_ ||
                scan_generation_ != generation || !scan_processing_) {
                return;
            }
        }
        scan_error = esp_wifi_scan_get_ap_num(&ap_count);
        if (scan_error == ESP_OK && ap_count > 0) {
            records.resize(ap_count);
            scan_error = esp_wifi_scan_get_ap_records(&ap_count, records.data());
        }
        if (scan_error != ESP_OK) {
            (void)esp_wifi_clear_ap_list();
        }
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!initialized_ || !scan_pending_ || scan_generation_ != generation ||
            !scan_processing_) {
            return;
        }
        scan_pending_ = false;
        scan_command_pending_ = false;
        scan_processing_ = false;
        scan_event_seen_ = false;
        callback = std::move(scan_callback_);
    }

    std::vector<WiFiScanResult> results;
    if (scan_error == ESP_OK) {
        results.reserve(records.size());
        for (const auto& ap : records) {
            WiFiScanResult result;
            const char* ssid = reinterpret_cast<const char*>(ap.ssid);
            result.ssid.assign(ssid, strnlen(ssid, sizeof(ap.ssid)));
            result.rssi = ap.rssi;
            result.auth_mode = ap.authmode;
            result.is_secured = (ap.authmode != WIFI_AUTH_OPEN);
            results.push_back(std::move(result));
        }
    }

    if (scan_error != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(scan_error));
    } else {
        ESP_LOGI(TAG, "Scan complete, found %u APs",
                 static_cast<unsigned>(results.size()));
    }
    if (callback) {
        callback(results);
    }
}

void ESP32WiFiAdapter::WiFiEventHandler(void* arg, esp_event_base_t event_base,
                                         int32_t event_id, void* event_data) {
    auto* self = static_cast<ESP32WiFiAdapter*>(arg);
    if (self == nullptr) {
        return;
    }

    self->event_task_handle_.store(
        static_cast<void*>(xTaskGetCurrentTaskHandle()), std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(self->state_mutex_);
        ++self->event_callbacks_in_flight_;
    }

    const bool previous = g_in_wifi_event_handler;
    g_in_wifi_event_handler = true;
    self->HandleWiFiEvent(event_base, event_id, event_data);
    g_in_wifi_event_handler = previous;

    {
        std::lock_guard<std::mutex> lock(self->state_mutex_);
        if (self->event_callbacks_in_flight_ > 0) {
            --self->event_callbacks_in_flight_;
        }
        if (self->event_callbacks_in_flight_ == 0) {
            self->event_idle_condition_.notify_all();
        }
    }
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
                if (event == nullptr || event->ssid_len > sizeof(event->ssid)) {
                    break;
                }
                std::string connected_ssid;
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    if (initialized_ && !disconnect_pending_ &&
                        !connect_command_pending_ && status_ == WiFiStatus::kConnecting &&
                        WiFiEventMatchesTargetSSID(
                            target_ssid_, event->ssid, event->ssid_len)) {
                        sta_connected_generation_ = connection_generation_;
                        connected_ssid_.assign(
                            reinterpret_cast<const char*>(event->ssid), event->ssid_len);
                        connected_ssid = connected_ssid_;
                    }
                }
                if (!connected_ssid.empty()) {
                    ESP_LOGI(TAG, "Connected to WiFi: %s", connected_ssid.c_str());
                }
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED: {
                auto* event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
                if (event == nullptr || event->ssid_len > sizeof(event->ssid)) {
                    break;
                }

                bool verify_disconnect = false;
                uint32_t verify_generation = 0;
                uint32_t verify_serial = 0;
                bool retry = false;
                int retry_count = 0;
                uint32_t retry_generation = 0;
                std::function<void(WiFiStatus)> callback;
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    if (!initialized_) {
                        break;
                    }

                    if (disconnect_pending_) {
                        const bool matches =
                            disconnect_target_ssid_.empty() ||
                            EventSSIDMatchesOrUnknown(
                                disconnect_target_ssid_, event->ssid, event->ssid_len);
                        if (!matches) {
                            break;
                        }
                        disconnect_event_seen_ = true;
                        ++disconnect_event_serial_;
                        if (disconnect_event_serial_ == 0) {
                            ++disconnect_event_serial_;
                        }
                        if (!disconnect_command_pending_ &&
                            !disconnect_verify_in_flight_) {
                            verify_disconnect = true;
                            verify_generation = connection_generation_;
                            verify_serial = disconnect_event_serial_;
                        }
                    } else {
                        if (connect_command_pending_ ||
                            !EventSSIDMatchesOrUnknown(
                                target_ssid_, event->ssid, event->ssid_len) ||
                            (status_ != WiFiStatus::kConnecting &&
                             status_ != WiFiStatus::kConnected)) {
                            break;
                        }

                        sta_connected_generation_ = 0;
                        connected_ssid_.clear();
                        ip_address_.clear();
                        if (ShouldRetryWiFiConnection(
                                retry_count_, kMaxRetries,
                                status_ == WiFiStatus::kConnecting ||
                                    status_ == WiFiStatus::kConnected,
                                connect_callback_ != nullptr)) {
                            retry = true;
                            retry_count = ++retry_count_;
                            retry_generation = connection_generation_;
                            status_ = WiFiStatus::kConnecting;
                            connect_command_pending_ = true;
                            connect_callback_notified_ = false;
                        } else {
                            status_ = WiFiStatus::kFailed;
                            callback = std::move(connect_callback_);
                            connect_callback_notified_ = false;
                        }
                    }
                }

                if (verify_disconnect) {
                    VerifyDisconnect(verify_generation, verify_serial);
                    break;
                }
                if (retry) {
                    ESP_LOGI(TAG, "Retrying connection (%d/%d)", retry_count, kMaxRetries);
                    esp_err_t connect_error = ESP_OK;
                    {
                        std::lock_guard<std::mutex> api_lock(api_mutex_);
                        {
                            std::lock_guard<std::mutex> lock(state_mutex_);
                            if (!initialized_ || disconnect_pending_ ||
                                connection_generation_ != retry_generation ||
                                status_ != WiFiStatus::kConnecting ||
                                connect_callback_ == nullptr) {
                                connect_error = ESP_ERR_WIFI_STATE;
                            }
                        }
                        if (connect_error == ESP_OK) {
                            connect_error = esp_wifi_connect();
                        }
                    }
                    {
                        std::lock_guard<std::mutex> lock(state_mutex_);
                        if (connection_generation_ == retry_generation &&
                            connect_command_pending_) {
                            connect_command_pending_ = false;
                            if (connect_error != ESP_OK) {
                                status_ = WiFiStatus::kFailed;
                                callback = std::move(connect_callback_);
                            }
                        }
                    }
                    if (connect_error != ESP_OK) {
                        ESP_LOGW(TAG, "WiFi retry failed: %s",
                                 esp_err_to_name(connect_error));
                    }
                } else if (callback) {
                    ESP_LOGI(TAG, "WiFi disconnected");
                }
                if (callback) {
                    callback(WiFiStatus::kFailed);
                }
                break;
            }

            case WIFI_EVENT_SCAN_DONE:
                ProcessScanDone(static_cast<wifi_event_sta_scan_done_t*>(event_data));
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        if (event == nullptr) {
            return;
        }

        char ip_str[16] = {};
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        std::function<void(WiFiStatus)> callback;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            const bool active_status = status_ == WiFiStatus::kConnecting ||
                                       status_ == WiFiStatus::kConnected;
            if (!initialized_ || disconnect_pending_ || !active_status ||
                !ShouldAcceptWiFiGotIP(
                    connection_generation_, sta_connected_generation_, true)) {
                return;
            }
            ip_address_ = ip_str;
            status_ = WiFiStatus::kConnected;
            retry_count_ = 0;
            if (connect_callback_ != nullptr && !connect_callback_notified_) {
                connect_callback_notified_ = true;
                callback = connect_callback_;
            }
        }

        ESP_LOGI(TAG, "Got IP address: %s", ip_str);
        if (callback) {
            callback(WiFiStatus::kConnected);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (initialized_ && !disconnect_pending_ &&
            sta_connected_generation_ == connection_generation_ &&
            status_ == WiFiStatus::kConnected) {
            ip_address_.clear();
            status_ = WiFiStatus::kConnecting;
        }
    }
}

// 工厂函数
WiFiAdapter* CreateWiFiAdapter() {
    return new ESP32WiFiAdapter();
}

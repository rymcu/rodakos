#include "phone_os/unified_mqtt_service.h"

#include "phone_os/audio_output_service.h"
#include "phone_os/ota_update_service.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/semphr.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <utility>

namespace rodakos {
namespace {
constexpr const char* TAG = "UnifiedMqtt";
constexpr int kTelemetryIntervalMs = 30 * 1000;
constexpr int kConnectionRetryInitialMs = 2 * 1000;
constexpr int kConnectionRetryMaxMs = 60 * 1000;
constexpr int kCredentialRefreshDelayMs = 2 * 1000;
constexpr int kBackgroundTaskPollMs = 50;

std::string EncodeJson(cJSON* root) {
    char* text = cJSON_PrintUnformatted(root);
    if (text == nullptr) {
        return "{}";
    }
    std::string result(text);
    cJSON_free(text);
    return result;
}

std::string BuildClientId(const std::string& device_key) {
    std::string suffix;
    suffix.reserve(device_key.size());
    for (const unsigned char ch : device_key) {
        if (std::isalnum(ch)) {
            suffix.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    if (suffix.size() > 20) {
        suffix = suffix.substr(suffix.size() - 20);
    }
    return "rodakos_" + suffix;
}

bool NeedsV2Refresh(const DeviceCloudConfig& config) {
    return config.mqtt_protocol_version < 2 || config.mqtt_http_base_url.empty() ||
           config.mqtt_topic_ota_notify.empty() || config.mqtt_topic_ota_progress.empty();
}

void DelayWhileStarted(const std::atomic<bool>& started, int delay_ms) {
    int remaining_ms = delay_ms;
    while (started.load() && remaining_ms > 0) {
        const int step_ms = std::min(remaining_ms, kBackgroundTaskPollMs);
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        remaining_ms -= step_ms;
    }
}

void TimerDeleteBarrier(void* semaphore, uint32_t value) {
    (void)value;
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(semaphore));
}

void DeleteTimerAndWait(TimerHandle_t timer) {
    StaticSemaphore_t semaphore_storage;
    SemaphoreHandle_t completed = xSemaphoreCreateBinaryStatic(&semaphore_storage);
    xTimerStop(timer, portMAX_DELAY);
    if (xTimerDelete(timer, portMAX_DELAY) == pdPASS && completed != nullptr &&
        xTimerPendFunctionCall(TimerDeleteBarrier, completed, 0, portMAX_DELAY) == pdPASS) {
        xSemaphoreTake(completed, portMAX_DELAY);
    }
    if (completed != nullptr) {
        vSemaphoreDelete(completed);
    }
}
}  // namespace

UnifiedMqttService::UnifiedMqttService(DeviceCloudConfigService& config_service,
                                       OtaUpdateService& ota_update,
                                       AudioOutputService* audio_output)
    : config_service_(config_service), ota_update_(ota_update), audio_output_(audio_output) {
    ota_update_.SetProgressPublisher([this](const std::string& payload) {
        return Publish(CopyTopic(&DeviceCloudConfig::mqtt_topic_ota_progress), payload);
    });
}

UnifiedMqttService::~UnifiedMqttService() {
    Stop();
}

bool UnifiedMqttService::Start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return true;
    }

    const esp_err_t err = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, NetworkEventHandler, this, &ip_event_instance_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register network listener: %s", esp_err_to_name(err));
        started_.store(false);
        return false;
    }
    ESP_LOGI(TAG, "Waiting for WiFi before starting MQTT");
    return true;
}

void UnifiedMqttService::Stop() {
    if (!started_.exchange(false)) {
        return;
    }
    connected_.store(false);
    TimerHandle_t telemetry_timer = nullptr;
    esp_mqtt_client_handle_t client = nullptr;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        telemetry_timer = telemetry_timer_;
        telemetry_timer_ = nullptr;
        client = client_;
        client_ = nullptr;
    }
    if (telemetry_timer != nullptr) {
        DeleteTimerAndWait(telemetry_timer);
    }
    if (client != nullptr) {
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
    }
    if (ip_event_instance_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_instance_);
        ip_event_instance_ = nullptr;
    }
    while (connecting_.load() || reset_scheduled_.load()) {
        vTaskDelay(pdMS_TO_TICKS(kBackgroundTaskPollMs));
    }
}

void UnifiedMqttService::NetworkEventHandler(void* arg, esp_event_base_t event_base,
                                             int32_t event_id, void* event_data) {
    (void)event_base;
    (void)event_id;
    (void)event_data;
    auto* service = static_cast<UnifiedMqttService*>(arg);
    if (service != nullptr) {
        service->StartConnectionAsync();
    }
}

void UnifiedMqttService::StartConnectionAsync() {
    if (!started_.load() || HasClient()) {
        return;
    }
    bool expected = false;
    if (!connecting_.compare_exchange_strong(expected, true)) {
        return;
    }
    if (xTaskCreate(ConnectionTask, "mqtt_config", 8192, this, 4, nullptr) != pdPASS) {
        connecting_.store(false);
    }
}

void UnifiedMqttService::ConnectionTask(void* arg) {
    auto* service = static_cast<UnifiedMqttService*>(arg);
    if (service != nullptr) {
        int retry_delay_ms = kConnectionRetryInitialMs;
        while (service->started_.load() && !service->HasClient()) {
            service->Connect();
            if (service->HasClient() || !service->started_.load()) {
                break;
            }
            ESP_LOGW(TAG, "Unified MQTT setup failed; retrying in %d ms", retry_delay_ms);
            DelayWhileStarted(service->started_, retry_delay_ms);
            retry_delay_ms = std::min(retry_delay_ms * 2, kConnectionRetryMaxMs);
        }
        service->connecting_.store(false);
    }
    vTaskDelete(nullptr);
}

void UnifiedMqttService::Connect() {
    DeviceCloudConfig next_config;
    config_service_.Load(next_config);
    if (force_refresh_.exchange(false) || !next_config.has_mqtt_config ||
        NeedsV2Refresh(next_config)) {
        ESP_LOGI(TAG, "Refreshing bootstrap to obtain unified MQTT v2 credentials");
        const DeviceCloudConfig cached_config = next_config;
        DeviceCloudConfig refreshed_config = next_config;
        const bool refresh_succeeded = config_service_.Refresh(refreshed_config);
        next_config = refresh_succeeded && refreshed_config.has_mqtt_config
                          ? std::move(refreshed_config)
                          : cached_config;
    }
    if (!next_config.has_mqtt_config) {
        const std::string config_error = config_service_.last_error();
        ESP_LOGW(TAG, "Unified MQTT credentials are unavailable: %s",
                 config_error.c_str());
        return;
    }

    std::string next_broker_uri = "mqtt://" + next_config.mqtt_broker_address + ":" +
                                  std::to_string(next_config.mqtt_broker_port);
    std::string next_client_id = BuildClientId(next_config.mqtt_device_key);
    esp_mqtt_client_config_t mqtt_config = {};
    mqtt_config.broker.address.uri = next_broker_uri.c_str();
    mqtt_config.credentials.client_id = next_client_id.c_str();
    mqtt_config.credentials.username = next_config.mqtt_username.c_str();
    mqtt_config.credentials.authentication.password = next_config.mqtt_password.c_str();
    mqtt_config.session.keepalive = next_config.mqtt_keepalive;
    mqtt_config.network.reconnect_timeout_ms = 2000;
    mqtt_config.network.timeout_ms = 10 * 1000;

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }
    const esp_err_t register_err = esp_mqtt_client_register_event(
        client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), MqttEventHandler, this);
    if (register_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s",
                 esp_err_to_name(register_err));
        esp_mqtt_client_destroy(client);
        return;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    std::string broker_uri;
    std::string username;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (started_.load()) {
            config_ = std::move(next_config);
            broker_uri_ = std::move(next_broker_uri);
            client_id_ = std::move(next_client_id);
            client_ = client;
            err = esp_mqtt_client_start(client);
            if (err == ESP_OK) {
                broker_uri = broker_uri_;
                username = config_.mqtt_username;
            } else {
                client_ = nullptr;
            }
        }
    }
    if (err != ESP_OK) {
        if (started_.load()) {
            ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        }
        esp_mqtt_client_destroy(client);
        return;
    }
    ESP_LOGI(TAG, "Connecting to %s as %s", broker_uri.c_str(), username.c_str());
}

void UnifiedMqttService::ScheduleCredentialRefresh() {
    if (!started_.load()) {
        return;
    }
    bool expected = false;
    if (!reset_scheduled_.compare_exchange_strong(expected, true)) {
        return;
    }
    if (xTaskCreate(ClientResetTask, "mqtt_reauth", 4096, this, 4, nullptr) != pdPASS) {
        reset_scheduled_.store(false);
    }
}

void UnifiedMqttService::ClientResetTask(void* arg) {
    auto* service = static_cast<UnifiedMqttService*>(arg);
    if (service != nullptr) {
        DelayWhileStarted(service->started_, kCredentialRefreshDelayMs);
    }
    if (service != nullptr && service->started_.load()) {
        service->connected_.store(false);
        esp_mqtt_client_handle_t client = nullptr;
        {
            std::lock_guard<std::mutex> lock(service->mqtt_mutex_);
            client = service->client_;
            service->client_ = nullptr;
        }
        if (client != nullptr) {
            esp_mqtt_client_stop(client);
            esp_mqtt_client_destroy(client);
        }
        service->force_refresh_.store(true);
        if (service->started_.load()) {
            service->StartConnectionAsync();
        }
    }
    if (service != nullptr) {
        service->reset_scheduled_.store(false);
    }
    vTaskDelete(nullptr);
}

bool UnifiedMqttService::HasClient() const {
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    return client_ != nullptr;
}

std::string UnifiedMqttService::CopyTopic(
    const std::string DeviceCloudConfig::*member) const {
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    return config_.*member;
}

void UnifiedMqttService::MqttEventHandler(void* arg, esp_event_base_t event_base,
                                          int32_t event_id, void* event_data) {
    (void)event_base;
    (void)event_id;
    auto* service = static_cast<UnifiedMqttService*>(arg);
    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
    if (service != nullptr && event != nullptr) {
        service->HandleMqttEvent(event);
    }
}

void UnifiedMqttService::HandleMqttEvent(esp_mqtt_event_handle_t event) {
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!started_.load() || event->client != client_) {
            return;
        }
        if (event->event_id == MQTT_EVENT_CONNECTED) {
            connected_.store(true);
        } else if (event->event_id == MQTT_EVENT_DISCONNECTED) {
            connected_.store(false);
        }
    }
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Unified MQTT connected");
            ota_update_.OnNetworkReady();
            SubscribeTopics();
            PublishTelemetry();
            PublishShadowReport();
            {
                std::lock_guard<std::mutex> lock(mqtt_mutex_);
                if (!started_.load() || event->client != client_) {
                    return;
                }
                if (telemetry_timer_ == nullptr) {
                    telemetry_timer_ = xTimerCreate(
                        "mqtt_telemetry", pdMS_TO_TICKS(kTelemetryIntervalMs), pdTRUE,
                        this, TelemetryTimerCallback);
                }
                if (telemetry_timer_ != nullptr) {
                    xTimerStart(telemetry_timer_, 0);
                }
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Unified MQTT disconnected; ESP-MQTT will reconnect");
            break;
        case MQTT_EVENT_DATA:
            if (event->current_data_offset == 0 && event->data_len == event->total_data_len) {
                HandleMessage(std::string(event->topic, event->topic_len),
                              std::string(event->data, event->data_len));
            } else {
                ESP_LOGW(TAG, "Ignoring fragmented MQTT payload on topic %.*s",
                         event->topic_len, event->topic);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "MQTT transport error");
            if (event->error_handle != nullptr &&
                event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED &&
                (event->error_handle->connect_return_code == MQTT_CONNECTION_REFUSE_BAD_USERNAME ||
                 event->error_handle->connect_return_code ==
                     MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED)) {
                ESP_LOGW(TAG, "MQTT credentials were rejected; refreshing bootstrap credentials");
                ScheduleCredentialRefresh();
            }
            break;
        default:
            break;
    }
}

void UnifiedMqttService::SubscribeTopics() {
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    if (!started_.load() || client_ == nullptr) {
        return;
    }
    const std::string* topics[] = {
        &config_.mqtt_topic_shadow_desired,
        &config_.mqtt_topic_ota_notify,
        &config_.mqtt_topic_pc_status,
    };
    for (const std::string* topic : topics) {
        if (!topic->empty() && esp_mqtt_client_subscribe(client_, topic->c_str(), 0) < 0) {
            ESP_LOGW(TAG, "Failed to subscribe %s", topic->c_str());
        }
    }
}

void UnifiedMqttService::HandleMessage(const std::string& topic,
                                       const std::string& payload) {
    const std::string ota_topic = CopyTopic(&DeviceCloudConfig::mqtt_topic_ota_notify);
    const std::string shadow_topic = CopyTopic(&DeviceCloudConfig::mqtt_topic_shadow_desired);
    if (topic == ota_topic) {
        ota_update_.HandleNotification(payload);
        return;
    }
    if (topic == shadow_topic) {
        ApplyDesiredShadow(payload);
    }
}

void UnifiedMqttService::ApplyDesiredShadow(const std::string& payload) {
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return;
    }
    cJSON* desired = cJSON_GetObjectItemCaseSensitive(root, "desired");
    if (!cJSON_IsObject(desired)) {
        cJSON* state = cJSON_GetObjectItemCaseSensitive(root, "state");
        desired = cJSON_IsObject(state)
                      ? cJSON_GetObjectItemCaseSensitive(state, "desired")
                      : nullptr;
    }
    cJSON* volume = cJSON_IsObject(desired)
                        ? cJSON_GetObjectItemCaseSensitive(desired, "volume")
                        : nullptr;
    if (cJSON_IsNumber(volume) && audio_output_ != nullptr) {
        audio_output_->SetVolume(std::clamp(volume->valueint, 0, 100));
        PublishShadowReport();
    }
    cJSON_Delete(root);
}

void UnifiedMqttService::TelemetryTimerCallback(TimerHandle_t timer) {
    auto* service = static_cast<UnifiedMqttService*>(pvTimerGetTimerID(timer));
    if (service != nullptr && service->started_.load()) {
        service->PublishTelemetry();
    }
}

void UnifiedMqttService::PublishTelemetry() {
    wifi_ap_record_t access_point = {};
    const bool has_wifi = esp_wifi_sta_get_ap_info(&access_point) == ESP_OK;
    const esp_app_desc_t* app = esp_app_get_description();
    const esp_partition_t* running = esp_ota_get_running_partition();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "firmware", app != nullptr ? app->version : "unknown");
    if (has_wifi) {
        cJSON_AddNumberToObject(root, "wifi_rssi", access_point.rssi);
    }
    if (audio_output_ != nullptr) {
        cJSON_AddNumberToObject(root, "volume", audio_output_->volume());
    }
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "minimum_free_heap_size", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(root, "uptime_ms",
                           static_cast<double>(esp_timer_get_time() / 1000));
    cJSON_AddStringToObject(root, "ota_slot", running != nullptr ? running->label : "unknown");
    const std::string payload = EncodeJson(root);
    cJSON_Delete(root);
    Publish(CopyTopic(&DeviceCloudConfig::mqtt_topic_telemetry), payload);
}

void UnifiedMqttService::PublishShadowReport() {
    const esp_app_desc_t* app = esp_app_get_description();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "firmware", app != nullptr ? app->version : "unknown");
    if (audio_output_ != nullptr) {
        cJSON_AddNumberToObject(root, "volume", audio_output_->volume());
    }
    const std::string payload = EncodeJson(root);
    cJSON_Delete(root);
    Publish(CopyTopic(&DeviceCloudConfig::mqtt_topic_shadow_report), payload);
}

bool UnifiedMqttService::Publish(const std::string& topic, const std::string& payload) {
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    if (!connected_.load() || client_ == nullptr || topic.empty()) {
        return false;
    }
    return esp_mqtt_client_enqueue(
               client_, topic.c_str(), payload.data(), payload.size(), 0, 0, true) >= 0;
}

}  // namespace rodakos

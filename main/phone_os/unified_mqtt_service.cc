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
#include <memory>
#include <new>
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
constexpr int kReliablePublishTimeoutMs = 10 * 1000;
constexpr int kReliablePublishRetryMs = 50;

struct MqttConnectedTaskContext {
    UnifiedMqttService* service = nullptr;
    uint32_t client_generation = 0;
};

struct MqttMessageTaskContext {
    UnifiedMqttService* service = nullptr;
    uint32_t client_generation = 0;
    std::string topic;
    std::string payload;
};

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
           config.mqtt_topic_ota_notify.empty() || config.mqtt_topic_ota_progress.empty() ||
           config.mqtt_topic_commands.empty() || config.mqtt_topic_pc_status.empty();
}

bool ExtractCommandNo(const std::string& topic, const std::string& wildcard,
                      std::string& command_no) {
    if (wildcard.empty() || wildcard.back() != '+') {
        return false;
    }
    const std::string prefix = wildcard.substr(0, wildcard.size() - 1);
    if (topic.rfind(prefix, 0) != 0) {
        return false;
    }
    command_no = topic.substr(prefix.size());
    return !command_no.empty() && command_no.find('/') == std::string::npos;
}

bool IsPingCommand(const std::string& payload) {
    cJSON* root = cJSON_Parse(payload.c_str());
    if (root == nullptr) {
        return payload == "ping";
    }
    const cJSON* command = nullptr;
    if (cJSON_IsString(root)) {
        command = root;
    } else if (cJSON_IsObject(root)) {
        command = cJSON_GetObjectItemCaseSensitive(root, "command");
        if (!cJSON_IsString(command)) {
            command = cJSON_GetObjectItemCaseSensitive(root, "type");
        }
    }
    const bool is_ping = cJSON_IsString(command) && command->valuestring != nullptr &&
                         std::string(command->valuestring) == "ping";
    cJSON_Delete(root);
    return is_ping;
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
    publish_ack_semaphore_ = xSemaphoreCreateBinaryStatic(&publish_ack_semaphore_storage_);
}

void UnifiedMqttService::BindOtaProgressPublisher() {
    ota_update_.SetProgressPublisher([this](const std::string& payload, bool wait_for_ack) {
        const std::string topic = CopyTopic(&DeviceCloudConfig::mqtt_topic_ota_progress);
        return wait_for_ack ? PublishWithAck(topic, payload) : Publish(topic, payload);
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
    BindOtaProgressPublisher();

    const esp_err_t err = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, NetworkEventHandler, this, &ip_event_instance_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register network listener: %s", esp_err_to_name(err));
        started_.store(false);
        ota_update_.SetProgressPublisher({});
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
    bool wake_publisher = false;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        telemetry_timer = telemetry_timer_;
        telemetry_timer_ = nullptr;
        client = client_;
        client_ = nullptr;
        ++client_generation_;
        wake_publisher = reliable_publish_.message_id >= 0;
        reliable_publish_ = {};
    }
    if (wake_publisher && publish_ack_semaphore_ != nullptr) {
        xSemaphoreGive(publish_ack_semaphore_);
    }
    ota_update_.SetProgressPublisher({});
    if (telemetry_timer != nullptr) {
        DeleteTimerAndWait(telemetry_timer);
    }
    if (client != nullptr) {
        std::lock_guard<std::mutex> api_lock(client_api_mutex_);
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
    }
    if (ip_event_instance_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_instance_);
        ip_event_instance_ = nullptr;
    }
    while (connecting_.load() || reset_scheduled_.load() || event_worker_count_.load() > 0) {
        vTaskDelay(pdMS_TO_TICKS(kBackgroundTaskPollMs));
    }
    std::lock_guard<std::mutex> reliable_lock(reliable_publish_mutex_);
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

    std::lock_guard<std::mutex> api_lock(client_api_mutex_);
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
    uint32_t generation = 0;
    bool attached = false;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (started_.load() && client_ == nullptr) {
            config_ = std::move(next_config);
            broker_uri_ = std::move(next_broker_uri);
            client_id_ = std::move(next_client_id);
            client_ = client;
            generation = ++client_generation_;
            attached = true;
            broker_uri = broker_uri_;
            username = config_.mqtt_username;
        }
    }
    if (attached) {
        err = esp_mqtt_client_start(client);
    }
    bool destroy_client = !attached;
    if (attached && err != ESP_OK) {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (client_ == client && client_generation_ == generation) {
            client_ = nullptr;
            ++client_generation_;
            destroy_client = true;
        }
    }
    if (err != ESP_OK) {
        if (started_.load() && attached) {
            ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        }
        if (destroy_client) {
            esp_mqtt_client_destroy(client);
        }
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (client_ != client || client_generation_ != generation) {
            return;
        }
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
        bool wake_publisher = false;
        {
            std::lock_guard<std::mutex> lock(service->mqtt_mutex_);
            client = service->client_;
            service->client_ = nullptr;
            ++service->client_generation_;
            wake_publisher = service->reliable_publish_.message_id >= 0;
            service->reliable_publish_ = {};
        }
        if (wake_publisher && service->publish_ack_semaphore_ != nullptr) {
            xSemaphoreGive(service->publish_ack_semaphore_);
        }
        if (client != nullptr) {
            std::lock_guard<std::mutex> api_lock(service->client_api_mutex_);
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

bool UnifiedMqttService::IsCurrentClientGeneration(uint32_t generation) const {
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    return started_.load() && connected_.load() && client_ != nullptr &&
           client_generation_ == generation;
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
    bool wake_publisher = false;
    bool schedule_connected = false;
    bool schedule_message = false;
    uint32_t event_generation = 0;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!started_.load() || event->client != client_) {
            return;
        }
        if (event->event_id == MQTT_EVENT_CONNECTED) {
            connected_.store(true);
            schedule_connected = true;
            event_generation = client_generation_;
            event_worker_count_.fetch_add(1);
        } else if (event->event_id == MQTT_EVENT_DISCONNECTED) {
            connected_.store(false);
        } else if (event->event_id == MQTT_EVENT_PUBLISHED) {
            const uint64_t sequence = ++published_event_sequence_;
            recent_published_events_[next_published_event_index_] = {
                client_generation_, sequence, event->msg_id};
            next_published_event_index_ =
                (next_published_event_index_ + 1) % recent_published_events_.size();
            if (reliable_publish_.client_generation == client_generation_ &&
                reliable_publish_.message_id == event->msg_id) {
                reliable_publish_.acknowledged = true;
                wake_publisher = true;
            }
        } else if (event->event_id == MQTT_EVENT_DELETED &&
                   reliable_publish_.client_generation == client_generation_ &&
                   reliable_publish_.message_id == event->msg_id) {
            reliable_publish_ = {};
            wake_publisher = true;
        } else if (event->event_id == MQTT_EVENT_DATA &&
                   event->current_data_offset == 0 &&
                   event->data_len == event->total_data_len) {
            schedule_message = true;
            event_generation = client_generation_;
            event_worker_count_.fetch_add(1);
        }
    }
    if (wake_publisher && publish_ack_semaphore_ != nullptr) {
        xSemaphoreGive(publish_ack_semaphore_);
    }
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Unified MQTT connected");
            if (schedule_connected) {
                auto context = std::unique_ptr<MqttConnectedTaskContext>(
                    new (std::nothrow) MqttConnectedTaskContext{this, event_generation});
                if (context == nullptr ||
                    xTaskCreate(ConnectedTask, "mqtt_connected", 6144, context.get(), 4,
                                nullptr) != pdPASS) {
                    event_worker_count_.fetch_sub(1);
                    ESP_LOGE(TAG, "Failed to schedule MQTT connected work");
                } else {
                    context.release();
                }
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Unified MQTT disconnected; ESP-MQTT will reconnect");
            break;
        case MQTT_EVENT_DATA:
            if (!schedule_message) {
                ESP_LOGW(TAG, "Ignoring fragmented MQTT payload on topic %.*s",
                         event->topic_len, event->topic);
                break;
            }
            {
                auto context = std::unique_ptr<MqttMessageTaskContext>(
                    new (std::nothrow) MqttMessageTaskContext{
                        this,
                        event_generation,
                        std::string(event->topic, event->topic_len),
                        std::string(event->data, event->data_len),
                    });
                if (context == nullptr ||
                    xTaskCreate(MessageTask, "mqtt_message", 6144, context.get(), 4,
                                nullptr) != pdPASS) {
                    event_worker_count_.fetch_sub(1);
                    ESP_LOGE(TAG, "Failed to schedule MQTT message work");
                } else {
                    context.release();
                }
            }
            break;
        case MQTT_EVENT_DELETED:
            ESP_LOGW(TAG, "MQTT outbox deleted expired message: msg_id=%d", event->msg_id);
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

void UnifiedMqttService::ConnectedTask(void* arg) {
    std::unique_ptr<MqttConnectedTaskContext> context(
        static_cast<MqttConnectedTaskContext*>(arg));
    UnifiedMqttService* service = context != nullptr ? context->service : nullptr;
    const uint32_t generation = context != nullptr ? context->client_generation : 0;
    if (service != nullptr && service->IsCurrentClientGeneration(generation)) {
        service->SubscribeTopics();
        if (service->IsCurrentClientGeneration(generation)) {
            service->ota_update_.OnNetworkReady();
            service->PublishTelemetry();
            service->PublishShadowReport();
        }
        std::lock_guard<std::mutex> lock(service->mqtt_mutex_);
        if (service->started_.load() && service->connected_.load() &&
            service->client_generation_ == generation) {
            if (service->telemetry_timer_ == nullptr) {
                service->telemetry_timer_ = xTimerCreate(
                    "mqtt_telemetry", pdMS_TO_TICKS(kTelemetryIntervalMs), pdTRUE,
                    service, TelemetryTimerCallback);
            }
            if (service->telemetry_timer_ != nullptr) {
                xTimerStart(service->telemetry_timer_, 0);
            }
        }
    }
    if (service != nullptr) {
        service->event_worker_count_.fetch_sub(1);
    }
    vTaskDelete(nullptr);
}

void UnifiedMqttService::MessageTask(void* arg) {
    std::unique_ptr<MqttMessageTaskContext> context(
        static_cast<MqttMessageTaskContext*>(arg));
    if (context != nullptr && context->service != nullptr) {
        if (context->service->IsCurrentClientGeneration(context->client_generation)) {
            context->service->HandleMessage(context->topic, context->payload);
        }
        context->service->event_worker_count_.fetch_sub(1);
    }
    vTaskDelete(nullptr);
}

void UnifiedMqttService::SubscribeTopics() {
    std::lock_guard<std::mutex> api_lock(client_api_mutex_);
    esp_mqtt_client_handle_t client = nullptr;
    std::array<std::string, 4> topics;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!started_.load() || client_ == nullptr) {
            return;
        }
        client = client_;
        topics = {
            config_.mqtt_topic_shadow_desired,
            config_.mqtt_topic_ota_notify,
            config_.mqtt_topic_commands,
            config_.mqtt_topic_pc_status,
        };
    }
    for (const std::string& topic : topics) {
        if (!topic.empty() && esp_mqtt_client_subscribe(client, topic.c_str(), 0) < 0) {
            ESP_LOGW(TAG, "Failed to subscribe %s", topic.c_str());
        }
    }
}

void UnifiedMqttService::HandleMessage(const std::string& topic,
                                       const std::string& payload) {
    const std::string ota_topic = CopyTopic(&DeviceCloudConfig::mqtt_topic_ota_notify);
    const std::string shadow_topic = CopyTopic(&DeviceCloudConfig::mqtt_topic_shadow_desired);
    const std::string commands_topic = CopyTopic(&DeviceCloudConfig::mqtt_topic_commands);
    const std::string pc_status_topic = CopyTopic(&DeviceCloudConfig::mqtt_topic_pc_status);
    if (topic == ota_topic) {
        ota_update_.HandleNotification(payload);
        return;
    }
    if (topic == shadow_topic) {
        ApplyDesiredShadow(payload);
        return;
    }
    if (topic == pc_status_topic) {
        HandlePcStatus(payload);
        return;
    }
    std::string command_no;
    if (ExtractCommandNo(topic, commands_topic, command_no)) {
        HandleCommand(command_no, payload);
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

void UnifiedMqttService::HandlePcStatus(const std::string& payload) {
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "Ignoring invalid PC status payload");
        cJSON_Delete(root);
        return;
    }
    const cJSON* host = cJSON_GetObjectItemCaseSensitive(root, "host");
    const cJSON* cpu = cJSON_GetObjectItemCaseSensitive(root, "cpu");
    const cJSON* memory = cJSON_GetObjectItemCaseSensitive(root, "memory");
    const cJSON* host_name = cJSON_IsObject(host)
                                 ? cJSON_GetObjectItemCaseSensitive(host, "name")
                                 : nullptr;
    const cJSON* cpu_usage = cJSON_IsObject(cpu)
                                 ? cJSON_GetObjectItemCaseSensitive(cpu, "usagePercent")
                                 : nullptr;
    const cJSON* memory_usage = cJSON_IsObject(memory)
                                    ? cJSON_GetObjectItemCaseSensitive(memory, "usagePercent")
                                    : nullptr;
    ESP_LOGI(TAG, "PC status received: host=%s cpu=%.1f%% memory=%.1f%%",
             cJSON_IsString(host_name) && host_name->valuestring != nullptr
                 ? host_name->valuestring
                 : "unknown",
             cJSON_IsNumber(cpu_usage) ? cpu_usage->valuedouble : -1.0,
             cJSON_IsNumber(memory_usage) ? memory_usage->valuedouble : -1.0);
    cJSON_Delete(root);
}

void UnifiedMqttService::HandleCommand(const std::string& command_no,
                                       const std::string& payload) {
    const bool is_ping = IsPingCommand(payload);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", is_ping ? "ok" : "error");
    if (is_ping) {
        cJSON* result = cJSON_CreateObject();
        const esp_app_desc_t* app = esp_app_get_description();
        cJSON_AddBoolToObject(result, "pong", true);
        cJSON_AddStringToObject(result, "firmware", app != nullptr ? app->version : "unknown");
        cJSON_AddItemToObject(root, "result", result);
    } else {
        cJSON_AddStringToObject(root, "errorCode", "unsupported_command");
    }
    const std::string ack_payload = EncodeJson(root);
    cJSON_Delete(root);

    const std::string wildcard = CopyTopic(&DeviceCloudConfig::mqtt_topic_commands);
    const std::string ack_topic = wildcard.empty()
                                      ? std::string()
                                      : wildcard.substr(0, wildcard.size() - 1) + command_no + "/ack";
    if (Publish(ack_topic, ack_payload)) {
        ESP_LOGI(TAG, "Command %s acknowledged: %s", command_no.c_str(),
                 is_ping ? "ok" : "unsupported");
    } else {
        ESP_LOGW(TAG, "Failed to acknowledge command %s", command_no.c_str());
    }
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
    if (topic.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> api_lock(client_api_mutex_);
    esp_mqtt_client_handle_t client = nullptr;
    uint32_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!connected_.load() || client_ == nullptr) {
            return false;
        }
        client = client_;
        generation = client_generation_;
    }
    if (esp_mqtt_client_enqueue(
            client, topic.c_str(), payload.data(), payload.size(), 0, 0, true) < 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    return client_ == client && client_generation_ == generation;
}

bool UnifiedMqttService::PublishWithAck(const std::string& topic,
                                        const std::string& payload) {
    if (publish_ack_semaphore_ == nullptr || topic.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> reliable_lock(reliable_publish_mutex_);
    xSemaphoreTake(publish_ack_semaphore_, 0);

    const TickType_t started_at = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(kReliablePublishTimeoutMs);
    uint32_t generation = 0;
    int message_id = -1;

    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (reliable_publish_.message_id >= 0) {
            if (reliable_publish_.client_generation != client_generation_) {
                reliable_publish_ = {};
            } else if (reliable_publish_.topic != topic ||
                       reliable_publish_.payload != payload) {
                ESP_LOGW(TAG, "Another reliable MQTT publish is still pending");
                return false;
            } else {
                generation = reliable_publish_.client_generation;
                message_id = reliable_publish_.message_id;
                if (reliable_publish_.acknowledged) {
                    reliable_publish_ = {};
                    return true;
                }
            }
        }
    }

    while (xTaskGetTickCount() - started_at < timeout) {
        if (message_id >= 0) {
            break;
        }
        {
            std::lock_guard<std::mutex> api_lock(client_api_mutex_);
            esp_mqtt_client_handle_t client = nullptr;
            uint64_t published_before_enqueue = 0;
            {
                std::lock_guard<std::mutex> lock(mqtt_mutex_);
                if (!connected_.load() || client_ == nullptr) {
                    return false;
                }
                client = client_;
                generation = client_generation_;
                published_before_enqueue = published_event_sequence_;
            }
            message_id = esp_mqtt_client_enqueue(
                client, topic.c_str(), payload.data(), payload.size(), 1, 0, true);
            if (message_id >= 0) {
                std::lock_guard<std::mutex> lock(mqtt_mutex_);
                if (client_ != client || client_generation_ != generation) {
                    return false;
                }
                bool already_acknowledged = false;
                for (const PublishedEvent& published : recent_published_events_) {
                    if (published.client_generation == generation &&
                        published.message_id == message_id &&
                        published.sequence > published_before_enqueue) {
                        already_acknowledged = true;
                        break;
                    }
                }
                reliable_publish_ = {
                    generation, message_id, already_acknowledged, topic, payload};
            }
        }
        if (message_id < 0) {
            vTaskDelay(pdMS_TO_TICKS(kReliablePublishRetryMs));
        }
    }
    if (message_id < 0) {
        ESP_LOGW(TAG, "Reliable MQTT publish could not enter the outbox");
        return false;
    }

    while (true) {
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            if (reliable_publish_.client_generation != generation ||
                reliable_publish_.message_id != message_id) {
                return false;
            }
            if (reliable_publish_.acknowledged) {
                reliable_publish_ = {};
                return true;
            }
        }
        const TickType_t elapsed = xTaskGetTickCount() - started_at;
        if (elapsed >= timeout) {
            break;
        }
        xSemaphoreTake(publish_ack_semaphore_, timeout - elapsed);
    }

    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (reliable_publish_.client_generation == generation &&
            reliable_publish_.message_id == message_id &&
            reliable_publish_.acknowledged) {
            reliable_publish_ = {};
            return true;
        }
    }
    ESP_LOGW(TAG, "Reliable MQTT publish is still awaiting PUBACK: msg_id=%d", message_id);
    return false;
}

}  // namespace rodakos

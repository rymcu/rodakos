#include "phone_os/resource_failure_injection.h"
#include "phone_os/unified_mqtt_service.h"

#include "phone_os/audio_output_service.h"
#include "phone_os/realtime_voice_contract.h"
#include "phone_os/ota_update_service.h"
#include "phone_os/voice_wake_service.h"
#include "phone_os/webrtc_camera_service.h"
#include "phone_os/webrtc_display_service.h"
#include "phone_os/appearance_service.h"
#include "rodak_appearance_policy.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
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
constexpr size_t kMaxCommandPublications = 8;
constexpr size_t kMaxCommandPublicationPayloadBytes = 64 * 1024;
constexpr size_t kMaxCommandPublicationBytes = 128 * 1024;
constexpr size_t kMaxCommandPublicationTopicBytes = 512;

bool HasUniqueJsonKeys(const cJSON* object) {
    if (!cJSON_IsObject(object)) return false;
    size_t count = 0;
    for (const cJSON* item = object->child; item != nullptr; item = item->next)
        if (++count > 256 || item->string == nullptr) return false;
    for (const cJSON* item = object->child; item != nullptr; item = item->next)
        for (const cJSON* next = item->next; next != nullptr; next = next->next)
            if (std::strcmp(item->string, next->string) == 0) return false;
    return true;
}

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

esp_mqtt_client_config_t BuildMqttClientConfig(const DeviceCloudConfig& config,
                                                const std::string& broker_uri,
                                                const std::string& client_id) {
    esp_mqtt_client_config_t mqtt_config = {};
    mqtt_config.broker.address.uri = broker_uri.c_str();
    mqtt_config.credentials.client_id = client_id.c_str();
    mqtt_config.credentials.username = config.mqtt_username.c_str();
    mqtt_config.credentials.authentication.password = config.mqtt_password.c_str();
    mqtt_config.session.keepalive = config.mqtt_keepalive;
    mqtt_config.network.reconnect_timeout_ms = 2000;
    mqtt_config.network.timeout_ms = 10 * 1000;
    // The default 6 KiB stack can fail alongside the local wake model. Plain
    // MQTT has stayed within 4 KiB while leaving headroom for bootstrap work.
    mqtt_config.task.stack_size = 4096;
    return mqtt_config;
}

bool HasSameMqttSessionIdentity(const DeviceCloudConfig& current,
                                const DeviceCloudConfig& refreshed) {
    return current.mqtt_protocol_version == refreshed.mqtt_protocol_version &&
           current.mqtt_broker_address == refreshed.mqtt_broker_address &&
           current.mqtt_broker_port == refreshed.mqtt_broker_port &&
           current.mqtt_username == refreshed.mqtt_username &&
           current.mqtt_device_key == refreshed.mqtt_device_key &&
           current.mqtt_home_enabled == refreshed.mqtt_home_enabled &&
           current.mqtt_http_base_url == refreshed.mqtt_http_base_url &&
           current.mqtt_topic_telemetry == refreshed.mqtt_topic_telemetry &&
           current.mqtt_topic_shadow_report == refreshed.mqtt_topic_shadow_report &&
           current.mqtt_topic_shadow_desired == refreshed.mqtt_topic_shadow_desired &&
           current.mqtt_topic_ota_notify == refreshed.mqtt_topic_ota_notify &&
           current.mqtt_topic_ota_progress == refreshed.mqtt_topic_ota_progress &&
           current.mqtt_topic_commands == refreshed.mqtt_topic_commands &&
           current.mqtt_topic_pc_status == refreshed.mqtt_topic_pc_status &&
           current.mqtt_topic_home_prefix == refreshed.mqtt_topic_home_prefix;
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

bool HasSameEffectAuthority(const DeviceCloudConfig& current,
                            const DeviceCloudConfig& next) {
    // 密码正常轮换不改变同一 boot 的去重域；重新绑定、authority 或路由变化必须隔离。
    return HasSameMqttSessionIdentity(current, next) &&
           current.provisioning_url == next.provisioning_url &&
           current.aiot_device_secret == next.aiot_device_secret &&
           current.aiot_registered == next.aiot_registered &&
           current.aiot_activated == next.aiot_activated &&
           current.unbind_pending == next.unbind_pending;
}

bool ProjectAppearanceDesired(const cJSON* appearance, char* buffer, size_t capacity) {
    const auto text = [&](const char* name) -> const char* {
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(appearance, name);
        return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : nullptr;
    };
    const auto identifier = [](const char* value, bool allow_empty) {
        if (value == nullptr) return false;
        const size_t length = std::strlen(value);
        if (length > 64 || (!allow_empty && length == 0)) return false;
        for (size_t i = 0; i < length; ++i) {
            const unsigned char ch = value[i];
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return false;
        }
        return true;
    };
    const char* deployment = text("deploymentId");
    const char* release = text("releaseId");
    const char* key = text("keyId");
    const char* mode = text("mode");
    const cJSON* revision = cJSON_GetObjectItemCaseSensitive(appearance, "revision");
    if (!identifier(deployment, false) || !identifier(release, true) || !identifier(key, false) ||
        mode == nullptr || (std::strcmp(mode, "custom") != 0 && std::strcmp(mode, "builtin") != 0) ||
        !cJSON_IsNumber(revision) || !std::isfinite(revision->valuedouble) ||
        std::floor(revision->valuedouble) != revision->valuedouble ||
        revision->valuedouble < 1 || revision->valuedouble > 0xffffffffu) return false;
    const int length = std::snprintf(buffer, capacity,
        "{\"deploymentId\":\"%s\",\"releaseId\":\"%s\",\"keyId\":\"%s\",\"mode\":\"%s\",\"revision\":%u}",
        deployment, release, key, mode, static_cast<unsigned>(revision->valuedouble));
    return length > 0 && static_cast<size_t>(length) < capacity;
}

const char* PeerMessageTypeName(esp_peer_msg_type_t type) {
    switch (type) {
        case ESP_PEER_MSG_TYPE_SDP:
            return "sdp";
        case ESP_PEER_MSG_TYPE_CANDIDATE:
            return "candidate";
        default:
            return "unknown";
    }
}

const char* PeerStateName(esp_peer_state_t state) {
    switch (state) {
        case ESP_PEER_STATE_NEW_CONNECTION:
            return "new";
        case ESP_PEER_STATE_CONNECTING:
            return "connecting";
        case ESP_PEER_STATE_CONNECTED:
            return "connected";
        case ESP_PEER_STATE_DATA_CHANNEL_CONNECTED:
            return "data-channel-connected";
        case ESP_PEER_STATE_DISCONNECTED:
            return "disconnected";
        case ESP_PEER_STATE_CONNECT_FAILED:
            return "failed";
        case ESP_PEER_STATE_CLOSED:
            return "closed";
        default:
            return "unknown";
    }
}

std::string EncodeBase64(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return {};
    }
    // Mbed TLS includes the trailing NUL in the required destination capacity.
    std::string encoded(((size + 2) / 3) * 4 + 1, '\0');
    size_t output_size = 0;
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(encoded.data()), encoded.size(),
                              &output_size, data, size) != 0) {
        return {};
    }
    encoded.resize(output_size);
    return encoded;
}

std::vector<uint8_t> DecodeBase64(const char* data) {
    if (data == nullptr || *data == '\0') {
        return {};
    }
    const size_t input_size = std::strlen(data);
    std::vector<uint8_t> decoded((input_size / 4) * 3 + 3);
    size_t output_size = 0;
    if (mbedtls_base64_decode(decoded.data(), decoded.size(), &output_size,
                              reinterpret_cast<const unsigned char*>(data), input_size) != 0) {
        return {};
    }
    decoded.resize(output_size);
    return decoded;
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
                                       AudioOutputService* audio_output,
                                       BatteryStateProvider* battery_provider,
                                       LightService* light_service)
    : config_service_(config_service),
      ota_update_(ota_update),
      audio_output_(audio_output),
      volume_effect_(audio_output),
      light_effect_(light_service),
      battery_provider_(battery_provider),
      light_service_(light_service) {
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
    message_queue_ = xQueueCreate(8, sizeof(PendingMessage*));
    worker_running_.store(true);
    if (message_queue_ == nullptr ||
        xTaskCreate(WorkerTask, "mqtt_worker", 6144, this, 4, &worker_) != pdPASS) {
        worker_running_.store(false);
        started_.store(false);
        if (message_queue_ != nullptr) {
            vQueueDelete(message_queue_);
            message_queue_ = nullptr;
        }
        ESP_LOGE(TAG, "Cannot reserve MQTT worker (stack=6144 internal_free=%u largest=%u)",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
        return false;
    }
    ESP_LOGI(TAG, "Reserved internal MQTT worker: stack=6144 queue=8");
    force_refresh_.store(true);
    BindOtaProgressPublisher();

    const esp_err_t err = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, NetworkEventHandler, this, &ip_event_instance_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register network listener: %s", esp_err_to_name(err));
        Stop();
        return false;
    }
    ESP_LOGI(TAG, "Waiting for WiFi before starting MQTT");
    wifi_ap_record_t access_point = {};
    if (esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) {
        StartConnectionAsync();
    }
    return true;
}

void UnifiedMqttService::Stop() {
    // A WebRTC stream owns camera/display tasks independently from the MQTT
    // client. Tear both peers down before stopping MQTT so a reconnect or
    // shutdown cannot leave a stale capture task holding the device resource.
    StopWebRtcDisplayStream();
    StopWebRtcCameraStream();
    TimerHandle_t telemetry_timer = nullptr;
    esp_mqtt_client_handle_t client = nullptr;
    bool wake_publisher = false;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!started_.exchange(false)) return;
        connected_.store(false);
        effect_authority_active_ = false;
        AdvanceConnectionEpochLocked();
        telemetry_timer = telemetry_timer_;
        telemetry_timer_ = nullptr;
        client = client_;
        client_ = nullptr;
        publication_event_pending_ = false;
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
    while (worker_running_.load()) {
        vTaskDelay(pdMS_TO_TICKS(kBackgroundTaskPollMs));
    }
    PendingMessage* pending = nullptr;
    while (xQueueReceive(message_queue_, &pending, 0) == pdTRUE) {
        delete pending;
    }
    vQueueDelete(message_queue_);
    message_queue_ = nullptr;
    worker_ = nullptr;
    connecting_.store(false);
    reset_scheduled_.store(false);
    telemetry_pending_.store(false);
    connected_pending_ = false;
    pending_credential_config_.reset();
    credential_restart_pending_ = false;
    credential_refresh_deferred_for_voice_ = false;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        transport_recovery_ = {};
        transport_refresh_scheduled_ = false;
        auth_refresh_pending_ = false;
    }
    std::lock_guard<std::mutex> reliable_lock(reliable_publish_mutex_);
}

void UnifiedMqttService::RequestCredentialRefresh() {
    if (!started_.load()) {
        force_refresh_.store(true);
        return;
    }
    // Serial provisioning may race an in-flight bootstrap task before it has
    // attached a client. Restart unconditionally so that neither an old
    // cached config nor an old MQTT outbox can cross the new boundary.
    ESP_LOGI(TAG, "Provisioning changed cloud configuration; restarting device");
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        effect_authority_active_ = false;
        AdvanceConnectionEpochLocked();
        ResetEffectAuthorityLocked();
    }
    std::fflush(stdout);
    // Give the shared USB console a scheduling window to deliver the marker
    // before reset disconnects the device from the host.
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
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
}

void UnifiedMqttService::WorkerTask(void* arg) {
    auto* service = static_cast<UnifiedMqttService*>(arg);
    service->WorkerLoop();
    service->worker_running_.store(false);
    vTaskDelete(nullptr);
}

void UnifiedMqttService::WorkerLoop() {
    while (started_.load()) {
        MaybeScheduleTransportRecovery();
        if (reset_scheduled_.load() && !ShouldDeferCredentialRefresh()) {
            RefreshCredentials();
        }
        if (connecting_.load() && started_.load()) {
            RunConnection();
        }
        uint32_t generation = 0;
        bool connected_work = false;
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            connected_work = connected_pending_;
            generation = connected_pending_generation_;
            connected_pending_ = false;
        }
        if (connected_work) {
            OnConnected(generation);
            ESP_LOGI(TAG, "MQTT worker stack minimum free=%u bytes",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        }
        if (telemetry_pending_.exchange(false) && started_.load()) {
            PublishTelemetry();
            if (appearance_ != nullptr) appearance_->OnNetworkReady();
        }
        if (appearance_report_pending_.exchange(false) && connected_.load()) PublishShadowReport();
        if (appearance_ == nullptr || !appearance_->IsBusy()) {
            std::string deferred;
            { std::lock_guard<std::mutex> lock(mqtt_mutex_); deferred.swap(deferred_ota_payload_); }
            if (!deferred.empty()) ota_update_.HandleNotification(deferred);
        }
        PendingMessage* raw = nullptr;
        if (xQueueReceive(message_queue_, &raw, pdMS_TO_TICKS(kBackgroundTaskPollMs)) == pdTRUE) {
            std::unique_ptr<PendingMessage> message(raw);
            if (IsCurrentClientGeneration(message->client_generation, message->connection_epoch)) {
                HandleMessage(message->topic, message->payload,
                              message->client_generation, message->connection_epoch);
            }
        }
    }
}

void UnifiedMqttService::RunConnection() {
    int retry_delay_ms = kConnectionRetryInitialMs;
    while (started_.load() && !HasClient()) {
        Connect();
        if (HasClient() || !started_.load()) {
            break;
        }
        ESP_LOGW(TAG, "Unified MQTT setup failed; retrying in %d ms", retry_delay_ms);
        DelayWhileStarted(started_, retry_delay_ms);
        retry_delay_ms = std::min(retry_delay_ms * 2, kConnectionRetryMaxMs);
    }
    connecting_.store(false);
}

void UnifiedMqttService::Connect() {
    auto next = std::unique_ptr<DeviceCloudConfig>(FailResource(ResourceFailure::kMqttConfig)
        ? nullptr : new (std::nothrow) DeviceCloudConfig);
    if (next == nullptr) {
        ESP_LOGE(TAG, "Cannot allocate MQTT bootstrap configuration");
        return;
    }
    DeviceCloudConfig& next_config = *next;
    config_service_.Load(next_config);
    if (force_refresh_.exchange(false) || !next_config.has_mqtt_config ||
        NeedsV2Refresh(next_config)) {
        ESP_LOGI(TAG, "Refreshing bootstrap to obtain unified MQTT v2 credentials");
        auto refreshed = std::unique_ptr<DeviceCloudConfig>(
            new (std::nothrow) DeviceCloudConfig(next_config));
        if (refreshed == nullptr) {
            ESP_LOGE(TAG, "Cannot allocate MQTT bootstrap refresh configuration");
            return;
        }
        if (config_service_.Refresh(*refreshed) && refreshed->has_mqtt_config) {
            next_config = std::move(*refreshed);
        }
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
    esp_mqtt_client_config_t mqtt_config =
        BuildMqttClientConfig(next_config, next_broker_uri, next_client_id);

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
            if (!HasSameEffectAuthority(config_, next_config)) ResetEffectAuthorityLocked();
            config_ = std::move(next_config);
            effect_authority_active_ = !config_.unbind_pending;
            broker_uri_ = std::move(next_broker_uri);
            client_id_ = std::move(next_client_id);
            client_ = client;
            publication_event_pending_ = false;
            generation = ++client_generation_;
            AdvanceConnectionEpochLocked();
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
            publication_event_pending_ = false;
            ++client_generation_;
            AdvanceConnectionEpochLocked();
            destroy_client = true;
        }
    }
    if (err != ESP_OK) {
        if (started_.load() && attached) {
            ESP_LOGE(TAG,
                     "Failed to start MQTT client: %s (internal_free=%u largest=%u)",
                     esp_err_to_name(err),
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
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
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    if (!started_.load()) {
        return;
    }
    // Authentication rejection takes precedence over a pending TCP recovery.
    transport_refresh_scheduled_ = false;
    auth_refresh_pending_ = true;
    bool expected = false;
    if (!reset_scheduled_.compare_exchange_strong(expected, true)) {
        return;
    }
    // Coalesced control work cannot be starved by a full message queue.
}

void UnifiedMqttService::FinishCredentialRefresh() {
    pending_credential_config_.reset();
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    transport_refresh_scheduled_ = false;
    // A rejection received during HTTP or application still needs its own pass.
    reset_scheduled_.store(auth_refresh_pending_);
}

bool UnifiedMqttService::ShouldDeferCredentialRefresh() {
    const bool voice_active = voice_wake_ != nullptr &&
                              voice_wake_->GetState().status ==
                                  VoiceWakeStatus::kAssistantActive;
    if (voice_active && !credential_refresh_deferred_for_voice_) {
        ESP_LOGI(TAG, "MQTT recovery deferred until the active voice session finishes");
    } else if (!voice_active && credential_refresh_deferred_for_voice_) {
        ESP_LOGI(TAG, "Voice session finished; resuming pending MQTT recovery");
    }
    credential_refresh_deferred_for_voice_ = voice_active;
    return voice_active;
}

void UnifiedMqttService::MaybeScheduleTransportRecovery() {
    const int64_t now_ms = esp_timer_get_time() / 1000;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!started_.load() || client_ == nullptr || connected_.load() ||
            transport_recovery_.Decide(now_ms, reset_scheduled_.load(), false) !=
                MqttTransportRecoveryAction::kRefresh) {
            return;
        }
    }
    const bool voice_active = ShouldDeferCredentialRefresh();
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!started_.load() || client_ == nullptr || connected_.load() ||
            transport_recovery_.Decide(now_ms, reset_scheduled_.load(), voice_active) !=
                MqttTransportRecoveryAction::kRefresh) {
            return;
        }
        bool expected = false;
        if (!reset_scheduled_.compare_exchange_strong(expected, true)) {
            return;
        }
        transport_refresh_scheduled_ = true;
    }
    ESP_LOGW(TAG, "MQTT TCP failed at least 3 times; scheduling bootstrap recovery "
                  "(minimum refresh interval=60s)");
}

void UnifiedMqttService::RefreshCredentials() {
    if (credential_restart_pending_) {
        if (started_.load() && !ShouldDeferCredentialRefresh()) {
            ESP_LOGW(TAG, "Restarting to isolate refreshed MQTT session");
            esp_restart();
        }
        return;
    }
    if (pending_credential_config_ == nullptr) {
        DelayWhileStarted(started_, kCredentialRefreshDelayMs);
        if (!started_.load()) {
            reset_scheduled_.store(false);
            return;
        }
        if (ShouldDeferCredentialRefresh()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            if (transport_refresh_scheduled_ && connected_.load()) {
                reset_scheduled_.store(false);
                transport_refresh_scheduled_ = false;
                ESP_LOGI(TAG, "MQTT reconnected before TCP recovery; refresh cancelled");
                return;
            }
            transport_recovery_.MarkRefreshStarted(esp_timer_get_time() / 1000);
            auth_refresh_pending_ = false;
        }
        auto refreshed = std::unique_ptr<DeviceCloudConfig>(
            new (std::nothrow) DeviceCloudConfig);
        if (refreshed == nullptr) {
            ESP_LOGE(TAG, "Cannot allocate MQTT credential refresh snapshot");
            FinishCredentialRefresh();
            return;
        }
        config_service_.Load(*refreshed);
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            if (refreshed->unbind_pending || !refreshed->has_mqtt_config ||
                !HasSameEffectAuthority(config_, *refreshed)) {
                effect_authority_active_ = false;
                AdvanceConnectionEpochLocked();
                ResetEffectAuthorityLocked();
            }
        }
        if (refreshed->unbind_pending) {
            FinishCredentialRefresh();
            return;
        }
        ESP_LOGI(TAG, "Refreshing bootstrap to obtain unified MQTT v2 credentials");
        if (!config_service_.Refresh(*refreshed) || !refreshed->has_mqtt_config) {
            ESP_LOGE(TAG, "MQTT credential refresh failed: %s",
                     config_service_.last_error().c_str());
            FinishCredentialRefresh();
            return;
        }
        // HTTP rotates credentials. Retain this result if voice became active
        // so the worker can keep processing events without repeating the request.
        pending_credential_config_ = std::move(refreshed);
    }
    if (!started_.load() || ShouldDeferCredentialRefresh()) {
        return;
    }
    auto active_snapshot = std::unique_ptr<DeviceCloudConfig>(
        new (std::nothrow) DeviceCloudConfig);
    if (active_snapshot == nullptr) {
        ESP_LOGE(TAG, "Cannot allocate MQTT active configuration snapshot");
        DelayWhileStarted(started_, kCredentialRefreshDelayMs);
        return;
    }
    DeviceCloudConfig& refreshed_config = *pending_credential_config_;
    DeviceCloudConfig& active_config = *active_snapshot;
    // Voice preparation may have refreshed again while application was deferred.
    config_service_.Load(refreshed_config);
    if (!refreshed_config.has_mqtt_config || refreshed_config.unbind_pending) {
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            effect_authority_active_ = false;
            AdvanceConnectionEpochLocked();
            ResetEffectAuthorityLocked();
        }
        ESP_LOGW(TAG, "MQTT recovery cancelled: persisted configuration is unavailable");
        FinishCredentialRefresh();
        return;
    }
    std::string broker_uri = "mqtt://" + refreshed_config.mqtt_broker_address + ":" +
                             std::to_string(refreshed_config.mqtt_broker_port);
    std::string client_id = BuildClientId(refreshed_config.mqtt_device_key);
    esp_mqtt_client_config_t mqtt_config =
        BuildMqttClientConfig(refreshed_config, broker_uri, client_id);

    esp_mqtt_client_handle_t client = nullptr;
    bool client_connected = false;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        client = client_;
        active_config = config_;
        client_connected = connected_.load();
    }
    MqttCredentialRefreshAction action = MqttCredentialRefreshAction::kKeepCurrentClient;
    const bool same_session_identity =
        client != nullptr && HasSameMqttSessionIdentity(active_config, refreshed_config);
    if (!HasSameEffectAuthority(active_config, refreshed_config)) {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        effect_authority_active_ = false;
        AdvanceConnectionEpochLocked();
        ResetEffectAuthorityLocked();
    }
    if (client == nullptr) {
        MqttCredentialRefreshState state;
        state.refresh_succeeded = true;
        action = DecideMqttCredentialRefreshAction(state);
    } else if (!same_session_identity) {
        MqttCredentialRefreshState state;
        state.refresh_succeeded = true;
        state.has_client = true;
        state.outbox_empty = true;
        action = DecideMqttCredentialRefreshAction(state);
        ESP_LOGW(TAG, "MQTT session identity changed; restart required");
    } else {
        std::lock_guard<std::mutex> api_lock(client_api_mutex_);
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            if (!started_.load() || client_ != client) {
                client = nullptr;
            } else {
                client_connected = connected_.load();
            }
        }
        if (client != nullptr) {
            const int outbox_size = client_connected ? 0 : esp_mqtt_client_get_outbox_size(client);
            MqttCredentialRefreshState state;
            state.refresh_succeeded = true;
            state.has_client = true;
            state.client_connected = client_connected;
            state.same_session_identity = true;
            state.outbox_empty = outbox_size <= 0;
            action = DecideMqttCredentialRefreshAction(state);
            if (action == MqttCredentialRefreshAction::kRestart) {
                if (client_connected) {
                    ESP_LOGW(TAG, "MQTT client reconnected during refresh; restart required");
                } else {
                    ESP_LOGW(TAG, "MQTT outbox is not empty (%d bytes); restart required",
                             outbox_size);
                }
            } else {
                bool wake_publisher = false;
                {
                    std::lock_guard<std::mutex> lock(mqtt_mutex_);
                    if (client_ == client) {
                        ++client_generation_;
                        AdvanceConnectionEpochLocked();
                        wake_publisher = reliable_publish_.message_id >= 0;
                        reliable_publish_ = {};
                    }
                }
                if (wake_publisher && publish_ack_semaphore_ != nullptr) {
                    xSemaphoreGive(publish_ack_semaphore_);
                }
                const esp_err_t config_err = esp_mqtt_set_config(client, &mqtt_config);
                if (config_err == ESP_OK) {
                    {
                        std::lock_guard<std::mutex> lock(mqtt_mutex_);
                        if (client_ == client) {
                            if (!HasSameEffectAuthority(config_, refreshed_config))
                                ResetEffectAuthorityLocked();
                            config_ = std::move(refreshed_config);
                            effect_authority_active_ = !config_.unbind_pending;
                            broker_uri_ = std::move(broker_uri);
                            client_id_ = std::move(client_id);
                        }
                    }
                    const esp_err_t reconnect_err = esp_mqtt_client_reconnect(client);
                    if (reconnect_err == ESP_OK) {
                        ESP_LOGI(TAG, "MQTT credentials refreshed; reconnect requested");
                    } else {
                        ESP_LOGE(TAG, "Failed to reconnect refreshed MQTT client: %s",
                                 esp_err_to_name(reconnect_err));
                        action = MqttCredentialRefreshAction::kRestart;
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to apply refreshed MQTT credentials: %s",
                             esp_err_to_name(config_err));
                    action = MqttCredentialRefreshAction::kRestart;
                }
            }
        } else {
            MqttCredentialRefreshState state;
            state.refresh_succeeded = true;
            action = DecideMqttCredentialRefreshAction(state);
        }
    }
    pending_credential_config_.reset();
    if (action == MqttCredentialRefreshAction::kStartConnection && started_.load()) {
        StartConnectionAsync();
    }
    if (action == MqttCredentialRefreshAction::kRestart && started_.load()) {
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            effect_authority_active_ = false;
            AdvanceConnectionEpochLocked();
        }
        credential_restart_pending_ = true;
        if (!ShouldDeferCredentialRefresh()) {
            ESP_LOGW(TAG, "Restarting to isolate refreshed MQTT session");
            esp_restart();
        }
        return;
    }
    FinishCredentialRefresh();
}

bool UnifiedMqttService::HasClient() const {
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    return client_ != nullptr;
}

bool UnifiedMqttService::IsCurrentClientGeneration(uint32_t generation,
                                                   uint64_t connection_epoch) const {
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    return started_.load() && connected_.load() && client_ != nullptr &&
           client_generation_ == generation &&
           (connection_epoch == 0 || connection_epoch_ == connection_epoch);
}

void UnifiedMqttService::AdvanceConnectionEpochLocked() {
    if (++connection_epoch_ == 0) ++connection_epoch_;
    message_assembly_ = {};
    effect_receipts_.clear();
    command_publications_.clear();
    command_publication_bytes_ = 0;
}

void UnifiedMqttService::ResetEffectAuthorityLocked() {
    volume_effect_.ResetAuthority();
    light_effect_.ResetAuthority();
}

bool UnifiedMqttService::SchedulePublicationEventLocked() {
    // Both producers hold client_api_mutex_ and mqtt_mutex_. The SDK overlay
    // only copies this wakeup into its independent zero-timeout custom queue;
    // it must never take the SDK API lock or run callbacks here.
    if (publication_event_pending_) return true;
    esp_mqtt_event_t event = {};
    event.event_id = MQTT_USER_EVENT;
    event.client = client_;
    if (esp_mqtt_dispatch_custom_event(client_, &event) != ESP_OK) return false;
    publication_event_pending_ = true;
    return true;
}

void UnifiedMqttService::QueueEffectReceipt(const std::string& payload,
                                            uint32_t generation, uint64_t connection_epoch) {
    // dispatch_custom_event 只以零超时投递 SDK event queue，不获取 SDK API 锁。
    // client_api_mutex_ 保证指针在投递期间不会被 Stop/destroy 或凭据替换。
    std::lock_guard<std::mutex> api_lock(client_api_mutex_);
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    if (!started_.load() || !connected_.load() || !effect_authority_active_ || client_ == nullptr ||
        client_generation_ != generation || connection_epoch_ != connection_epoch ||
        effect_receipts_.size() >= 8) return;
    effect_receipts_.push_back({generation, connection_epoch,
        "devices/" + config_.mqtt_device_key + "/effects/receipt", payload});
    if (!SchedulePublicationEventLocked()) {
        effect_receipts_.pop_back();
        ESP_LOGW(TAG, "Device effect receipt event queue is unavailable; outcome remains cached");
    }
}

bool UnifiedMqttService::QueueCommandPublication(const CommandPublishContext& context,
                                                const std::string& payload) {
    if (context.ack_topic.empty() || context.ack_topic.size() > kMaxCommandPublicationTopicBytes ||
        payload.size() > kMaxCommandPublicationPayloadBytes) {
        ESP_LOGW(TAG, "Dropping command output: topic or payload exceeds the publication limit");
        return false;
    }
    // Only post a zero-timeout SDK event here. Taking its API lock while holding
    // mqtt_mutex_ would invert the SDK callback's existing lock order.
    std::lock_guard<std::mutex> api_lock(client_api_mutex_);
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    if (!started_.load() || !connected_.load() || client_ == nullptr ||
        client_generation_ != context.client_generation ||
        connection_epoch_ != context.connection_epoch) {
        ESP_LOGW(TAG, "Dropping command output: original MQTT connection is no longer current");
        return false;
    }
    if (command_publications_.size() >= kMaxCommandPublications ||
        payload.size() > kMaxCommandPublicationBytes - command_publication_bytes_) {
        ESP_LOGW(TAG, "Dropping command output: publication queue is full");
        return false;
    }
    command_publications_.push_back({context, payload});
    command_publication_bytes_ += payload.size();
    if (!SchedulePublicationEventLocked()) {
        command_publication_bytes_ -= command_publications_.back().payload.size();
        command_publications_.pop_back();
        ESP_LOGW(TAG, "Dropping command output: SDK event loop is unavailable");
        return false;
    }
    return true;
}

void UnifiedMqttService::DrainCommandPublications(esp_mqtt_client_handle_t event_client) {
    // ESP-MQTT runs its event loop under its recursive API lock, keeping
    // event_client alive and serializing reconnect. Do not acquire client_api_mutex_
    // here: Stop may hold it while waiting for the SDK callback to finish.
    for (size_t attempt = 0; attempt < kMaxCommandPublications; ++attempt) {
        PendingCommandPublication publication;
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            if (!started_.load() || !connected_.load() || client_ != event_client ||
                command_publications_.empty()) return;
            publication = std::move(command_publications_.front());
            command_publications_.pop_front();
            command_publication_bytes_ -= publication.payload.size();
            if (publication.context.client_generation != client_generation_ ||
                publication.context.connection_epoch != connection_epoch_) continue;
        }
        // This admission may finish on the original client after Stop revokes
        // service state. QoS0 direct publish cannot leave an outbox item for a
        // later connection. A write failure can synchronously dispatch DISCONNECTED,
        // so mqtt_mutex_ must be released and no queue/event reference may survive.
        if (esp_mqtt_client_publish(event_client, publication.context.ack_topic.c_str(),
                publication.payload.data(), static_cast<int>(publication.payload.size()), 0, 0) < 0) {
            ESP_LOGW(TAG, "Command output was not sent; it will not be retried on another connection");
        }
    }
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
    constexpr size_t kMaxMqttPayloadBytes = 256 * 1024;
    bool wake_publisher = false;
    bool schedule_message = false;
    bool drain_command_publications = false;
    const esp_mqtt_client_handle_t event_client = event->client;
    uint32_t event_generation = 0;
    uint64_t event_epoch = 0;
    std::string completed_topic;
    std::string completed_payload;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!started_.load() || event->client != client_) {
            return;
        }
        if (event->event_id == MQTT_EVENT_CONNECTED) {
            AdvanceConnectionEpochLocked();
            connected_.store(true);
            transport_recovery_.MarkConnected();
            connected_pending_ = true;
            connected_pending_generation_ = client_generation_;
        } else if (event->event_id == MQTT_EVENT_DISCONNECTED) {
            connected_.store(false);
            AdvanceConnectionEpochLocked();
        } else if (event->event_id == MQTT_USER_EVENT) {
            // The SDK has consumed the shared wakeup. A producer arriving
            // during the bounded drain may now reserve the next one.
            publication_event_pending_ = false;
            drain_command_publications = true;
            // ESP-MQTT 在持 SDK 递归 API 锁时运行此回调；同线程 enqueue 不反转锁序。
            while (!effect_receipts_.empty()) {
                PendingEffectReceipt receipt = std::move(effect_receipts_.front());
                effect_receipts_.pop_front();
                if (connected_.load() && effect_authority_active_ &&
                    receipt.client_generation == client_generation_ &&
                    receipt.connection_epoch == connection_epoch_) {
                    if (esp_mqtt_client_enqueue(client_, receipt.topic.c_str(),
                            receipt.payload.data(), receipt.payload.size(), 0, 0, true) < 0)
                        ESP_LOGW(TAG, "Device effect receipt could not enter the current MQTT outbox");
                }
            }
        } else if (event->event_id == MQTT_EVENT_ERROR && event->error_handle != nullptr &&
                   event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            transport_recovery_.RecordTransportFailure();
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
        } else if (event->event_id == MQTT_EVENT_DATA && connected_.load()) {
            const bool valid_lengths = event->current_data_offset >= 0 &&
                                       event->data_len >= 0 && event->total_data_len >= 0 &&
                                       static_cast<size_t>(event->total_data_len) <=
                                           kMaxMqttPayloadBytes &&
                                       event->current_data_offset <= event->total_data_len &&
                                       event->data_len <=
                                           event->total_data_len - event->current_data_offset;
            const bool starts_message = event->current_data_offset == 0;
            if (starts_message) {
                message_assembly_ = {};
                if (valid_lengths) {
                    message_assembly_.active = true;
                    message_assembly_.client_generation = client_generation_;
                    message_assembly_.connection_epoch = connection_epoch_;
                    message_assembly_.total_length =
                        static_cast<size_t>(event->total_data_len);
                    if (event->topic != nullptr && event->topic_len > 0) {
                        message_assembly_.topic.assign(event->topic,
                                                       static_cast<size_t>(event->topic_len));
                    }
                    message_assembly_.payload.reserve(message_assembly_.total_length);
                    if (event->data != nullptr && event->data_len > 0) {
                        message_assembly_.payload.append(
                            event->data, static_cast<size_t>(event->data_len));
                    }
                }
            } else if (valid_lengths && message_assembly_.active &&
                       message_assembly_.client_generation == client_generation_ &&
                       message_assembly_.connection_epoch == connection_epoch_ &&
                       message_assembly_.total_length ==
                           static_cast<size_t>(event->total_data_len) &&
                       message_assembly_.payload.size() ==
                           static_cast<size_t>(event->current_data_offset)) {
                if (event->data != nullptr && event->data_len > 0) {
                    message_assembly_.payload.append(
                        event->data, static_cast<size_t>(event->data_len));
                }
            } else {
                message_assembly_ = {};
            }

            if (message_assembly_.active && message_assembly_.payload.size() ==
                                                 message_assembly_.total_length) {
                schedule_message = true;
                event_generation = message_assembly_.client_generation;
                event_epoch = message_assembly_.connection_epoch;
                completed_topic = std::move(message_assembly_.topic);
                completed_payload = std::move(message_assembly_.payload);
                message_assembly_ = {};
            }
        }
    }
    if (wake_publisher && publish_ack_semaphore_ != nullptr) {
        xSemaphoreGive(publish_ack_semaphore_);
    }
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Unified MQTT connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Unified MQTT disconnected; ESP-MQTT will reconnect");
            break;
        case MQTT_EVENT_DATA:
            if (!schedule_message) {
                ESP_LOGW(TAG, "Ignoring invalid or incomplete MQTT payload fragment");
                break;
            }
            {
                auto context = std::unique_ptr<PendingMessage>(
                    new (std::nothrow) PendingMessage{
                        event_generation,
                        event_epoch,
                        std::move(completed_topic),
                        std::move(completed_payload),
                    });
                PendingMessage* pending = context.get();
                if (pending == nullptr || xQueueSend(message_queue_, &pending, 0) != pdTRUE) {
                    ESP_LOGE(TAG, "MQTT message dropped: worker queue full or allocation failed");
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
    if (drain_command_publications) DrainCommandPublications(event_client);
}

void UnifiedMqttService::OnConnected(uint32_t generation) {
    auto* service = this;
    if (service != nullptr && service->IsCurrentClientGeneration(generation)) {
        service->SubscribeTopics();
        if (service->IsCurrentClientGeneration(generation)) {
            service->ota_update_.OnNetworkReady();
            if (service->appearance_ != nullptr) service->appearance_->OnNetworkReady();
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
                                       const std::string& payload, uint32_t generation,
                                       uint64_t connection_epoch) {
    const std::string ota_topic = CopyTopic(&DeviceCloudConfig::mqtt_topic_ota_notify);
    const std::string shadow_topic = CopyTopic(&DeviceCloudConfig::mqtt_topic_shadow_desired);
    const std::string commands_topic = CopyTopic(&DeviceCloudConfig::mqtt_topic_commands);
    const std::string pc_status_topic = CopyTopic(&DeviceCloudConfig::mqtt_topic_pc_status);
    if (topic == ota_topic) {
        if (appearance_ != nullptr && appearance_->IsBusy()) {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            deferred_ota_payload_ = payload;
            return;
        }
        ota_update_.HandleNotification(payload);
        return;
    }
    if (topic == shadow_topic) {
        ApplyDesiredShadow(payload, generation, connection_epoch);
        return;
    }
    if (topic == pc_status_topic) {
        HandlePcStatus(payload);
        return;
    }
    std::string command_no;
    if (ExtractCommandNo(topic, commands_topic, command_no)) {
        const CommandPublishContext context{generation, connection_epoch, topic + "/ack"};
        HandleCommand(command_no, payload, context);
    }
}

void UnifiedMqttService::ApplyDesiredShadow(const std::string& payload,
                                           uint32_t generation, uint64_t connection_epoch) {
    if (payload.size() > 256 * 1024 || !IsBoundedRealtimeVoiceControlJson(payload, 16)) return;
    cJSON* root = cJSON_ParseWithLengthOpts(payload.c_str(), payload.size() + 1, nullptr, true);
    if (!HasUniqueJsonKeys(root)) {
        cJSON_Delete(root);
        return;
    }
    const cJSON* meta = cJSON_GetObjectItemCaseSensitive(root, "_meta");
    if (meta != nullptr && !HasUniqueJsonKeys(meta)) {
        cJSON_Delete(root);
        return;
    }
    const cJSON* correlation = cJSON_GetObjectItemCaseSensitive(meta, "rodak/deviceEffect");
    const cJSON* schema = cJSON_GetObjectItemCaseSensitive(correlation, "schema");
    const bool correlated = correlation != nullptr || (meta != nullptr && !cJSON_IsObject(meta));
    std::string receipt;
    bool volume_handled = false;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!started_.load() || !connected_.load() || !effect_authority_active_ || client_ == nullptr ||
            client_generation_ != generation || connection_epoch_ != connection_epoch) {
            cJSON_Delete(root);
            return;
        }
        // A correlated frame grants only its named effect; old desired fields cannot
        // fall through into another domain, even when the metadata is malformed.
        if (!correlated) {
            volume_effect_.Handle(payload, config_.mqtt_device_key, volume_handled);
            light_effect_.Handle(payload, config_.mqtt_device_key);
        } else if (cJSON_IsString(schema) && schema->valuestring != nullptr) {
            if (std::strcmp(schema->valuestring, "rodak.mqtt-volume-effect.v1") == 0)
                receipt = volume_effect_.Handle(payload, config_.mqtt_device_key, volume_handled);
            else if (std::strcmp(schema->valuestring, "rodak.mqtt-light-effect.v1") == 0)
                receipt = light_effect_.Handle(payload, config_.mqtt_device_key);
        }
    }
    if (!receipt.empty()) QueueEffectReceipt(receipt, generation, connection_epoch);
    if (correlated) {
        PublishShadowReport();
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
    cJSON* appearance = cJSON_IsObject(desired)
                            ? cJSON_GetObjectItemCaseSensitive(desired, "appearance") : nullptr;
    if (cJSON_IsObject(appearance) && appearance_ != nullptr) {
        char encoded_appearance[384] = {};
        if (ProjectAppearanceDesired(appearance, encoded_appearance, sizeof(encoded_appearance))) {
            appearance_->ApplyDesiredJson(encoded_appearance);
        }
    }
    cJSON* light = cJSON_IsObject(desired)
                       ? cJSON_GetObjectItemCaseSensitive(desired, "light")
                       : nullptr;
    cJSON* voice_identity = cJSON_IsObject(desired)
                                ? cJSON_GetObjectItemCaseSensitive(desired, "voice_identity")
                                : nullptr;
    bool voice_identity_changed = false;
    if (cJSON_IsObject(voice_identity) && voice_wake_ != nullptr) {
        VoiceIdentityConfig config;
        const VoiceIdentityConfig defaults = DefaultVoiceIdentityConfig();
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(voice_identity, "name");
        const cJSON* wake_word = cJSON_GetObjectItemCaseSensitive(voice_identity, "wakeWord");
        const cJSON* wake_command = cJSON_GetObjectItemCaseSensitive(voice_identity, "wakeCommand");
        const cJSON* mode = cJSON_GetObjectItemCaseSensitive(voice_identity, "mode");
        const cJSON* revision = cJSON_GetObjectItemCaseSensitive(voice_identity, "revision");
        const cJSON* expires_at_ms = cJSON_GetObjectItemCaseSensitive(voice_identity, "expiresAtMs");
        config.name = cJSON_IsString(name) ? name->valuestring : defaults.name;
        config.wake_word = cJSON_IsString(wake_word) ? wake_word->valuestring : defaults.wake_word;
        config.wake_command = cJSON_IsString(wake_command) ? wake_command->valuestring : defaults.wake_command;
        config.mode = cJSON_IsString(mode) && std::string(mode->valuestring) == "temporary"
                          ? VoiceIdentityApplyMode::kTemporary
                          : VoiceIdentityApplyMode::kPersistent;
        config.revision = cJSON_IsNumber(revision) && revision->valueint > 0
                              ? static_cast<uint32_t>(revision->valueint)
                              : 1;
        config.expires_at_ms = cJSON_IsNumber(expires_at_ms)
                                   ? static_cast<int64_t>(expires_at_ms->valuedouble)
                                   : 0;
        std::string error;
        voice_identity_changed = voice_wake_->ApplyVoiceIdentity(config, error);
        if (!voice_identity_changed) {
            ESP_LOGW(TAG, "Rejected desired voice identity: %s", error.c_str());
        }
    }
    if (cJSON_IsNumber(volume) || cJSON_IsObject(light) || cJSON_IsObject(voice_identity) || cJSON_IsObject(appearance)) {
        PublishShadowReport();
    }
    cJSON_Delete(root);
}

void UnifiedMqttService::ReconnectAfterCredentialChange() {
    if (!started_.load()) {
        if (!Start()) {
            ESP_LOGE(TAG, "Failed to restart MQTT service after device binding");
        }
        return;
    }
    StartConnectionAsync();
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

void UnifiedMqttService::StopWebRtcCameraStream() {
    if (web_rtc_camera_service_ != nullptr) {
        web_rtc_camera_service_->Stop();
    }
    std::lock_guard<std::mutex> lock(camera_mutex_);
    camera_session_id_.clear();
}

void UnifiedMqttService::StopWebRtcDisplayStream() {
    if (web_rtc_display_service_ != nullptr) {
        web_rtc_display_service_->Stop();
    }
    std::lock_guard<std::mutex> lock(camera_mutex_);
    display_session_id_.clear();
}

void UnifiedMqttService::HandleCommand(const std::string& command_no,
                                       const std::string& payload,
                                       const CommandPublishContext& context) {
    cJSON* request = cJSON_Parse(payload.c_str());
    std::string command;
    if (cJSON_IsObject(request)) {
        const cJSON* command_json = cJSON_GetObjectItemCaseSensitive(request, "command");
        if (!cJSON_IsString(command_json)) {
            command_json = cJSON_GetObjectItemCaseSensitive(request, "type");
        }
        if (cJSON_IsString(command_json) && command_json->valuestring != nullptr) {
            command = command_json->valuestring;
        }
    } else if (payload == "ping") {
        command = "ping";
    }

    bool handled = false;
    bool success = false;
    std::string error_code;
    cJSON* result = cJSON_CreateObject();
    if (command == "camera.stream.start" || command == "camera.stream.stop" || command == "camera.stream.signal") {
        handled = true;
        if (web_rtc_camera_service_ == nullptr) {
            error_code = "camera_stream_unavailable";
        } else if (!cJSON_IsObject(request)) {
            error_code = "invalid_payload";
        } else {
            const cJSON* session_json = cJSON_GetObjectItemCaseSensitive(request, "sessionId");
            const std::string session_id =
                cJSON_IsString(session_json) && session_json->valuestring != nullptr
                    ? session_json->valuestring
                    : std::string();
            if (session_id.empty()) {
                error_code = "missing_session_id";
            } else if (command == "camera.stream.start") {
                WebRtcCameraService::Config config;
                const cJSON* width = cJSON_GetObjectItemCaseSensitive(request, "width");
                const cJSON* height = cJSON_GetObjectItemCaseSensitive(request, "height");
                const cJSON* fps = cJSON_GetObjectItemCaseSensitive(request, "fps");
                const cJSON* chunk_size = cJSON_GetObjectItemCaseSensitive(request, "chunkSize");
                if (cJSON_IsNumber(width)) config.width = std::clamp(width->valueint, 160, 1280);
                if (cJSON_IsNumber(height)) config.height = std::clamp(height->valueint, 120, 960);
                if (cJSON_IsNumber(fps)) config.fps = static_cast<uint8_t>(std::clamp(fps->valueint, 1, 15));
                if (cJSON_IsNumber(chunk_size)) {
                    // Camera stream keeps its historical 10KB framing for
                    // compatibility with existing receivers.
                    config.chunk_size = static_cast<uint16_t>(std::clamp(chunk_size->valueint, 1024, 10000));
                }

                auto publish_signal = [this, context, session_id](const char* event,
                                                                         const char* type,
                                                                         const uint8_t* data,
                                                                         size_t size) {
                    cJSON* signal = cJSON_CreateObject();
                    cJSON_AddStringToObject(signal, "status", "ok");
                    cJSON* result = cJSON_CreateObject();
                    cJSON* camera_stream = cJSON_CreateObject();
                    cJSON_AddStringToObject(camera_stream, "sessionId", session_id.c_str());
                    cJSON_AddStringToObject(camera_stream, "event", event);
                    if (type != nullptr) cJSON_AddStringToObject(camera_stream, "type", type);
                    if (data != nullptr && size > 0) {
                        const std::string encoded = EncodeBase64(data, size);
                        if (!encoded.empty()) {
                            cJSON_AddStringToObject(camera_stream, "data", encoded.c_str());
                        }
                    }
                    cJSON_AddItemToObject(result, "cameraStream", camera_stream);
                    cJSON_AddItemToObject(signal, "result", result);
                    const std::string encoded = EncodeJson(signal);
                    cJSON_Delete(signal);
                    QueueCommandPublication(context, encoded);
                };
                auto on_signaling = [publish_signal](esp_peer_msg_type_t type,
                                                     std::vector<uint8_t>&& data) {
                    while (!data.empty() && data.back() == 0) data.pop_back();
                    publish_signal("signal", PeerMessageTypeName(type), data.data(), data.size());
                };
                auto on_state = [this, publish_signal, session_id](esp_peer_state_t state) {
                    publish_signal("state", PeerStateName(state), nullptr, 0);
                    if (state == ESP_PEER_STATE_CLOSED || state == ESP_PEER_STATE_CONNECT_FAILED ||
                        state == ESP_PEER_STATE_DISCONNECTED ||
                        state == ESP_PEER_STATE_DATA_CHANNEL_CLOSED ||
                        state == ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED) {
                        std::lock_guard<std::mutex> lock(camera_mutex_);
                        if (camera_session_id_ == session_id) {
                            camera_session_id_.clear();
                        }
                    }
                };

                bool already_running = false;
                {
                    std::lock_guard<std::mutex> lock(camera_mutex_);
                    already_running = !camera_session_id_.empty() || !display_session_id_.empty();
                    if (!already_running) {
                        // Start may synchronously emit signaling or terminal
                        // callbacks, so reserve its session before calling it.
                        camera_session_id_ = session_id;
                    }
                }
                if (already_running) {
                    error_code = "camera_stream_busy";
                } else if (web_rtc_camera_service_->Start(config, std::move(on_signaling),
                                                          std::move(on_state))) {
                    success = true;
                    cJSON_AddStringToObject(result, "sessionId", session_id.c_str());
                    cJSON_AddStringToObject(result, "transport", "webrtc-datachannel");
                } else {
                    std::lock_guard<std::mutex> lock(camera_mutex_);
                    if (camera_session_id_ == session_id) {
                        camera_session_id_.clear();
                    }
                    error_code = "camera_stream_start_failed";
                }
            } else if (command == "camera.stream.stop") {
                bool matches = false;
                {
                    std::lock_guard<std::mutex> lock(camera_mutex_);
                    matches = camera_session_id_ == session_id;
                }
                if (!matches) {
                    error_code = "camera_stream_not_found";
                } else {
                    web_rtc_camera_service_->Stop();
                    {
                        std::lock_guard<std::mutex> lock(camera_mutex_);
                        camera_session_id_.clear();
                    }
                    success = true;
                    cJSON_AddStringToObject(result, "sessionId", session_id.c_str());
                }
            } else {
                const cJSON* type_json = cJSON_GetObjectItemCaseSensitive(request, "type");
                const cJSON* data_json = cJSON_GetObjectItemCaseSensitive(request, "data");
                const bool valid_type = cJSON_IsString(type_json) && type_json->valuestring != nullptr;
                const bool valid_data = cJSON_IsString(data_json) && data_json->valuestring != nullptr;
                esp_peer_msg_type_t message_type = ESP_PEER_MSG_TYPE_NONE;
                if (valid_type && std::string(type_json->valuestring) == "sdp") {
                    message_type = ESP_PEER_MSG_TYPE_SDP;
                } else if (valid_type && std::string(type_json->valuestring) == "candidate") {
                    message_type = ESP_PEER_MSG_TYPE_CANDIDATE;
                }
                bool matches = false;
                {
                    std::lock_guard<std::mutex> lock(camera_mutex_);
                    matches = camera_session_id_ == session_id;
                }
                if (!matches) {
                    error_code = "camera_stream_not_found";
                } else if (!valid_data || message_type == ESP_PEER_MSG_TYPE_NONE) {
                    error_code = "invalid_signal";
                } else {
                    std::vector<uint8_t> decoded = DecodeBase64(data_json->valuestring);
                    if (decoded.empty()) {
                        error_code = "invalid_signal_encoding";
                    } else if (!web_rtc_camera_service_->HandleRemoteMessage(message_type, decoded)) {
                        error_code = "camera_signal_rejected";
                    } else {
                        success = true;
                        cJSON_AddStringToObject(result, "sessionId", session_id.c_str());
                    }
                }
                /* Keep the branch above explicit so malformed base64 never reaches esp_peer. */
                if (!success && error_code.empty() && message_type != ESP_PEER_MSG_TYPE_NONE) {
                    error_code = "camera_signal_rejected";
                }
            }
        }
    } else if (command == "display.stream.start" || command == "display.stream.stop" || command == "display.stream.signal") {
        handled = true;
        if (web_rtc_display_service_ == nullptr) {
            error_code = "display_stream_unavailable";
        } else if (!cJSON_IsObject(request)) {
            error_code = "invalid_payload";
        } else {
            const cJSON* session_json = cJSON_GetObjectItemCaseSensitive(request, "sessionId");
            const std::string session_id =
                cJSON_IsString(session_json) && session_json->valuestring != nullptr
                    ? session_json->valuestring
                    : std::string();
            if (session_id.empty()) {
                error_code = "missing_session_id";
            } else if (command == "display.stream.start") {
                WebRtcDisplayService::Config config;
                const cJSON* width = cJSON_GetObjectItemCaseSensitive(request, "width");
                const cJSON* height = cJSON_GetObjectItemCaseSensitive(request, "height");
                const cJSON* fps = cJSON_GetObjectItemCaseSensitive(request, "fps");
                const cJSON* chunk_size = cJSON_GetObjectItemCaseSensitive(request, "chunkSize");
                if (cJSON_IsNumber(width)) config.width = std::clamp(width->valueint, 160, 1280);
                if (cJSON_IsNumber(height)) config.height = std::clamp(height->valueint, 120, 960);
                if (cJSON_IsNumber(fps)) config.fps = static_cast<uint8_t>(std::clamp(fps->valueint, 1, 15));
                if (cJSON_IsNumber(chunk_size)) {
                    // A quality-65 320x240 screen JPEG normally fits one
                    // 20KB chunk, reducing SCTP/main-loop round trips.
                    config.chunk_size = static_cast<uint16_t>(std::clamp(chunk_size->valueint, 1024, 20000));
                }

                auto publish_signal = [this, context, session_id](const char* event,
                                                                         const char* type,
                                                                         const uint8_t* data,
                                                                         size_t size) {
                    cJSON* signal = cJSON_CreateObject();
                    cJSON_AddStringToObject(signal, "status", "ok");
                    cJSON* result = cJSON_CreateObject();
                    cJSON* display_stream = cJSON_CreateObject();
                    cJSON_AddStringToObject(display_stream, "sessionId", session_id.c_str());
                    cJSON_AddStringToObject(display_stream, "event", event);
                    if (type != nullptr) cJSON_AddStringToObject(display_stream, "type", type);
                    if (data != nullptr && size > 0) {
                        const std::string encoded = EncodeBase64(data, size);
                        if (!encoded.empty()) {
                            cJSON_AddStringToObject(display_stream, "data", encoded.c_str());
                        }
                    }
                    cJSON_AddItemToObject(result, "displayStream", display_stream);
                    cJSON_AddItemToObject(signal, "result", result);
                    const std::string encoded = EncodeJson(signal);
                    cJSON_Delete(signal);
                    QueueCommandPublication(context, encoded);
                };
                auto on_signaling = [publish_signal](esp_peer_msg_type_t type,
                                                     std::vector<uint8_t>&& data) {
                    while (!data.empty() && data.back() == 0) data.pop_back();
                    publish_signal("signal", PeerMessageTypeName(type), data.data(), data.size());
                };
                auto on_state = [this, publish_signal, session_id](esp_peer_state_t state) {
                    publish_signal("state", PeerStateName(state), nullptr, 0);
                    if (state == ESP_PEER_STATE_CLOSED || state == ESP_PEER_STATE_CONNECT_FAILED ||
                        state == ESP_PEER_STATE_DISCONNECTED ||
                        state == ESP_PEER_STATE_DATA_CHANNEL_CLOSED ||
                        state == ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED) {
                        std::lock_guard<std::mutex> lock(camera_mutex_);
                        if (display_session_id_ == session_id) {
                            display_session_id_.clear();
                        }
                    }
                };

                bool already_running = false;
                {
                    std::lock_guard<std::mutex> lock(camera_mutex_);
                    already_running = !display_session_id_.empty() || !camera_session_id_.empty();
                    if (!already_running) {
                        // Start may synchronously emit signaling or terminal
                        // callbacks, so reserve its session before calling it.
                        display_session_id_ = session_id;
                    }
                }
                if (already_running) {
                    error_code = "display_stream_busy";
                } else if (web_rtc_display_service_->Start(config, std::move(on_signaling),
                                                          std::move(on_state),
                                                          display_control_callback_)) {
                    success = true;
                    cJSON_AddStringToObject(result, "sessionId", session_id.c_str());
                    cJSON_AddStringToObject(result, "transport", "webrtc-datachannel");
                } else {
                    std::lock_guard<std::mutex> lock(camera_mutex_);
                    if (display_session_id_ == session_id) {
                        display_session_id_.clear();
                    }
                    error_code = "display_stream_start_failed";
                }
            } else if (command == "display.stream.stop") {
                bool matches = false;
                {
                    std::lock_guard<std::mutex> lock(camera_mutex_);
                    matches = display_session_id_ == session_id;
                }
                if (!matches) {
                    error_code = "display_stream_not_found";
                } else {
                    web_rtc_display_service_->Stop();
                    {
                        std::lock_guard<std::mutex> lock(camera_mutex_);
                        display_session_id_.clear();
                    }
                    success = true;
                    cJSON_AddStringToObject(result, "sessionId", session_id.c_str());
                }
            } else {
                const cJSON* type_json = cJSON_GetObjectItemCaseSensitive(request, "type");
                const cJSON* data_json = cJSON_GetObjectItemCaseSensitive(request, "data");
                const bool valid_type = cJSON_IsString(type_json) && type_json->valuestring != nullptr;
                const bool valid_data = cJSON_IsString(data_json) && data_json->valuestring != nullptr;
                esp_peer_msg_type_t message_type = ESP_PEER_MSG_TYPE_NONE;
                if (valid_type && std::string(type_json->valuestring) == "sdp") {
                    message_type = ESP_PEER_MSG_TYPE_SDP;
                } else if (valid_type && std::string(type_json->valuestring) == "candidate") {
                    message_type = ESP_PEER_MSG_TYPE_CANDIDATE;
                }
                bool matches = false;
                {
                    std::lock_guard<std::mutex> lock(camera_mutex_);
                    matches = display_session_id_ == session_id;
                }
                if (!matches) {
                    error_code = "display_stream_not_found";
                } else if (!valid_data || message_type == ESP_PEER_MSG_TYPE_NONE) {
                    error_code = "invalid_signal";
                } else {
                    std::vector<uint8_t> decoded = DecodeBase64(data_json->valuestring);
                    if (decoded.empty()) {
                        error_code = "invalid_signal_encoding";
                    } else if (!web_rtc_display_service_->HandleRemoteMessage(message_type, decoded)) {
                        error_code = "display_signal_rejected";
                    } else {
                        success = true;
                        cJSON_AddStringToObject(result, "sessionId", session_id.c_str());
                    }
                }
                /* Keep the branch above explicit so malformed base64 never reaches esp_peer. */
                if (!success && error_code.empty() && message_type != ESP_PEER_MSG_TYPE_NONE) {
                    error_code = "display_signal_rejected";
                }
            }
        }
    } else if (command == "ping" || IsPingCommand(payload)) {
        handled = true;
        success = true;
        cJSON_AddBoolToObject(result, "pong", true);
        const esp_app_desc_t* app = esp_app_get_description();
        cJSON_AddStringToObject(result, "firmware", app != nullptr ? app->version : "unknown");
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", success ? "ok" : "error");
    if (success) {
        cJSON_AddItemToObject(root, "result", result);
    } else {
        cJSON_Delete(result);
        cJSON_AddStringToObject(root, "errorCode",
                                handled && !error_code.empty() ? error_code.c_str()
                                                               : "unsupported_command");
    }
    cJSON_Delete(request);
    const std::string ack_payload = EncodeJson(root);
    cJSON_Delete(root);

    if (QueueCommandPublication(context, ack_payload)) {
        ESP_LOGI(TAG, "Command %s response queued for its original connection: %s", command_no.c_str(),
                 success ? "ok" : (handled ? "rejected" : "unsupported"));
    } else {
        ESP_LOGW(TAG, "Failed to acknowledge command %s", command_no.c_str());
    }
}

void UnifiedMqttService::TelemetryTimerCallback(TimerHandle_t timer) {
    auto* service = static_cast<UnifiedMqttService*>(pvTimerGetTimerID(timer));
    if (service != nullptr && service->started_.load()) {
        service->telemetry_pending_.store(true);
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
    const BatterySnapshot battery = battery_provider_ != nullptr
                                        ? battery_provider_->Read()
                                        : fallback_battery_monitor_.Read();
    if (battery.level_percent >= 0) {
        cJSON_AddNumberToObject(root, "battery", battery.level_percent);
    }
    if (battery.charging_valid) {
        cJSON_AddBoolToObject(root, "charging", battery.charging);
    }
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "minimum_free_heap_size", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(root, "uptime_ms",
                           static_cast<double>(esp_timer_get_time() / 1000));
    cJSON_AddStringToObject(root, "ota_slot", running != nullptr ? running->label : "unknown");
    const std::string payload = EncodeJson(root);
    cJSON_Delete(root);
    const bool telemetry_queued = Publish(CopyTopic(&DeviceCloudConfig::mqtt_topic_telemetry), payload);
    ESP_LOGI(TAG, "MQTT health: connected=%d internal_free=%u internal_largest=%u stack_min_free=%u "
                 "psram_free=%u psram_largest=%u telemetry_queued=%d",
             connected_.load(),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             telemetry_queued);
}

void UnifiedMqttService::PublishShadowReport() {
    const esp_app_desc_t* app = esp_app_get_description();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "firmware", app != nullptr ? app->version : "unknown");
    if (appearance_ != nullptr) {
        cJSON* appearance = cJSON_Parse(appearance_->ReportedJson().c_str());
        if (cJSON_IsObject(appearance)) cJSON_AddItemToObject(root, "appearance", appearance);
        else cJSON_Delete(appearance);
    }
    if (audio_output_ != nullptr) {
        cJSON_AddNumberToObject(root, "volume", audio_output_->volume());
    }
    if (light_service_ != nullptr) {
        LightState light;
        if (light_service_->GetLight(0, light)) {
            cJSON* light_json = cJSON_CreateObject();
            cJSON_AddStringToObject(light_json, "id", light.id.c_str());
            cJSON_AddBoolToObject(light_json, "available", light.available);
            cJSON_AddNumberToObject(light_json, "last_error", light.last_error);
            cJSON_AddNumberToObject(light_json, "configurationRevision", light.configuration_revision);
            cJSON_AddStringToObject(light_json, "application",
                light.application == LightApplication::kDriverApplied ? "driver-applied" : "unverified");
            cJSON_AddBoolToObject(light_json, "enabled", light.enabled);
            cJSON_AddNumberToObject(light_json, "brightness", light.brightness_percent);
            cJSON* color = cJSON_CreateObject();
            cJSON_AddNumberToObject(color, "r", light.color.red);
            cJSON_AddNumberToObject(color, "g", light.color.green);
            cJSON_AddNumberToObject(color, "b", light.color.blue);
            cJSON_AddItemToObject(light_json, "color", color);
            cJSON_AddItemToObject(root, "light", light_json);
        }
    }
    const BatterySnapshot battery = battery_provider_ != nullptr
                                        ? battery_provider_->Read()
                                        : fallback_battery_monitor_.Read();
    if (battery.level_percent >= 0) {
        cJSON_AddNumberToObject(root, "battery", battery.level_percent);
    }
    if (battery.charging_valid) {
        cJSON_AddBoolToObject(root, "charging", battery.charging);
    }
    if (voice_wake_ != nullptr) {
        const VoiceWakeState state = voice_wake_->GetState();
        const VoiceIdentityConfig& identity = state.voice_identity;
        cJSON* identity_json = cJSON_CreateObject();
        cJSON_AddStringToObject(identity_json, "name", identity.name.c_str());
        cJSON_AddStringToObject(identity_json, "wakeWord", identity.wake_word.c_str());
        cJSON_AddStringToObject(identity_json, "wakeCommand", identity.wake_command.c_str());
        cJSON_AddStringToObject(identity_json, "mode",
                                identity.mode == VoiceIdentityApplyMode::kTemporary
                                    ? "temporary"
                                    : "persistent");
        cJSON_AddNumberToObject(identity_json, "revision", identity.revision);
        if (identity.expires_at_ms > 0) {
            cJSON_AddNumberToObject(identity_json, "expiresAtMs", identity.expires_at_ms);
        }
        cJSON_AddStringToObject(identity_json, "status", state.voice_identity_status.c_str());
        cJSON_AddStringToObject(identity_json, "runtime", state.runtime_name.c_str());
        cJSON_AddStringToObject(identity_json, "model", "multinet5q8_cn");
        if (!state.voice_identity_error.empty()) {
            cJSON_AddStringToObject(identity_json, "error", state.voice_identity_error.c_str());
        }
        cJSON_AddItemToObject(root, "voice_identity", identity_json);
    }
    const std::string payload = EncodeJson(root);
    cJSON_Delete(root);
    Publish(CopyTopic(&DeviceCloudConfig::mqtt_topic_shadow_report), payload);
}

void UnifiedMqttService::SetAppearanceService(AppearanceService* appearance) {
    if (appearance_ != nullptr) appearance_->SetStatePublisher({});
    appearance_ = appearance;
    if (appearance_ != nullptr) appearance_->SetStatePublisher([this]() { appearance_report_pending_.store(true); });
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

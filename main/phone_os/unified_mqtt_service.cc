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
#include <esp_netif.h>
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
#include <inttypes.h>
#include <limits>
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

bool HasCommandJsonNul(const std::string& json) {
    for (size_t i = 0; i < json.size(); ++i) {
        if (json[i] == '\0') return true;
        if (json[i] == '\\') {
            if (json.compare(i, 6, "\\u0000") == 0) return true;
            ++i;
        }
    }
    return false;
}

cJSON* ParseCommandJson(const std::string& payload) {
    const char* end = nullptr;
    cJSON* result = cJSON_ParseWithLengthOpts(payload.c_str(), payload.size() + 1, &end, true);
    if (end != payload.c_str() + payload.size()) {
        cJSON_Delete(result);
        return nullptr;
    }
    return result;
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

bool ParseVoiceIdentityDesired(const cJSON* source, VoiceIdentityConfig& config,
                                std::string& error) {
    if (!HasUniqueJsonKeys(source)) {
        error = "voice identity must be an object with unique fields";
        return false;
    }
    config = DefaultVoiceIdentityConfig();
    for (const auto& field : {std::pair<const char*, std::string*>{"name", &config.name},
            {"wakeWord", &config.wake_word}, {"wakeCommand", &config.wake_command}}) {
        const auto* value = cJSON_GetObjectItemCaseSensitive(source, field.first);
        if (value == nullptr) continue;
        if (!cJSON_IsString(value)) {
            error = "voice identity text fields must be strings";
            return false;
        }
        *field.second = value->valuestring;
    }
    const auto* mode = cJSON_GetObjectItemCaseSensitive(source, "mode");
    if (mode != nullptr) {
        if (!cJSON_IsString(mode) || (std::strcmp(mode->valuestring, "persistent") != 0 &&
                                      std::strcmp(mode->valuestring, "temporary") != 0)) {
            error = "voice identity mode is invalid";
            return false;
        }
        if (std::strcmp(mode->valuestring, "temporary") == 0)
            config.mode = VoiceIdentityApplyMode::kTemporary;
    }
    const auto* revision = cJSON_GetObjectItemCaseSensitive(source, "revision");
    if (revision != nullptr) {
        if (!cJSON_IsNumber(revision) || !std::isfinite(revision->valuedouble) ||
            revision->valuedouble < 1 || revision->valuedouble > 4294967295.0 ||
            std::floor(revision->valuedouble) != revision->valuedouble) {
            error = "voice identity revision must be a positive uint32 integer";
            return false;
        }
        config.revision = static_cast<uint32_t>(revision->valuedouble);
    }
    const auto* expiry = cJSON_GetObjectItemCaseSensitive(source, "expiresAtMs");
    if (expiry != nullptr) {
        if (!cJSON_IsNumber(expiry) || !std::isfinite(expiry->valuedouble) ||
            expiry->valuedouble < 0 || expiry->valuedouble > 9007199254740991.0 ||
            std::floor(expiry->valuedouble) != expiry->valuedouble) {
            error = "voice identity expiry must be a safe Unix millisecond integer";
            return false;
        }
        config.expires_at_ms = static_cast<int64_t>(expiry->valuedouble);
    }
    VoiceIdentityConfig normalized;
    if (!NormalizeVoiceIdentityConfig(config, normalized, error)) return false;
    config = std::move(normalized);
    return true;
}

cJSON* BuildVoiceIdentityReport(const VoiceWakeState& state) {
    const auto& identity = state.voice_identity;
    cJSON* report = cJSON_CreateObject();
    cJSON_AddStringToObject(report, "name", identity.name.c_str());
    cJSON_AddStringToObject(report, "wakeWord", identity.wake_word.c_str());
    cJSON_AddStringToObject(report, "wakeCommand", identity.wake_command.c_str());
    cJSON_AddStringToObject(report, "mode",
        identity.mode == VoiceIdentityApplyMode::kTemporary ? "temporary" : "persistent");
    cJSON_AddNumberToObject(report, "revision", identity.revision);
    if (identity.expires_at_ms > 0)
        cJSON_AddNumberToObject(report, "expiresAtMs", identity.expires_at_ms);
    cJSON_AddNumberToObject(report, "revisionWatermark", state.voice_identity_revision_watermark);
    cJSON_AddBoolToObject(report, "activeConfirmed", state.voice_identity_active_confirmed);
    cJSON_AddStringToObject(report, "status", state.voice_identity_status.c_str());
    cJSON_AddStringToObject(report, "runtime", state.runtime_name.c_str());
    cJSON_AddStringToObject(report, "model", "multinet5q8_cn");
    if (!state.voice_identity_error.empty())
        cJSON_AddStringToObject(report, "error", state.voice_identity_error.c_str());
    return report;
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
                                                const std::string& client_id,
                                                const ServerTrust& tls_trust) {
    esp_mqtt_client_config_t mqtt_config = {};
    mqtt_config.broker.address.uri = broker_uri.c_str();
    mqtt_config.credentials.client_id = client_id.c_str();
    mqtt_config.credentials.username = config.mqtt_username.c_str();
    mqtt_config.credentials.authentication.password = config.mqtt_password.c_str();
    mqtt_config.session.keepalive = config.mqtt_keepalive;
    mqtt_config.network.reconnect_timeout_ms = 2000;
    mqtt_config.network.timeout_ms = 10 * 1000;
    if (!tls_trust.empty()) {
        mqtt_config.broker.verification.certificate = tls_trust.ca_pem.c_str();
        mqtt_config.broker.verification.certificate_len = tls_trust.ca_pem.size() + 1;
        mqtt_config.broker.verification.common_name = tls_trust.tls_name.c_str();
    }
    // The default 6 KiB stack can fail alongside the local wake model. Plain
    // MQTT has stayed within 4 KiB while leaving headroom for bootstrap work.
    mqtt_config.task.stack_size = tls_trust.empty() ? 4096 : 6144;
    return mqtt_config;
}

bool HasSameMqttSessionIdentity(const DeviceCloudConfig& current,
                                const DeviceCloudConfig& refreshed,
                                bool require_same_route = true) {
    return current.server_trust.server_id == refreshed.server_trust.server_id &&
           (!require_same_route ||
            current.server_connect_address == refreshed.server_connect_address) &&
           current.server_trust.ca_pem == refreshed.server_trust.ca_pem &&
           current.server_trust.tls_name == refreshed.server_trust.tls_name &&
           current.mqtt_protocol_version == refreshed.mqtt_protocol_version &&
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
    cJSON* root = ParseCommandJson(payload);
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
                            const DeviceCloudConfig& next,
                            bool require_same_route = true) {
    // 数值路由例外只能由完整的 pinned authority 校验开启。
    return HasSameMqttSessionIdentity(current, next, require_same_route) &&
           current.provisioning_url == next.provisioning_url &&
           current.aiot_device_secret == next.aiot_device_secret &&
           current.aiot_registered == next.aiot_registered &&
           current.aiot_activated == next.aiot_activated &&
           current.unbind_pending == next.unbind_pending;
}

bool HasSamePinnedAuthority(const DeviceCloudConfig& current,
                            const DeviceCloudConfig& next) {
    const auto numeric_route = [](const std::string& address) {
        return address.empty() || NormalizeServerRouteAddress(address) == address;
    };
    return !current.server_trust.empty() && !current.server_trust.ca_pem.empty() &&
           !current.server_trust.tls_name.empty() &&
           current.server_trust.version == next.server_trust.version &&
           numeric_route(current.server_connect_address) &&
           numeric_route(next.server_connect_address) &&
           HasSameEffectAuthority(current, next, false);
}

bool HasUsableMqttConfig(const DeviceCloudConfig& config) {
    return config.has_mqtt_config && !config.unbind_pending && !config.server_trust_error &&
           !config.server_trust_pending &&
           (config.server_trust.empty() ||
            config.mqtt_broker_address == config.server_trust.tls_name);
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
        case ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED:
            return "disconnected";
        case ESP_PEER_STATE_CONNECT_FAILED:
            return "failed";
        case ESP_PEER_STATE_CLOSED:
        case ESP_PEER_STATE_DATA_CHANNEL_CLOSED:
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
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mutex_);
    if (client_lifecycle_failed_.load()) return false;
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return true;
    }
    message_queue_ = xQueueCreate(8, sizeof(PendingMessage*));
    worker_running_.store(true);
    if (message_queue_ == nullptr ||
        xTaskCreate(WorkerTask, "mqtt_worker", 8192, this, 4, &worker_) != pdPASS) {
        worker_running_.store(false);
        started_.store(false);
        if (message_queue_ != nullptr) {
            vQueueDelete(message_queue_);
            message_queue_ = nullptr;
        }
        ESP_LOGE(TAG, "Cannot reserve MQTT worker (stack=8192 internal_free=%u largest=%u)",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
        return false;
    }
    ESP_LOGI(TAG, "Reserved internal MQTT worker: stack=8192 queue=8");
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
        esp_netif_t* station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t info = {};
        if (station != nullptr && esp_netif_get_ip_info(station, &info) == ESP_OK &&
            info.ip.addr != 0) {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            // A registered GOT_IP callback may already own a newer baseline.
            if (!station_ip_valid_) {
                station_ip_ = info.ip.addr;
                station_netmask_ = info.netmask.addr;
                station_gateway_ = info.gw.addr;
                station_ip_valid_ = true;
            }
        }
        StartConnectionAsync();
    }
    return true;
}

void UnifiedMqttService::Stop() {
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mutex_);
    TimerHandle_t telemetry_timer = nullptr;
    std::unique_ptr<ClientInstance> client;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!started_.exchange(false)) return;
        telemetry_timer = telemetry_timer_;
        telemetry_timer_ = nullptr;
        client = DetachClientLocked();
    }
    // Revoke command and input admission before waiting for a Start/Stop that
    // may already own the resource operation lock.
    CleanupRevokedStreams();
    ota_update_.SetProgressPublisher({});
    if (telemetry_timer != nullptr) {
        DeleteTimerAndWait(telemetry_timer);
    }
    DestroyClient(std::move(client));
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
    last_voice_identity_report_.clear();
    connected_pending_ = false;
    pending_credential_config_.reset();
    client_replacement_retry_at_ms_ = 0;
    client_replacement_retry_ms_ = kConnectionRetryInitialMs;
    credential_restart_pending_ = false;
    credential_refresh_deferred_for_voice_ = false;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        transport_recovery_ = {};
        transport_refresh_scheduled_ = false;
        network_route_refresh_scheduled_ = false;
        station_ip_valid_ = false;
        station_ip_ = 0;
        station_netmask_ = 0;
        station_gateway_ = 0;
        ++network_route_generation_;
        refresh_route_generation_ = 0;
        auth_refresh_generation_ = 0;
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
    auto* service = static_cast<UnifiedMqttService*>(arg);
    if (service == nullptr || event_id != IP_EVENT_STA_GOT_IP) return;
    const auto* event = static_cast<const ip_event_got_ip_t*>(event_data);
    service->HandleNetworkAddress(event == nullptr ? 0 : event->ip_info.ip.addr,
                                  event == nullptr ? 0 : event->ip_info.netmask.addr,
                                  event == nullptr ? 0 : event->ip_info.gw.addr);
}

void UnifiedMqttService::HandleNetworkAddress(uint32_t address, uint32_t netmask,
                                              uint32_t gateway) {
    bool route_changed = false;
    bool start_connection = false;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!started_.load() || client_lifecycle_failed_.load() || address == 0) return;
        const bool same_route = station_ip_valid_ && station_ip_ == address &&
            station_netmask_ == netmask && station_gateway_ == gateway;
        route_changed = !same_route &&
            (station_ip_valid_ || client_ != nullptr || connecting_.load());
        if (!same_route) {
            station_ip_ = address;
            station_netmask_ = netmask;
            station_gateway_ = gateway;
            station_ip_valid_ = true;
            ++network_route_generation_;
        }
        if (route_changed) {
            network_route_refresh_scheduled_ = true;
            bool expected = false;
            reset_scheduled_.compare_exchange_strong(expected, true);
        } else if (client_ == nullptr) {
            start_connection = true;
        }
    }
    if (route_changed) {
        ESP_LOGI(TAG, "WiFi route changed; scheduling authenticated MQTT route refresh");
        return;
    }
    if (start_connection) StartConnectionAsync();
}

void UnifiedMqttService::StartConnectionAsync() {
    if (!started_.load() || client_lifecycle_failed_.load() || HasClient()) {
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
        if (client_lifecycle_failed_.load()) {
            vTaskDelay(pdMS_TO_TICKS(kBackgroundTaskPollMs));
            continue;
        }
        CleanupRevokedStreams();
        MaybeScheduleTransportRecovery();
        if (reset_scheduled_.load() && !ShouldDeferCredentialRefresh()) {
            RefreshCredentials();
        }
        if (connecting_.load() && started_.load() && pending_credential_config_ == nullptr &&
            !credential_restart_pending_ && !client_lifecycle_failed_.load()) {
            RunConnection();
        }
        uint32_t generation = 0;
        bool connected_work = false;
        ControlGateTiming control_timing;
        bool log_control_timing = false;
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            connected_work = connected_pending_;
            generation = connected_pending_generation_;
            connected_pending_ = false;
            log_control_timing = control_gate_timing_pending_;
            control_timing = control_gate_timing_;
            control_gate_timing_pending_ = false;
        }
        if (log_control_timing) {
            ESP_LOGW(TAG, "control gate slow: callback=%" PRIu64 " lease=%" PRIu64
                " started_us=%" PRId64 " acquired_us=%" PRId64
                " checked_us=%" PRId64 " current=%d",
                control_timing.callback_no, control_timing.instance_nonce,
                control_timing.started_us, control_timing.acquired_us,
                control_timing.checked_us, control_timing.current);
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
        PublishChangedVoiceIdentity();
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
    while (started_.load() && !client_lifecycle_failed_.load() && !HasClient()) {
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

std::unique_ptr<DeviceCloudConfig> UnifiedMqttService::LoadMqttSnapshot() {
    auto snapshot = std::unique_ptr<DeviceCloudConfig>(FailResource(ResourceFailure::kMqttConfig)
        ? nullptr : new (std::nothrow) DeviceCloudConfig);
    if (snapshot == nullptr) {
        ESP_LOGE(TAG, "Cannot allocate MQTT configuration snapshot");
        return nullptr;
    }
    // Load's bool means complete AIoT identity, not general storage success.
    // Always load into a fresh object so a partial read cannot reuse old fields.
    const bool has_aiot = config_service_.Load(*snapshot);
    if (!has_aiot && snapshot->server_requires_bound_identity) snapshot->has_mqtt_config = false;
    return snapshot;
}

void UnifiedMqttService::Connect() {
    uint64_t route_generation = 0;
    bool route_refresh = false;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        route_generation = network_route_generation_;
        route_refresh = network_route_refresh_scheduled_;
        if (route_refresh) refresh_route_generation_ = route_generation;
    }
    auto next = LoadMqttSnapshot();
    if (next == nullptr) return;
    DeviceCloudConfig& next_config = *next;
    if (force_refresh_.exchange(false) || route_refresh || !next_config.has_mqtt_config ||
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
    if (!HasUsableMqttConfig(next_config)) {
        const std::string config_error = config_service_.last_error();
        ESP_LOGW(TAG, "Unified MQTT credentials are unavailable: %s",
                 config_error.c_str());
        return;
    }

    if (StartClient(next_config, route_generation) && route_refresh) FinishCredentialRefresh();
}

bool UnifiedMqttService::StartClient(DeviceCloudConfig& next_config,
                                     uint64_t route_generation) {
    auto instance = std::unique_ptr<ClientInstance>(new (std::nothrow) ClientInstance);
    if (instance == nullptr) {
        ESP_LOGE(TAG, "Cannot allocate MQTT client lifetime");
        return false;
    }
    instance->service = this;
    instance->tls_trust = next_config.server_trust;
    const std::string logical_broker_uri = (next_config.server_trust.empty() ? "mqtt://" : "mqtts://") +
        next_config.mqtt_broker_address + ":" + std::to_string(next_config.mqtt_broker_port);
    instance->broker_uri = ServerTrustConnectUrl(next_config.server_trust, logical_broker_uri,
                                                next_config.server_connect_address);
    instance->client_id = BuildClientId(next_config.mqtt_device_key);
    if (instance->broker_uri.empty() || !HasUsableMqttConfig(next_config)) return false;

    std::unique_lock<std::mutex> api_lock(client_api_mutex_);
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!started_.load() || client_ != nullptr || client_lifecycle_failed_.load()) return false;
        if (++client_generation_ == 0) ++client_generation_;
        instance->generation = client_generation_;
    }
    const esp_mqtt_client_config_t mqtt_config = BuildMqttClientConfig(
        next_config, instance->broker_uri, instance->client_id, instance->tls_trust);
    instance->handle = esp_mqtt_client_init(&mqtt_config);
    if (instance->handle == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return false;
    }
    const esp_err_t register_err = esp_mqtt_client_register_event(
        instance->handle, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
        MqttEventHandler, instance.get());
    if (register_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s",
                 esp_err_to_name(register_err));
        esp_mqtt_client_destroy(instance->handle);
        return false;
    }

    ClientInstance* const attached_instance = instance.get();
    // API -> cloud configuration -> MQTT state. Only attachment/authority is
    // admitted here; no SDK operation or callback join holds the cloud fence.
    const bool attached = config_service_.ApplyIfMqttConfigCurrent(next_config, [&]() {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!started_.load() || client_ != nullptr || client_lifecycle_failed_.load() ||
            client_generation_ != instance->generation ||
            route_generation != network_route_generation_) return false;
        if (!HasSameEffectAuthority(config_, next_config) &&
            !(network_route_refresh_scheduled_ && HasSamePinnedAuthority(config_, next_config))) {
            ResetEffectAuthorityLocked();
        }
        config_ = next_config;
        effect_authority_active_ = true;
        client_ = instance->handle;
        publication_event_pending_ = false;
        AdvanceConnectionEpochLocked();
        client_instance_ = std::move(instance);
        return true;
    });
    if (!attached) {
        esp_mqtt_client_destroy(instance->handle);
        ESP_LOGW(TAG, "MQTT candidate cancelled before attachment");
        return false;
    }

    ESP_LOGI(TAG, "MQTT generation %u attached with current credentials",
             static_cast<unsigned>(attached_instance->generation));
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (started_.load()) {
        err = esp_mqtt_client_start(attached_instance->handle);
        attached_instance->sdk_started = err == ESP_OK;
    }
    std::unique_ptr<ClientInstance> failed_start;
    bool current = false;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        current = started_.load() && client_instance_.get() == attached_instance;
        if (err != ESP_OK && client_instance_.get() == attached_instance) {
            failed_start = DetachClientLocked();
        }
    }
    api_lock.unlock();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        DestroyClient(std::move(failed_start));
        return false;
    }
    return current;
}

std::unique_ptr<UnifiedMqttService::ClientInstance> UnifiedMqttService::DetachClientLocked() {
    const uint32_t retired_generation = client_generation_;
    connected_.store(false);
    effect_authority_active_ = false;
    client_ = nullptr;
    if (++client_generation_ == 0) ++client_generation_;
    AdvanceConnectionEpochLocked();
    connected_pending_ = false;
    publication_event_pending_ = false;
    recent_published_events_ = {};
    if (auth_refresh_generation_ == retired_generation) {
        auth_refresh_generation_ = 0;
        ESP_LOGI(TAG, "MQTT auth request from retired generation %u coalesced",
                 static_cast<unsigned>(retired_generation));
    }
    reliable_publish_ = {};
    if (publish_ack_semaphore_ != nullptr) xSemaphoreGive(publish_ack_semaphore_);
    return std::move(client_instance_);
}

bool UnifiedMqttService::DestroyClient(std::unique_ptr<ClientInstance> client) {
    if (client == nullptr) return !client_lifecycle_failed_.load();
    std::unique_lock<std::mutex> api_lock(client_api_mutex_);
    const uint32_t generation = client->generation;
    const esp_err_t stop_err = client->sdk_started ? esp_mqtt_client_stop(client->handle) : ESP_OK;
    if (stop_err != ESP_OK) {
        // ESP_FAIL also covers task startup/exit races. Neither run=false nor
        // SDK destroy proves STOPPED; retain every borrowed pointer until reboot.
        failed_client_ = std::move(client);
        client_lifecycle_failed_.store(true);
        api_lock.unlock();
        ESP_LOGE(TAG, "MQTT generation %u stop unconfirmed (%s); isolating by restart",
                 static_cast<unsigned>(generation), esp_err_to_name(stop_err));
        esp_restart();
        return false;
    }
    const esp_err_t destroy_err = esp_mqtt_client_destroy(client->handle);
    if (destroy_err != ESP_OK) {
        failed_client_ = std::move(client);
        client_lifecycle_failed_.store(true);
        api_lock.unlock();
        ESP_LOGE(TAG, "MQTT generation %u destroy failed (%s); isolating by restart",
                 static_cast<unsigned>(generation), esp_err_to_name(destroy_err));
        esp_restart();
        return false;
    }
    ESP_LOGI(TAG, "MQTT generation %u stop/destroy confirmed",
             static_cast<unsigned>(generation));
    return true;
}

bool UnifiedMqttService::RetireClientForRefresh() {
    std::unique_ptr<ClientInstance> retired;
    {
        std::lock_guard<std::mutex> api_lock(client_api_mutex_);
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!started_.load() || client_lifecycle_failed_.load()) return false;
        if (client_ == nullptr) return true;
        retired = DetachClientLocked();
    }
    ESP_LOGI(TAG, "MQTT generation %u retired for credential refresh",
             static_cast<unsigned>(retired->generation));
    // External Stop also joins stream peers before taking client_api_mutex_.
    // Holding that lock here would deadlock its teardown against a peer callback.
    CleanupRevokedStreams();
    return DestroyClient(std::move(retired));
}

void UnifiedMqttService::ScheduleCredentialRefresh(uint32_t generation) {
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    if (!started_.load() || client_ == nullptr || client_generation_ != generation ||
        client_lifecycle_failed_.load()) {
        return;
    }
    // Authentication rejection takes precedence over a pending TCP recovery.
    transport_refresh_scheduled_ = false;
    auth_refresh_generation_ = generation;
    ESP_LOGI(TAG, "MQTT auth refresh requested by generation %u",
             static_cast<unsigned>(generation));
    bool expected = false;
    if (!reset_scheduled_.compare_exchange_strong(expected, true)) {
        return;
    }
    // Coalesced control work cannot be starved by a full message queue.
}

void UnifiedMqttService::FinishCredentialRefresh() {
    pending_credential_config_.reset();
    client_replacement_retry_at_ms_ = 0;
    client_replacement_retry_ms_ = kConnectionRetryInitialMs;
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    transport_refresh_scheduled_ = false;
    if (!started_.load()) {
        network_route_refresh_scheduled_ = false;
        refresh_route_generation_ = 0;
        reset_scheduled_.store(false);
        return;
    }
    const bool route_pending = network_route_refresh_scheduled_ &&
        refresh_route_generation_ != network_route_generation_;
    if (!route_pending) {
        network_route_refresh_scheduled_ = false;
        refresh_route_generation_ = 0;
    }
    // Only a rejection from the newly attached instance survives replacement.
    const bool pending = client_ != nullptr && auth_refresh_generation_ == client_generation_;
    reset_scheduled_.store(route_pending || pending);
    if (pending) {
        ESP_LOGI(TAG, "MQTT auth refresh retained for current generation %u",
                 static_cast<unsigned>(client_generation_));
    }
    if (route_pending) {
        ESP_LOGI(TAG, "MQTT route refresh retained for newer network generation");
    }
}

void UnifiedMqttService::RetryClientReplacement() {
    client_replacement_retry_at_ms_ = esp_timer_get_time() / 1000 + client_replacement_retry_ms_;
    ESP_LOGW(TAG, "MQTT replacement deferred; retrying latest configuration in %d ms",
             client_replacement_retry_ms_);
    client_replacement_retry_ms_ = std::min(client_replacement_retry_ms_ * 2,
                                           kConnectionRetryMaxMs);
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
    if (client_lifecycle_failed_.load()) return;
    if (credential_restart_pending_) {
        if (started_.load() && !ShouldDeferCredentialRefresh()) {
            ESP_LOGW(TAG, "Restarting to isolate refreshed MQTT session");
            esp_restart();
        }
        return;
    }
    bool route_refresh = false;
    uint64_t route_generation = 0;
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
            route_refresh = network_route_refresh_scheduled_;
            route_generation = network_route_generation_;
            if (route_refresh) refresh_route_generation_ = route_generation;
            if (transport_refresh_scheduled_ && !network_route_refresh_scheduled_ &&
                connected_.load()) {
                reset_scheduled_.store(false);
                transport_refresh_scheduled_ = false;
                ESP_LOGI(TAG, "MQTT reconnected before TCP recovery; refresh cancelled");
                return;
            }
            transport_recovery_.MarkRefreshStarted(esp_timer_get_time() / 1000);
            auth_refresh_generation_ = 0;
        }
        auto refreshed = LoadMqttSnapshot();
        if (refreshed == nullptr) {
            ESP_LOGE(TAG, "Cannot allocate MQTT credential refresh snapshot");
            FinishCredentialRefresh();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            if (refreshed->unbind_pending || !refreshed->has_mqtt_config ||
                (!HasSameEffectAuthority(config_, *refreshed) &&
                 !(network_route_refresh_scheduled_ && HasSamePinnedAuthority(config_, *refreshed)))) {
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
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            if (route_refresh && route_generation != network_route_generation_) {
                ESP_LOGI(TAG, "MQTT route changed again during bootstrap refresh; retrying latest route");
                pending_credential_config_.reset();
                return;
            }
        }
        // HTTP rotates credentials. Retain this result if voice became active
        // so the worker can keep processing events without repeating the request.
        pending_credential_config_ = std::move(refreshed);
    }
    if (!started_.load() || ShouldDeferCredentialRefresh() ||
        esp_timer_get_time() / 1000 < client_replacement_retry_at_ms_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (network_route_refresh_scheduled_ &&
            refresh_route_generation_ != network_route_generation_) {
            pending_credential_config_.reset();
            return;
        }
        route_generation = network_route_generation_;
    }
    // Voice may have rotated credentials while HTTP, teardown or a retry was
    // deferred. SDK creation failure must not rotate them by repeating HTTP.
    auto latest = LoadMqttSnapshot();
    if (latest == nullptr) {
        RetryClientReplacement();
        return;
    }
    pending_credential_config_ = std::move(latest);
    if (!HasUsableMqttConfig(*pending_credential_config_)) {
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            effect_authority_active_ = false;
            AdvanceConnectionEpochLocked();
            ResetEffectAuthorityLocked();
        }
        ESP_LOGW(TAG, "MQTT recovery cancelled: persisted configuration is unavailable");
        RetireClientForRefresh();
        connecting_.store(false);
        FinishCredentialRefresh();
        return;
    }
    MqttCredentialRefreshState state;
    state.refresh_succeeded = true;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        // config_ retains the prior authority while its replacement is offline.
        state.has_client = client_ != nullptr || config_.has_mqtt_config;
        state.same_effect_authority = HasSameEffectAuthority(config_, *pending_credential_config_) ||
            (network_route_refresh_scheduled_ &&
             HasSamePinnedAuthority(config_, *pending_credential_config_));
    }
    const auto action = DecideMqttCredentialRefreshAction(state);
    if (action == MqttCredentialRefreshAction::kRestart && started_.load()) {
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            effect_authority_active_ = false;
            AdvanceConnectionEpochLocked();
            ResetEffectAuthorityLocked();
        }
        credential_restart_pending_ = true;
        if (!ShouldDeferCredentialRefresh()) {
            ESP_LOGW(TAG, "MQTT authority changed; restarting to isolate refreshed session");
            esp_restart();
        }
        return;
    }
    if (!RetireClientForRefresh() || !started_.load()) return;
    if (ShouldDeferCredentialRefresh()) return;

    // Teardown can wait for callbacks. Reload after it, then use the cloud
    // service's exact credential fence for the final attachment linearization.
    latest = LoadMqttSnapshot();
    if (latest == nullptr) {
        RetryClientReplacement();
        return;
    }
    pending_credential_config_ = std::move(latest);
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!HasUsableMqttConfig(*pending_credential_config_) ||
            (config_.has_mqtt_config &&
             !HasSameEffectAuthority(config_, *pending_credential_config_) &&
             !(network_route_refresh_scheduled_ &&
               HasSamePinnedAuthority(config_, *pending_credential_config_)))) {
            RetryClientReplacement();
            return;
        }
    }
    if (!StartClient(*pending_credential_config_, route_generation)) {
        if (started_.load() && !client_lifecycle_failed_.load()) RetryClientReplacement();
        return;
    }
    connecting_.store(false);
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
    camera_closed_lease_.reset();
    display_closed_lease_.reset();
    if (camera_lease_ != nullptr) camera_lease_->Revoke();
    if (display_lease_ != nullptr) display_lease_->Revoke();
    stream_cleanup_pending_ = camera_lease_ != nullptr || display_lease_ != nullptr;
}

void UnifiedMqttService::ResetEffectAuthorityLocked() {
    volume_effect_.ResetAuthority();
    light_effect_.ResetAuthority();
    command_ledger_.ResetAuthority();
    camera_closed_lease_.reset();
    display_closed_lease_.reset();
}

bool UnifiedMqttService::IsCommandContextCurrentLocked(
    const CommandPublishContext& context) const {
    return started_.load() && connected_.load() && client_ != nullptr &&
           client_generation_ == context.client_generation &&
           connection_epoch_ == context.connection_epoch;
}

bool UnifiedMqttService::IsStreamPublicationCurrentLocked(
    const StreamLeasePtr& lease, bool terminal, bool display) const {
    if (lease == nullptr) return true;
    if (!started_.load() || !connected_.load() || !effect_authority_active_ ||
        client_ == nullptr || lease->client_generation != client_generation_ ||
        lease->connection_epoch != connection_epoch_) return false;
    if (terminal) {
        return lease->instance_nonce == (display ? display_latest_nonce_ : camera_latest_nonce_);
    }
    return lease->IsActive() && lease == (display ? display_lease_ : camera_lease_);
}

void UnifiedMqttService::RevokeStreamLease(const StreamLeasePtr& lease) {
    if (lease == nullptr) return;
    lease->Revoke();
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    if (lease == camera_lease_ || lease == display_lease_) stream_cleanup_pending_ = true;
}

StreamLeasePtr UnifiedMqttService::FindClosedStreamLocked(
    bool display, const std::string& session_id, const std::string& start_command_no) const {
    const auto& closed = display ? display_closed_lease_ : camera_closed_lease_;
    if (!started_.load() || !connected_.load() || !effect_authority_active_ || client_ == nullptr ||
        closed == nullptr || (display ? display_lease_ : camera_lease_) != nullptr ||
        !closed->WasStartAccepted() || closed->IsActive() || start_command_no.empty() ||
        closed->session_id != session_id || closed->start_command_no != start_command_no ||
        closed->client_generation != client_generation_ || closed->connection_epoch != connection_epoch_ ||
        closed->instance_nonce != (display ? display_latest_nonce_ : camera_latest_nonce_)) return {};
    return closed;
}

void UnifiedMqttService::CleanupRevokedStreams() {
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!stream_cleanup_pending_) return;
    }
    std::lock_guard<std::mutex> operation_lock(stream_operation_mutex_);
    CleanupRevokedStreamsLocked();
}

void UnifiedMqttService::CleanupRevokedStreamsLocked() {
    // The operation lock keeps an old instance's Stop from reaching a newly
    // started peer. Never hold mqtt_mutex_ while Stop joins peer callbacks.
    for (const bool display : {false, true}) {
        StreamLeasePtr lease;
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            const auto& current = display ? display_lease_ : camera_lease_;
            if (current != nullptr && !current->IsActive()) lease = current;
        }
        if (lease == nullptr) continue;
        bool stop_completed = false;
        if (display) {
            if (web_rtc_display_service_ != nullptr) {
                web_rtc_display_service_->Stop();
                stop_completed = true;
            }
        } else if (web_rtc_camera_service_ != nullptr) {
            web_rtc_camera_service_->Stop();
            stop_completed = true;
        }
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        auto& current = display ? display_lease_ : camera_lease_;
        if (current == lease) {
            // Only the completed native join is evidence; revocation/terminal
            // callbacks alone cannot make a delayed Stop successful.
            if (stop_completed && lease->WasStartAccepted() && started_.load() && connected_.load() &&
                effect_authority_active_ && client_ != nullptr && lease->client_generation == client_generation_ &&
                lease->connection_epoch == connection_epoch_ &&
                lease->instance_nonce == (display ? display_latest_nonce_ : camera_latest_nonce_)) {
                (display ? display_closed_lease_ : camera_closed_lease_) = lease;
            }
            current.reset();
        }
    }
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    stream_cleanup_pending_ = (camera_lease_ != nullptr && !camera_lease_->IsActive()) ||
                              (display_lease_ != nullptr && !display_lease_->IsActive());
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
                                                const std::string& payload,
                                                const StreamLeasePtr& stream_lease,
                                                bool terminal_stream_event,
                                                bool display_stream) {
    if (context.ack_topic.empty() || context.ack_topic.size() > kMaxCommandPublicationTopicBytes ||
        payload.size() > kMaxCommandPublicationPayloadBytes) {
        ESP_LOGW(TAG, "Dropping command output: topic or payload exceeds the publication limit");
        return false;
    }
    // Only post a zero-timeout SDK event here. Taking its API lock while holding
    // mqtt_mutex_ would invert the SDK callback's existing lock order.
    std::lock_guard<std::mutex> api_lock(client_api_mutex_);
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    if (!IsCommandContextCurrentLocked(context) ||
        !IsStreamPublicationCurrentLocked(stream_lease, terminal_stream_event, display_stream)) {
        ESP_LOGW(TAG, "Dropping command output: original MQTT connection is no longer current");
        return false;
    }
    const bool count_limit_reached = command_publications_.size() >= kMaxCommandPublications;
    if (count_limit_reached ||
        payload.size() > kMaxCommandPublicationBytes - command_publication_bytes_) {
        ESP_LOGW(TAG, "Dropping command output: publication queue is full reason=%s "
                     "publication_count=%u publication_bytes=%u request_bytes=%u",
                 count_limit_reached ? "count_limit" : "byte_limit",
                 static_cast<unsigned>(command_publications_.size()),
                 static_cast<unsigned>(command_publication_bytes_),
                 static_cast<unsigned>(payload.size()));
        return false;
    }
    command_publications_.push_back(
        {context, payload, stream_lease, terminal_stream_event, display_stream});
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
                publication.context.connection_epoch != connection_epoch_ ||
                !IsStreamPublicationCurrentLocked(publication.stream_lease,
                    publication.terminal_stream_event, publication.display_stream)) continue;
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
    auto* instance = static_cast<ClientInstance*>(arg);
    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
    if (instance != nullptr && instance->service != nullptr && event != nullptr &&
        event->client == instance->handle) {
        instance->service->HandleMqttEvent(event, instance->generation);
    }
}

void UnifiedMqttService::HandleMqttEvent(esp_mqtt_event_handle_t event, uint32_t generation) {
    constexpr size_t kMaxMqttPayloadBytes = 256 * 1024;
    bool wake_publisher = false;
    bool schedule_message = false;
    bool drain_command_publications = false;
    bool report_sdk_stack = false;
    uint32_t sdk_stack_free = 0;
    const esp_mqtt_client_handle_t event_client = event->client;
    uint32_t event_generation = 0;
    uint64_t event_epoch = 0;
    std::string completed_topic;
    std::string completed_payload;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!started_.load() || event->client != client_ || client_generation_ != generation ||
            client_lifecycle_failed_.load()) {
            return;
        }
        event_generation = generation;
        sdk_stack_free = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
        if (sdk_stack_free < mqtt_sdk_stack_min_free_) {
            mqtt_sdk_stack_min_free_ = sdk_stack_free;
            report_sdk_stack = true;
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
    if (report_sdk_stack) {
        ESP_LOGI(TAG, "MQTT SDK stack minimum free=%u bytes", static_cast<unsigned>(sdk_stack_free));
    }
    if (wake_publisher && publish_ack_semaphore_ != nullptr) {
        xSemaphoreGive(publish_ack_semaphore_);
    }
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Unified MQTT connected: generation=%u",
                     static_cast<unsigned>(event_generation));
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
                const size_t topic_bytes = completed_topic.size();
                const size_t payload_bytes = completed_payload.size();
                auto context = std::unique_ptr<PendingMessage>(
                    new (std::nothrow) PendingMessage{
                        event_generation,
                        event_epoch,
                        std::move(completed_topic),
                        std::move(completed_payload),
                    });
                PendingMessage* pending = context.get();
                const char* drop_reason = nullptr;
                if (pending == nullptr) {
                    drop_reason = "object_alloc_failed";
                } else if (xQueueSend(message_queue_, &pending, 0) != pdTRUE) {
                    drop_reason = "queue_send_rejected";
                }
                if (drop_reason != nullptr) {
                    // The worker and allocator can progress between these failure-adjacent samples.
                    const int64_t at_us_sample = esp_timer_get_time();
                    const UBaseType_t queue_depth_sample = uxQueueMessagesWaiting(message_queue_);
                    const size_t default_free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
                    const size_t default_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
                    ESP_LOGE(TAG, "MQTT message dropped: reason=%s topic_bytes=%u payload_bytes=%u "
                                 "object_bytes=%u queue_depth_sample=%u at_us_sample=%" PRId64
                                 " default_free=%u default_largest=%u",
                             drop_reason, static_cast<unsigned>(topic_bytes),
                             static_cast<unsigned>(payload_bytes),
                             static_cast<unsigned>(sizeof(PendingMessage)),
                             static_cast<unsigned>(queue_depth_sample), at_us_sample,
                             static_cast<unsigned>(default_free),
                             static_cast<unsigned>(default_largest));
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
                ScheduleCredentialRefresh(event_generation);
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
    if (voice_identity != nullptr && voice_wake_ != nullptr) {
        VoiceIdentityConfig config;
        std::string error;
        if (!ParseVoiceIdentityDesired(voice_identity, config, error)) {
            voice_wake_->RejectVoiceIdentity(error);
        } else {
            voice_identity_changed = voice_wake_->ApplyVoiceIdentity(config, error);
        }
        if (!voice_identity_changed) {
            ESP_LOGW(TAG, "Rejected desired voice identity: %s", error.c_str());
        }
    }
    if (cJSON_IsNumber(volume) || cJSON_IsObject(light) || voice_identity != nullptr || cJSON_IsObject(appearance)) {
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
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (camera_lease_ != nullptr) {
            camera_lease_->Revoke();
            stream_cleanup_pending_ = true;
        }
    }
    std::lock_guard<std::mutex> operation_lock(stream_operation_mutex_);
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (camera_lease_ != nullptr) camera_lease_->Revoke();
    }
    CleanupRevokedStreamsLocked();
}

void UnifiedMqttService::StopWebRtcDisplayStream() {
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (display_lease_ != nullptr) {
            display_lease_->Revoke();
            stream_cleanup_pending_ = true;
        }
    }
    std::lock_guard<std::mutex> operation_lock(stream_operation_mutex_);
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (display_lease_ != nullptr) display_lease_->Revoke();
    }
    CleanupRevokedStreamsLocked();
}

void UnifiedMqttService::HandleCommand(const std::string& command_no,
                                       const std::string& payload,
                                       const CommandPublishContext& context) {
    cJSON* request = ParseCommandJson(payload);
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

    const bool camera_command = command == "camera.stream.start" ||
        command == "camera.stream.stop" || command == "camera.stream.signal";
    const bool display_command = command == "display.stream.start" ||
        command == "display.stream.stop" || command == "display.stream.signal";
    const bool stream_stop = command == "camera.stream.stop" || command == "display.stream.stop";
    const cJSON* start_command_json = cJSON_GetObjectItemCaseSensitive(request, "startCommandNo");
    const bool exact_stop = stream_stop && start_command_json != nullptr;
    const std::string stop_start_command = cJSON_IsString(start_command_json) && start_command_json->valuestring != nullptr
        ? start_command_json->valuestring : std::string();
    const cJSON* session_json = cJSON_GetObjectItemCaseSensitive(request, "sessionId");
    const std::string session_id = cJSON_IsString(session_json) && session_json->valuestring != nullptr
        ? session_json->valuestring : std::string();
    std::unique_lock<std::mutex> operation_lock(stream_operation_mutex_, std::defer_lock);
    if (camera_command || display_command) {
        operation_lock.lock();
        CleanupRevokedStreamsLocked();
    }
    MqttCommandLedger::Admission admission;
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        if (!IsCommandContextCurrentLocked(context) || !effect_authority_active_) {
            cJSON_Delete(request);
            return;
        }
        admission = command_ledger_.Begin(command_no, payload);
    }
    if (admission.disposition != MqttCommandLedger::Disposition::kExecute) {
        if (exact_stop && admission.disposition == MqttCommandLedger::Disposition::kReplay) {
            cJSON* cached = cJSON_Parse(admission.acknowledgement.c_str());
            const cJSON* status = cJSON_GetObjectItemCaseSensitive(cached, "status");
            if (cJSON_IsString(status) && std::strcmp(status->valuestring, "ok") == 0) {
                std::lock_guard<std::mutex> lock(mqtt_mutex_);
                const auto closed = FindClosedStreamLocked(display_command, session_id, stop_start_command);
                const auto& scope = admission.completion_scope;
                if (!IsCommandContextCurrentLocked(context) || closed == nullptr ||
                    scope.client_generation != context.client_generation ||
                    scope.connection_epoch != context.connection_epoch ||
                    scope.stream_instance_nonce != closed->instance_nonce) {
                    // Keep the frozen ledger result; an old success cannot be
                    // reissued as current-scope completion of another instance.
                    admission.acknowledgement = std::string("{\"status\":\"error\",\"errorCode\":\"") +
                        (display_command ? "display" : "camera") + "_stream_not_found\"}";
                }
            }
            cJSON_Delete(cached);
        }
        cJSON_Delete(request);
        if (operation_lock.owns_lock()) operation_lock.unlock();
        if (admission.disposition != MqttCommandLedger::Disposition::kPending) {
            QueueCommandPublication(context, admission.acknowledgement);
        }
        return;
    }

    bool handled = false;
    bool success = false;
    MqttCommandLedger::CompletionScope completion_scope;
    std::string error_code;
    cJSON* result = cJSON_CreateObject();
    if (camera_command || display_command) {
        handled = true;
        const bool display = display_command;
        const std::string prefix = display ? "display" : "camera";
        bool admitted = false;
        {
            std::lock_guard<std::mutex> lock(mqtt_mutex_);
            admitted = IsCommandContextCurrentLocked(context) && effect_authority_active_;
        }
        if (!admitted) {
            error_code = "command_scope_expired";
        } else if (display ? web_rtc_display_service_ == nullptr : web_rtc_camera_service_ == nullptr) {
            error_code = prefix + "_stream_unavailable";
        } else if (!cJSON_IsObject(request)) {
            error_code = "invalid_payload";
        } else if (!HasUniqueJsonKeys(request) || HasCommandJsonNul(payload)) {
            error_code = "invalid_payload";
        } else if (session_id.empty()) {
            error_code = "missing_session_id";
        } else if (exact_stop && (stop_start_command.empty() ||
                   stop_start_command.size() > MqttCommandLedger::kMaxCommandNoBytes)) {
            error_code = "invalid_start_command_no";
        } else if (command == prefix + ".stream.start") {
            StreamLeasePtr lease;
            {
                std::lock_guard<std::mutex> lock(mqtt_mutex_);
                if (!IsCommandContextCurrentLocked(context) || !effect_authority_active_) {
                    error_code = "command_scope_expired";
                } else if (camera_lease_ != nullptr || display_lease_ != nullptr) {
                    error_code = prefix + "_stream_busy";
                } else if (next_stream_instance_nonce_ == std::numeric_limits<uint64_t>::max()) {
                    error_code = "stream_instance_capacity_exceeded";
                } else {
                    ++next_stream_instance_nonce_;
                    lease = std::make_shared<StreamLease>(context.client_generation,
                        context.connection_epoch, next_stream_instance_nonce_, session_id, command_no);
                    (display ? display_lease_ : camera_lease_) = lease;
                    (display ? display_latest_nonce_ : camera_latest_nonce_) = lease->instance_nonce;
                    (display ? display_closed_lease_ : camera_closed_lease_).reset();
                }
            }
            if (lease != nullptr) {
                auto publish_signal = [this, context, lease, display](const char* event,
                    const char* type, const uint8_t* data, size_t size, bool terminal = false) {
                    cJSON* signal = cJSON_CreateObject();
                    cJSON_AddStringToObject(signal, "status", "ok");
                    cJSON* result = cJSON_CreateObject();
                    cJSON* stream = cJSON_CreateObject();
                    cJSON_AddStringToObject(stream, "sessionId", lease->session_id.c_str());
                    cJSON_AddStringToObject(stream, "event", event);
                    if (type != nullptr) cJSON_AddStringToObject(stream, "type", type);
                    if (data != nullptr && size > 0) {
                        const std::string encoded = EncodeBase64(data, size);
                        if (!encoded.empty()) cJSON_AddStringToObject(stream, "data", encoded.c_str());
                    }
                    cJSON_AddItemToObject(result, display ? "displayStream" : "cameraStream", stream);
                    cJSON_AddItemToObject(signal, "result", result);
                    const std::string encoded = EncodeJson(signal);
                    cJSON_Delete(signal);
                    QueueCommandPublication(context, encoded, lease, terminal, display);
                };
                auto on_signaling = [publish_signal](esp_peer_msg_type_t type,
                                                     std::vector<uint8_t>&& data) {
                    while (!data.empty() && data.back() == 0) data.pop_back();
                    publish_signal("signal", PeerMessageTypeName(type), data.data(), data.size());
                };
                auto on_state = [this, publish_signal, lease](esp_peer_state_t state) {
                    const bool terminal = state == ESP_PEER_STATE_CLOSED ||
                        state == ESP_PEER_STATE_CONNECT_FAILED || state == ESP_PEER_STATE_DISCONNECTED ||
                        state == ESP_PEER_STATE_DATA_CHANNEL_CLOSED ||
                        state == ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED;
                    if (terminal) RevokeStreamLease(lease);
                    publish_signal("state", PeerStateName(state), nullptr, 0, terminal);
                };
                const auto configure = [request](auto& config, int max_chunk_size) {
                    const cJSON* width = cJSON_GetObjectItemCaseSensitive(request, "width");
                    const cJSON* height = cJSON_GetObjectItemCaseSensitive(request, "height");
                    const cJSON* fps = cJSON_GetObjectItemCaseSensitive(request, "fps");
                    const cJSON* chunk_size = cJSON_GetObjectItemCaseSensitive(request, "chunkSize");
                    if (cJSON_IsNumber(width)) config.width = std::clamp(width->valueint, 160, 1280);
                    if (cJSON_IsNumber(height)) config.height = std::clamp(height->valueint, 120, 960);
                    if (cJSON_IsNumber(fps)) config.fps =
                        static_cast<uint8_t>(std::clamp(fps->valueint, 1, 15));
                    if (cJSON_IsNumber(chunk_size)) config.chunk_size =
                        static_cast<uint16_t>(std::clamp(chunk_size->valueint, 1024, max_chunk_size));
                };
                bool peer_started = false;
                if (display) {
                    WebRtcDisplayService::Config config;
                    configure(config, 20000);
                    config.stream_lease = lease;
                    auto on_control = [this, lease](const std::string& data, DisplayControlReply reply) {
                        static std::atomic<uint64_t> next_callback{0};
                        const uint64_t callback_no = next_callback.fetch_add(1, std::memory_order_relaxed) + 1;
                        const int64_t started_us = esp_timer_get_time();
                        bool current = false;
                        {
                            std::lock_guard<std::mutex> lock(mqtt_mutex_);
                            const int64_t acquired_us = esp_timer_get_time();
                            current = IsStreamPublicationCurrentLocked(lease, false, true);
                            const int64_t checked_us = esp_timer_get_time();
                            if (!data.empty() && checked_us - started_us >= 100000 &&
                                !control_gate_timing_pending_ && checked_us >= control_gate_next_log_us_) {
                                // Peer callbacks retain the SDK API lock. Only snapshot here;
                                // the existing MQTT worker emits the bounded sample outside locks.
                                control_gate_timing_ = {
                                    callback_no, lease->instance_nonce, started_us, acquired_us,
                                    checked_us, current};
                                control_gate_timing_pending_ = true;
                                control_gate_next_log_us_ = checked_us + 5000000;
                            }
                        }
                        // Teardown belongs to its captured owner even after revocation.
                        // The UI compares that lease before clearing control state.
                        if (display_control_callback_ && (data.empty() || current)) {
                            display_control_callback_(lease, data, std::move(reply));
                        } else if (reply) {
                            reply(false, "stream_lease_expired");
                        }
                    };
                    peer_started = web_rtc_display_service_->Start(config, std::move(on_signaling),
                        std::move(on_state), std::move(on_control));
                } else {
                    WebRtcCameraService::Config config;
                    configure(config, 10000);
                    peer_started = web_rtc_camera_service_->Start(
                        config, std::move(on_signaling), std::move(on_state));
                }
                {
                    std::lock_guard<std::mutex> lock(mqtt_mutex_);
                    success = peer_started && IsStreamPublicationCurrentLocked(lease, false, display);
                    if (success) {
                        lease->MarkStartAccepted();
                    } else {
                        lease->Revoke();
                        stream_cleanup_pending_ = true;
                        error_code = peer_started ? "command_scope_expired" : prefix + "_stream_start_failed";
                    }
                }
                if (success) {
                    cJSON_AddStringToObject(result, "sessionId", session_id.c_str());
                    cJSON_AddStringToObject(result, "startCommandNo", lease->start_command_no.c_str());
                    cJSON_AddStringToObject(result, "transport", "webrtc-datachannel");
                }
            }
        } else if (command == prefix + ".stream.stop") {
            StreamLeasePtr lease;
            StreamLeasePtr closed;
            {
                std::lock_guard<std::mutex> lock(mqtt_mutex_);
                const auto& current = display ? display_lease_ : camera_lease_;
                if (!IsCommandContextCurrentLocked(context) || !effect_authority_active_) {
                    error_code = "command_scope_expired";
                } else if (current != nullptr && current->session_id == session_id &&
                           (!exact_stop || current->start_command_no == stop_start_command) &&
                           IsStreamPublicationCurrentLocked(current, false, display)) {
                    lease = current;
                    lease->Revoke();
                    stream_cleanup_pending_ = true;
                } else if (exact_stop && (closed = FindClosedStreamLocked(display, session_id, stop_start_command))) {
                    success = true;
                } else {
                    error_code = prefix + "_stream_not_found";
                }
            }
            if (lease != nullptr) {
                CleanupRevokedStreamsLocked();
                std::lock_guard<std::mutex> lock(mqtt_mutex_);
                closed = FindClosedStreamLocked(display, session_id, lease->start_command_no);
                success = IsCommandContextCurrentLocked(context) && closed == lease;
                if (!success) error_code = "command_scope_expired";
            }
            if (success) {
                cJSON_AddStringToObject(result, "sessionId", session_id.c_str());
                cJSON_AddStringToObject(result, "startCommandNo", closed->start_command_no.c_str());
                cJSON_AddStringToObject(result, "stopOutcome", lease != nullptr ? "stopped" : "already_stopped");
                if (exact_stop) completion_scope = {
                    context.client_generation, context.connection_epoch, closed->instance_nonce};
            }
        } else {
            const cJSON* type_json = cJSON_GetObjectItemCaseSensitive(request, "type");
            const cJSON* data_json = cJSON_GetObjectItemCaseSensitive(request, "data");
            esp_peer_msg_type_t message_type = ESP_PEER_MSG_TYPE_NONE;
            if (cJSON_IsString(type_json) && type_json->valuestring != nullptr) {
                if (std::strcmp(type_json->valuestring, "sdp") == 0) message_type = ESP_PEER_MSG_TYPE_SDP;
                else if (std::strcmp(type_json->valuestring, "candidate") == 0) {
                    message_type = ESP_PEER_MSG_TYPE_CANDIDATE;
                }
            }
            if (!cJSON_IsString(data_json) || data_json->valuestring == nullptr ||
                message_type == ESP_PEER_MSG_TYPE_NONE) {
                error_code = "invalid_signal";
            } else {
                std::vector<uint8_t> decoded = DecodeBase64(data_json->valuestring);
                if (decoded.empty()) {
                    error_code = "invalid_signal_encoding";
                } else {
                    StreamLeasePtr lease;
                    {
                        std::lock_guard<std::mutex> lock(mqtt_mutex_);
                        const auto& current = display ? display_lease_ : camera_lease_;
                        if (!IsCommandContextCurrentLocked(context) || !effect_authority_active_) {
                            error_code = "command_scope_expired";
                        } else if (current != nullptr && current->session_id == session_id &&
                                   IsStreamPublicationCurrentLocked(current, false, display)) {
                            lease = current;
                        } else {
                            error_code = prefix + "_stream_not_found";
                        }
                    }
                    if (lease != nullptr) {
                        success = display ? web_rtc_display_service_->HandleRemoteMessage(message_type, decoded)
                                          : web_rtc_camera_service_->HandleRemoteMessage(message_type, decoded);
                        if (success) cJSON_AddStringToObject(result, "sessionId", session_id.c_str());
                        else error_code = prefix + "_signal_rejected";
                    }
                }
            }
        }
        // A disconnect or terminal callback may have invalidated an admitted
        // operation. Finish its own peer before the next command can start one.
        CleanupRevokedStreamsLocked();
        operation_lock.unlock();
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
    {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        // A completed operation remains replayable after a transport epoch
        // changes. The ledger ticket rejects completion after authority reset.
        command_ledger_.Complete(admission.ticket, ack_payload, completion_scope);
    }

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
                 "psram_free=%u psram_largest=%u telemetry_queued=%d dma_free=%u dma_largest=%u",
             connected_.load(),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             telemetry_queued,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

void UnifiedMqttService::PublishChangedVoiceIdentity() {
    if (!started_.load() || !connected_.load() || voice_wake_ == nullptr) return;
    const auto state = voice_wake_->GetState();
    cJSON* report = BuildVoiceIdentityReport(state);
    const std::string encoded = EncodeJson(report);
    cJSON_Delete(report);
    if (encoded != last_voice_identity_report_) PublishShadowReport(&state);
}

void UnifiedMqttService::PublishShadowReport(const VoiceWakeState* voice_state) {
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
    std::string identity_report;
    if (voice_wake_ != nullptr) {
        const VoiceWakeState state = voice_state != nullptr ? *voice_state : voice_wake_->GetState();
        cJSON* identity_json = BuildVoiceIdentityReport(state);
        identity_report = EncodeJson(identity_json);
        cJSON_AddItemToObject(root, "voice_identity", identity_json);
    }
    const std::string payload = EncodeJson(root);
    cJSON_Delete(root);
    if (Publish(CopyTopic(&DeviceCloudConfig::mqtt_topic_shadow_report), payload))
        last_voice_identity_report_ = std::move(identity_report);
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

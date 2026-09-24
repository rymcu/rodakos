#include "phone_os/device_cloud_config.h"
#include "phone_os/device_pairing_policy.h"
#include "phone_os/device_pairing_protocol.h"
#include "phone_os/realtime_voice_contract.h"
#include "phone_os/serial_provisioning_protocol.h"

#include "settings.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_chip_info.h>
#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_random.h>
#include <esp_system.h>
#include <nvs.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace rodakos {
namespace {
constexpr const char* TAG = "DeviceCloud";
constexpr const char* kCloudNamespace = "device_cloud";
constexpr const char* kRealtimeVoiceNamespace = "realtime_voice";
constexpr const char* kMqttNamespace = "unified_mqtt";
constexpr const char* kBoardNamespace = "board";
constexpr const char* kProvisioningUrlKey = "prov_url";
constexpr char kRealtimeVoiceEndpointKey[] = "endpoint";
constexpr char kRealtimeVoiceProtocolVersionKey[] = "protocol_ver";
constexpr char kRealtimeVoiceDownlinkSampleRateKey[] = "downlink_hz";
constexpr char kRealtimeVoiceDownlinkFrameDurationKey[] = "downlink_ms";
constexpr char kRealtimeVoiceMaxAudioFrameKey[] = "max_audio_bytes";
constexpr char kRealtimeVoiceMaxControlKey[] = "max_ctrl_bytes";
constexpr char kRealtimeVoiceVadStrategiesKey[] = "vad_strategies";
constexpr char kRealtimeVoicePreferredVadStrategyKey[] = "preferred_vad";
static_assert(sizeof(kRealtimeVoiceEndpointKey) <= NVS_KEY_NAME_MAX_SIZE &&
              sizeof(kRealtimeVoiceProtocolVersionKey) <= NVS_KEY_NAME_MAX_SIZE &&
              sizeof(kRealtimeVoiceDownlinkSampleRateKey) <= NVS_KEY_NAME_MAX_SIZE &&
              sizeof(kRealtimeVoiceDownlinkFrameDurationKey) <= NVS_KEY_NAME_MAX_SIZE &&
              sizeof(kRealtimeVoiceMaxAudioFrameKey) <= NVS_KEY_NAME_MAX_SIZE &&
              sizeof(kRealtimeVoiceMaxControlKey) <= NVS_KEY_NAME_MAX_SIZE &&
              sizeof(kRealtimeVoiceVadStrategiesKey) <= NVS_KEY_NAME_MAX_SIZE &&
              sizeof(kRealtimeVoicePreferredVadStrategyKey) <= NVS_KEY_NAME_MAX_SIZE,
              "Realtime voice NVS keys exceed the NVS name limit");
constexpr const char* kUuidKey = "uuid";
constexpr const char* kAiotSecretKey = "device_secret";
constexpr const char* kAiotTokenKey = "access_token";
constexpr const char* kAiotRegisteredKey = "registered";
constexpr const char* kAiotActivatedKey = "activated";
constexpr const char* kAiotPendingKey = "pending";
constexpr const char* kUnbindPendingKey = "unbind_pending";
constexpr const char* kUnbindAckKey = "unbind_ack";
constexpr const char* kPairingRequestIdKey = "pair_req_id";
constexpr const char* kPairingRequestTokenKey = "pair_req_token";
constexpr const char* kPairingCodeKey = "pair_code";
constexpr const char* kPairingExpiresAtKey = "pair_expires";
constexpr const char* kPairingStatusKey = "pair_status";
constexpr const char* kMqttProtocolVersionKey = "protocol_ver";
constexpr const char* kMqttBrokerAddressKey = "broker_address";
constexpr const char* kMqttBrokerPortKey = "broker_port";
constexpr const char* kMqttUsernameKey = "username";
constexpr const char* kMqttPasswordKey = "password";
constexpr const char* kMqttKeepaliveKey = "keepalive";
constexpr const char* kMqttDeviceKey = "device_key";
constexpr const char* kMqttHomeEnabledKey = "home_enabled";
constexpr const char* kMqttHttpBaseUrlKey = "http_base_url";
constexpr const char* kMqttTelemetryTopicKey = "telemetry";
constexpr const char* kMqttShadowReportTopicKey = "shadow_report";
constexpr const char* kMqttShadowDesiredTopicKey = "shadow_desired";
constexpr const char* kMqttOtaNotifyTopicKey = "ota_notify";
constexpr const char* kMqttOtaProgressTopicKey = "ota_progress";
constexpr const char* kMqttCommandsTopicKey = "commands";
constexpr const char* kMqttPcStatusTopicKey = "pc_status";
constexpr const char* kMqttHomePrefixTopicKey = "home_prefix";
// Deployments should provision an explicit Rodak server URL. This reserved
// canonical placeholder deliberately does not point at a legacy service.
constexpr const char* kDefaultProvisioningUrl =
    "https://api.rodak.local/api/v1/aiot/devices/bootstrap";
constexpr int kDefaultMqttBrokerPort = 1883;

int NormalizeRealtimeVoiceSampleRate(int value) {
    return (value == 8000 || value == 12000 || value == 16000 || value == 24000 ||
            value == 48000)
               ? value
               : 24000;
}

int NormalizeRealtimeVoiceFrameDuration(int value) {
    return IsSupportedRealtimeVoiceFrameDuration(value) ? value : 60;
}
constexpr int kDefaultMqttKeepalive = 240;
constexpr int kProvisioningTimeoutMs = 10000;
constexpr size_t kMaxAiotResponseBytes = 16384;

constexpr const char* kAiotBootstrapPath = "/api/v1/aiot/devices/bootstrap";
constexpr const char* kAiotTokenPath = "/api/v1/aiot/devices/auth/token";
constexpr const char* kAiotBindingRequestPath = "/api/v1/aiot/devices/binding/request";
constexpr const char* kAiotBindingStatusPrefix =
    "/api/v1/aiot/devices/binding/requests/";
constexpr const char* kAiotUnbindPath = "/api/v1/aiot/devices/binding/unbind";

struct HttpResponse {
    int status_code = 0;
    std::string body;
};

int ResponseBusinessCode(const HttpResponse& response) {
    cJSON* root = cJSON_Parse(response.body.c_str());
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return 0;
    }
    const cJSON* code = cJSON_GetObjectItemCaseSensitive(root, "code");
    const int value = cJSON_IsNumber(code) ? code->valueint : 0;
    cJSON_Delete(root);
    return value;
}

std::string MacAddress() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buffer[18];
    std::snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buffer;
}

std::string GenerateUuid() {
    uint8_t uuid[16];
    esp_fill_random(uuid, sizeof(uuid));
    uuid[6] = (uuid[6] & 0x0F) | 0x40;
    uuid[8] = (uuid[8] & 0x3F) | 0x80;

    char uuid_str[37];
    std::snprintf(uuid_str, sizeof(uuid_str),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  uuid[0], uuid[1], uuid[2], uuid[3],
                  uuid[4], uuid[5], uuid[6], uuid[7],
                  uuid[8], uuid[9], uuid[10], uuid[11],
                  uuid[12], uuid[13], uuid[14], uuid[15]);
    return uuid_str;
}

std::string GenerateDeviceSecret() {
    // The server stores only a hash of this value. Keep it printable so it can
    // be sent in JSON without an additional encoding dependency.
    uint8_t bytes[32] = {};
    esp_fill_random(bytes, sizeof(bytes));
    char secret[sizeof(bytes) * 2 + 1] = {};
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        std::snprintf(secret + i * 2, 3, "%02x", bytes[i]);
    }
    return secret;
}

std::string Lowercase(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool IsExplicitAiotUrl(const std::string& url) {
    return url.size() >= std::strlen(kAiotBootstrapPath) &&
           url.ends_with(kAiotBootstrapPath);
}

std::string UrlOrigin(const std::string& url) {
    const size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        return {};
    }
    const size_t path_start = url.find('/', scheme_end + 3);
    return path_start == std::string::npos ? url : url.substr(0, path_start);
}

std::string UrlHost(const std::string& origin) {
    const size_t scheme_end = origin.find("://");
    const size_t host_start = scheme_end == std::string::npos ? 0 : scheme_end + 3;
    if (host_start >= origin.size()) {
        return {};
    }
    if (origin[host_start] == '[') {
        const size_t closing = origin.find(']', host_start + 1);
        return closing == std::string::npos ? std::string() :
               origin.substr(host_start + 1, closing - host_start - 1);
    }
    const size_t host_end = origin.find(':', host_start);
    return origin.substr(host_start, host_end == std::string::npos ? std::string::npos
                                                                    : host_end - host_start);
}

std::string ResolveApiUrl(const std::string& configured_url, const char* path) {
    if (configured_url.rfind("http://", 0) != 0 &&
        configured_url.rfind("https://", 0) != 0) {
        return {};
    }
    const std::string origin = UrlOrigin(configured_url);
    return origin.empty() ? std::string() : origin + path;
}

std::string ResolveAiotBootstrapUrl(const std::string& configured_url) {
    if (IsExplicitAiotUrl(configured_url)) {
        return configured_url;
    }
    return ResolveApiUrl(configured_url, kAiotBootstrapPath);
}

bool IsForbiddenLegacyProvisioningHost(const std::string& url) {
    const std::string host = Lowercase(UrlHost(UrlOrigin(url)));
    return host == "tenclass.net" ||
           (host.size() > std::string(".tenclass.net").size() &&
            host.ends_with(".tenclass.net"));
}

bool PerformHttpRequest(const std::string& url,
                       esp_http_client_method_t method,
                       const std::string& body,
                       const std::string& bearer_token,
                       size_t max_response_bytes,
                       HttpResponse& output,
                       std::string& error) {
    if (url.empty()) {
        error = "AIoT endpoint URL is empty";
        return false;
    }

    esp_http_client_config_t http_config = {};
    http_config.url = url.c_str();
    http_config.method = method;
    http_config.timeout_ms = kProvisioningTimeoutMs;
    http_config.buffer_size = 1024;
    http_config.buffer_size_tx = 1024;
    http_config.crt_bundle_attach = esp_crt_bundle_attach;
    http_config.user_agent = "RodakOS/aiot";

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) {
        error = "Failed to create AIoT HTTP client";
        return false;
    }

    esp_http_client_set_header(client, "Accept", "application/json");
    if (method == HTTP_METHOD_POST) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }
    if (!bearer_token.empty()) {
        const std::string authorization =
            bearer_token.rfind("Bearer ", 0) == 0 ? bearer_token : "Bearer " + bearer_token;
        esp_http_client_set_header(client, "Authorization", authorization.c_str());
    }

    const esp_err_t open_result = esp_http_client_open(client, body.size());
    if (open_result != ESP_OK) {
        error = std::string("AIoT HTTP open failed: ") + esp_err_to_name(open_result);
        esp_http_client_cleanup(client);
        return false;
    }

    if (!body.empty()) {
        const int written = esp_http_client_write(client, body.c_str(), body.size());
        if (written != static_cast<int>(body.size())) {
            error = written < 0 ? "AIoT HTTP request write failed"
                                : "AIoT HTTP request write incomplete";
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
    }

    if (esp_http_client_fetch_headers(client) < 0) {
        error = "AIoT HTTP response headers failed";
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    output.status_code = esp_http_client_get_status_code(client);
    std::vector<char> response(max_response_bytes + 1, '\0');
    const int read_len = esp_http_client_read_response(
        client, response.data(), max_response_bytes);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (read_len < 0) {
        error = "AIoT HTTP response read failed";
        return false;
    }
    output.body.assign(response.data(), static_cast<size_t>(read_len));
    if (read_len == static_cast<int>(max_response_bytes)) {
        error = "AIoT HTTP response is too large";
        return false;
    }
    return true;
}

cJSON* ResponseData(cJSON* root, std::string& error) {
    if (!cJSON_IsObject(root)) {
        error = "AIoT response is not an object";
        return nullptr;
    }
    const cJSON* code = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (cJSON_IsNumber(code) && code->valueint != 200) {
        const cJSON* message = cJSON_GetObjectItemCaseSensitive(root, "message");
        error = cJSON_IsString(message) && message->valuestring != nullptr
                    ? message->valuestring
                    : "AIoT server rejected the request";
        return nullptr;
    }
    cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
    return data != nullptr ? data : root;
}

bool ParseResponse(const HttpResponse& response, cJSON*& root, cJSON*& data,
                   std::string& error) {
    if (response.status_code < 200 || response.status_code >= 300) {
        error = "AIoT HTTP status " + std::to_string(response.status_code);
        return false;
    }
    root = cJSON_Parse(response.body.c_str());
    if (root == nullptr) {
        error = "AIoT response is not JSON";
        return false;
    }
    data = ResponseData(root, error);
    if (data == nullptr) {
        cJSON_Delete(root);
        root = nullptr;
        return false;
    }
    return true;
}

void AddStringAlias(cJSON* object, const char* key, std::string& output) {
    if (!cJSON_IsObject(object)) {
        return;
    }
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(value) && value->valuestring != nullptr && output.empty()) {
        output = value->valuestring;
    }
}

void AddIntAlias(cJSON* object, const char* key, int& output) {
    if (!cJSON_IsObject(object)) {
        return;
    }
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(value) && value->valueint > 0) {
        output = value->valueint;
    }
}

std::string JsonToString(cJSON* root) {
    char* json = cJSON_PrintUnformatted(root);
    if (json == nullptr) {
        return "{}";
    }
    std::string result(json);
    cJSON_free(json);
    return result;
}

std::string SerializeStringArray(const std::vector<std::string>& values) {
    cJSON* root = cJSON_CreateArray();
    if (root == nullptr) return "[]";
    for (const std::string& value : values) {
        cJSON_AddItemToArray(root, cJSON_CreateString(value.c_str()));
    }
    const std::string result = JsonToString(root);
    cJSON_Delete(root);
    return result;
}

std::vector<std::string> ParseStringArray(const std::string& encoded) {
    std::vector<std::string> values;
    cJSON* root = cJSON_Parse(encoded.c_str());
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return values;
    }
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        if (cJSON_IsString(item) && item->valuestring != nullptr) {
            values.emplace_back(item->valuestring);
        }
    }
    cJSON_Delete(root);
    return values;
}

void ResetMqttConfig(DeviceCloudConfig& config) {
    config.mqtt_protocol_version = 1;
    config.mqtt_broker_address.clear();
    config.mqtt_broker_port = kDefaultMqttBrokerPort;
    config.mqtt_username.clear();
    config.mqtt_password.clear();
    config.mqtt_keepalive = kDefaultMqttKeepalive;
    config.mqtt_device_key.clear();
    config.mqtt_home_enabled = false;
    config.mqtt_http_base_url.clear();
    config.mqtt_topic_telemetry.clear();
    config.mqtt_topic_shadow_report.clear();
    config.mqtt_topic_shadow_desired.clear();
    config.mqtt_topic_ota_notify.clear();
    config.mqtt_topic_ota_progress.clear();
    config.mqtt_topic_commands.clear();
    config.mqtt_topic_pc_status.clear();
    config.mqtt_topic_home_prefix.clear();
    config.has_mqtt_config = false;
    config.has_aiot_config = false;
}

void ResetAiotCredentials(DeviceCloudConfig& config) {
    config.aiot_access_token.clear();
    config.aiot_registered = false;
    config.aiot_activated = false;
    config.aiot_pending = false;
    config.has_aiot_config = false;
}

void ResetPairingRequest(DeviceCloudConfig& config) {
    config.pairing_request_id.clear();
    config.pairing_request_token.clear();
    config.pairing_code.clear();
    config.pairing_expires_at.clear();
    config.pairing_status.clear();
    config.has_pairing_request = false;
}

bool HasCompleteAiotConfig(const DeviceCloudConfig& config) {
    return !config.aiot_device_secret.empty() &&
           !config.aiot_access_token.empty() &&
           config.aiot_registered && config.aiot_activated &&
           !config.aiot_pending && config.has_mqtt_config;
}

bool PersistAiotIdentity(const DeviceCloudConfig& config) {
    Settings settings(kCloudNamespace, true);
    const bool written = settings.SetString(kAiotSecretKey, config.aiot_device_secret) &&
                         settings.SetString(kAiotTokenKey, config.aiot_access_token) &&
                         settings.SetBool(kAiotRegisteredKey, config.aiot_registered) &&
                         settings.SetBool(kAiotActivatedKey, config.aiot_activated) &&
                         settings.SetBool(kAiotPendingKey, config.aiot_pending) &&
                         settings.SetBool(kUnbindPendingKey, config.unbind_pending) &&
                         settings.SetBool(kUnbindAckKey, config.unbind_server_acknowledged) &&
                         settings.SetString(kPairingRequestIdKey, config.pairing_request_id) &&
                         settings.SetString(kPairingRequestTokenKey, config.pairing_request_token) &&
                         settings.SetString(kPairingCodeKey, config.pairing_code) &&
                         settings.SetString(kPairingExpiresAtKey, config.pairing_expires_at) &&
                         settings.SetString(kPairingStatusKey, config.pairing_status);
    if (!written || !settings.Commit()) {
        (void)settings.Commit();
        return false;
    }
    return true;
}

bool PersistRealtimeVoiceConfig(const DeviceCloudConfig& config) {
    Settings settings(kRealtimeVoiceNamespace, true);
    const bool written =
        settings.SetString(kRealtimeVoiceEndpointKey, config.realtime_voice_url) &&
        settings.SetInt(kRealtimeVoiceProtocolVersionKey,
                        config.realtime_voice_protocol_version) &&
        settings.SetInt(kRealtimeVoiceDownlinkSampleRateKey,
                        config.realtime_voice_downlink_sample_rate_hz) &&
        settings.SetInt(kRealtimeVoiceDownlinkFrameDurationKey,
                        config.realtime_voice_downlink_frame_duration_ms) &&
        settings.SetInt(kRealtimeVoiceMaxAudioFrameKey,
                        static_cast<int32_t>(config.realtime_voice_max_audio_frame_bytes)) &&
        settings.SetInt(kRealtimeVoiceMaxControlKey,
                        static_cast<int32_t>(config.realtime_voice_max_control_bytes)) &&
        settings.SetString(kRealtimeVoiceVadStrategiesKey,
                           SerializeStringArray(config.realtime_voice_vad_strategies)) &&
        settings.SetString(kRealtimeVoicePreferredVadStrategyKey,
                           config.realtime_voice_preferred_vad_strategy);
    if (!written || !settings.Commit()) {
        // NVSHandleSimple may have applied an earlier field before reporting
        // an error. Mark the handle settled before the destructor and let the
        // caller restore its snapshot through a fresh handle.
        (void)settings.Commit();
        return false;
    }
    return true;
}

bool PersistMqttConfig(const DeviceCloudConfig& config) {
    Settings settings(kMqttNamespace, true);
    const bool written =
        settings.SetInt(kMqttProtocolVersionKey, config.mqtt_protocol_version) &&
        settings.SetString(kMqttBrokerAddressKey, config.mqtt_broker_address) &&
        settings.SetInt(kMqttBrokerPortKey, config.mqtt_broker_port) &&
        settings.SetString(kMqttUsernameKey, config.mqtt_username) &&
        settings.SetString(kMqttPasswordKey, config.mqtt_password) &&
        settings.SetInt(kMqttKeepaliveKey, config.mqtt_keepalive) &&
        settings.SetString(kMqttDeviceKey, config.mqtt_device_key) &&
        settings.SetBool(kMqttHomeEnabledKey, config.mqtt_home_enabled) &&
        settings.SetString(kMqttHttpBaseUrlKey, config.mqtt_http_base_url) &&
        settings.SetString(kMqttTelemetryTopicKey, config.mqtt_topic_telemetry) &&
        settings.SetString(kMqttShadowReportTopicKey, config.mqtt_topic_shadow_report) &&
        settings.SetString(kMqttShadowDesiredTopicKey, config.mqtt_topic_shadow_desired) &&
        settings.SetString(kMqttOtaNotifyTopicKey, config.mqtt_topic_ota_notify) &&
        settings.SetString(kMqttOtaProgressTopicKey, config.mqtt_topic_ota_progress) &&
        settings.SetString(kMqttCommandsTopicKey, config.mqtt_topic_commands) &&
        settings.SetString(kMqttPcStatusTopicKey, config.mqtt_topic_pc_status) &&
        settings.SetString(kMqttHomePrefixTopicKey, config.mqtt_topic_home_prefix);
    if (!written || !settings.Commit()) {
        (void)settings.Commit();
        return false;
    }
    return true;
}

void FillMqttTopicFallbacks(DeviceCloudConfig& config) {
    if (config.mqtt_device_key.empty()) {
        return;
    }
    const std::string prefix = "devices/" + config.mqtt_device_key;
    if (config.mqtt_topic_telemetry.empty()) {
        config.mqtt_topic_telemetry = prefix + "/telemetry";
    }
    if (config.mqtt_topic_shadow_report.empty()) {
        config.mqtt_topic_shadow_report = prefix + "/shadow/report";
    }
    if (config.mqtt_topic_shadow_desired.empty()) {
        config.mqtt_topic_shadow_desired = prefix + "/shadow/desired";
    }
    if (config.mqtt_topic_ota_notify.empty()) {
        config.mqtt_topic_ota_notify = prefix + "/ota/notify";
    }
    if (config.mqtt_topic_ota_progress.empty()) {
        config.mqtt_topic_ota_progress = prefix + "/ota/progress";
    }
    if (config.mqtt_topic_commands.empty()) {
        config.mqtt_topic_commands = prefix + "/commands/+";
    }
    if (config.mqtt_topic_pc_status.empty()) {
        config.mqtt_topic_pc_status = prefix + "/pc_status";
    }
}

void FinalizeMqttConfig(DeviceCloudConfig& config) {
    if (config.mqtt_protocol_version <= 0) {
        config.mqtt_protocol_version = 1;
    }
    if (config.mqtt_broker_port <= 0 || config.mqtt_broker_port > 65535) {
        config.mqtt_broker_port = kDefaultMqttBrokerPort;
    }
    if (config.mqtt_keepalive <= 0) {
        config.mqtt_keepalive = kDefaultMqttKeepalive;
    }
    FillMqttTopicFallbacks(config);
    config.has_mqtt_config = !config.mqtt_broker_address.empty() &&
                             !config.mqtt_username.empty() &&
                             !config.mqtt_password.empty() &&
                             !config.mqtt_device_key.empty();
}

}  // namespace

const char* DeviceCloudConfigService::DefaultProvisioningUrl() {
    return kDefaultProvisioningUrl;
}

bool DeviceCloudConfigService::Load(DeviceCloudConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(config_mutex_);
    Settings cloud_settings(kCloudNamespace, false);
    config.aiot_device_secret = cloud_settings.GetString(kAiotSecretKey, "");
    config.aiot_access_token = cloud_settings.GetString(kAiotTokenKey, "");
    config.aiot_registered = cloud_settings.GetBool(kAiotRegisteredKey, false);
    config.aiot_activated = cloud_settings.GetBool(kAiotActivatedKey, false);
    config.aiot_pending = cloud_settings.GetBool(kAiotPendingKey, false);
    config.unbind_pending = cloud_settings.GetBool(kUnbindPendingKey, false);
    config.unbind_server_acknowledged = cloud_settings.GetBool(kUnbindAckKey, false);
    config.pairing_request_id = cloud_settings.GetString(kPairingRequestIdKey, "");
    config.pairing_request_token = cloud_settings.GetString(kPairingRequestTokenKey, "");
    config.pairing_code = cloud_settings.GetString(kPairingCodeKey, "");
    config.pairing_expires_at = cloud_settings.GetString(kPairingExpiresAtKey, "");
    config.pairing_status = cloud_settings.GetString(kPairingStatusKey, "");
    config.has_pairing_request = !config.pairing_request_id.empty() &&
                                 !config.pairing_request_token.empty() &&
                                 !config.pairing_code.empty();
    config.provisioning_url = cloud_settings.GetString(kProvisioningUrlKey, "");
    if (config.provisioning_url.empty()) {
        config.provisioning_url = kDefaultProvisioningUrl;
    }

    Settings realtime_voice_settings(kRealtimeVoiceNamespace, false);
    config.realtime_voice_url =
        realtime_voice_settings.GetString(kRealtimeVoiceEndpointKey, "");
    config.realtime_voice_protocol_version = realtime_voice_settings.GetInt(
        kRealtimeVoiceProtocolVersionKey, kRodakRealtimeVoiceProtocolVersion);
    config.realtime_voice_downlink_sample_rate_hz = realtime_voice_settings.GetInt(
        kRealtimeVoiceDownlinkSampleRateKey, 24000);
    config.realtime_voice_downlink_frame_duration_ms = realtime_voice_settings.GetInt(
        kRealtimeVoiceDownlinkFrameDurationKey, 60);
    config.realtime_voice_downlink_sample_rate_hz = NormalizeRealtimeVoiceSampleRate(
        config.realtime_voice_downlink_sample_rate_hz);
    config.realtime_voice_downlink_frame_duration_ms = NormalizeRealtimeVoiceFrameDuration(
        config.realtime_voice_downlink_frame_duration_ms);
    config.realtime_voice_max_audio_frame_bytes = static_cast<size_t>(
        std::clamp<int32_t>(realtime_voice_settings.GetInt(kRealtimeVoiceMaxAudioFrameKey, 8192),
                            1, 64 * 1024));
    config.realtime_voice_max_control_bytes = static_cast<size_t>(
        std::clamp<int32_t>(realtime_voice_settings.GetInt(kRealtimeVoiceMaxControlKey, 64 * 1024),
                            1, 256 * 1024));
    config.realtime_voice_vad_strategies = ParseStringArray(
        realtime_voice_settings.GetString(kRealtimeVoiceVadStrategiesKey, "[]"));
    config.realtime_voice_preferred_vad_strategy = realtime_voice_settings.GetString(
        kRealtimeVoicePreferredVadStrategyKey, kRealtimeVoiceVadServerAuthoritative);
    if (!IsRealtimeVoiceVadStrategy(config.realtime_voice_preferred_vad_strategy)) {
        config.realtime_voice_preferred_vad_strategy = kRealtimeVoiceVadServerAuthoritative;
    }
    config.realtime_voice_vad_strategies.erase(
        std::remove_if(config.realtime_voice_vad_strategies.begin(),
                       config.realtime_voice_vad_strategies.end(),
                       [](const std::string& value) {
                           return !IsRealtimeVoiceVadStrategy(value);
                       }),
        config.realtime_voice_vad_strategies.end());
    if (config.realtime_voice_vad_strategies.empty() ||
        std::find(config.realtime_voice_vad_strategies.begin(),
                  config.realtime_voice_vad_strategies.end(),
                  config.realtime_voice_preferred_vad_strategy) ==
            config.realtime_voice_vad_strategies.end()) {
        config.realtime_voice_vad_strategies.clear();
        config.realtime_voice_preferred_vad_strategy = kRealtimeVoiceVadServerAuthoritative;
    }
    if (config.realtime_voice_protocol_version <= 0) {
        config.realtime_voice_protocol_version = kRodakRealtimeVoiceProtocolVersion;
    }

    ResetMqttConfig(config);
    Settings mqtt_settings(kMqttNamespace, false);
    config.mqtt_protocol_version = mqtt_settings.GetInt(kMqttProtocolVersionKey, 1);
    config.mqtt_broker_address = mqtt_settings.GetString(kMqttBrokerAddressKey, "");
    config.mqtt_broker_port = mqtt_settings.GetInt(kMqttBrokerPortKey, kDefaultMqttBrokerPort);
    config.mqtt_username = mqtt_settings.GetString(kMqttUsernameKey, "");
    config.mqtt_password = mqtt_settings.GetString(kMqttPasswordKey, "");
    config.mqtt_keepalive = mqtt_settings.GetInt(kMqttKeepaliveKey, kDefaultMqttKeepalive);
    config.mqtt_device_key = mqtt_settings.GetString(kMqttDeviceKey, "");
    config.mqtt_home_enabled = mqtt_settings.GetBool(kMqttHomeEnabledKey, false);
    config.mqtt_http_base_url = mqtt_settings.GetString(kMqttHttpBaseUrlKey, "");
    config.mqtt_topic_telemetry = mqtt_settings.GetString(kMqttTelemetryTopicKey, "");
    config.mqtt_topic_shadow_report = mqtt_settings.GetString(kMqttShadowReportTopicKey, "");
    config.mqtt_topic_shadow_desired = mqtt_settings.GetString(kMqttShadowDesiredTopicKey, "");
    config.mqtt_topic_ota_notify = mqtt_settings.GetString(kMqttOtaNotifyTopicKey, "");
    config.mqtt_topic_ota_progress = mqtt_settings.GetString(kMqttOtaProgressTopicKey, "");
    config.mqtt_topic_commands = mqtt_settings.GetString(kMqttCommandsTopicKey, "");
    config.mqtt_topic_pc_status = mqtt_settings.GetString(kMqttPcStatusTopicKey, "");
    config.mqtt_topic_home_prefix = mqtt_settings.GetString(kMqttHomePrefixTopicKey, "");
    FinalizeMqttConfig(config);
    // A pending AIoT transaction may have written a new MQTT namespace before
    // power was lost. Do not let a boot-time service consume that candidate;
    // the next refresh must establish a complete pair again.
    if (config.aiot_pending) {
        config.has_mqtt_config = false;
        // A partially committed pairing or unbind transaction must fail
        // closed across every cloud transport.
    }
    config.has_aiot_config = HasCompleteAiotConfig(config);
    config.has_realtime_voice_config = config.has_aiot_config &&
                                       !config.realtime_voice_url.empty();
    return config.has_aiot_config;
}

bool DeviceCloudConfigService::RefreshAiot(DeviceCloudConfig& config) {
    uint32_t config_generation = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(config_mutex_);
        config_generation = config_generation_;
    }
    const std::string bootstrap_url = ResolveAiotBootstrapUrl(config.provisioning_url);
    const std::string origin = UrlOrigin(bootstrap_url);
    if (origin.empty()) {
        SetError("AIoT bootstrap URL has no valid origin");
        return false;
    }

    std::string error;
    HttpResponse bootstrap_response;
    if (!PerformHttpRequest(bootstrap_url, HTTP_METHOD_GET, {}, {},
                            kMaxAiotResponseBytes, bootstrap_response, error)) {
        SetError(error);
        return false;
    }

    cJSON* bootstrap_root = nullptr;
    cJSON* bootstrap_data = nullptr;
    if (!ParseResponse(bootstrap_response, bootstrap_root, bootstrap_data, error)) {
        SetError(error);
        return false;
    }

    const cJSON* bootstrap_module =
        cJSON_GetObjectItemCaseSensitive(bootstrap_data, "module");
    const cJSON* bootstrap_protocol =
        cJSON_GetObjectItemCaseSensitive(bootstrap_data, "protocol");
    const cJSON* bootstrap_product =
        cJSON_GetObjectItemCaseSensitive(bootstrap_data, "productKey");
    if (!cJSON_IsString(bootstrap_product)) {
        bootstrap_product = cJSON_GetObjectItemCaseSensitive(bootstrap_data, "product_key");
    }
    if (cJSON_IsString(bootstrap_module) && bootstrap_module->valuestring != nullptr &&
        std::strcmp(bootstrap_module->valuestring, "aiot") != 0) {
        SetError("AIoT bootstrap module is unsupported");
        cJSON_Delete(bootstrap_root);
        return false;
    }
    if (cJSON_IsString(bootstrap_protocol) && bootstrap_protocol->valuestring != nullptr &&
        std::strcmp(bootstrap_protocol->valuestring, kRodakAiotProtocol) != 0) {
        SetError("AIoT bootstrap protocol is unsupported");
        cJSON_Delete(bootstrap_root);
        return false;
    }
    if (cJSON_IsString(bootstrap_product) && bootstrap_product->valuestring != nullptr &&
        std::strcmp(bootstrap_product->valuestring, kRodakBigSmartProductKey) != 0) {
        SetError("AIoT bootstrap product is not rymcu-bigsmart");
        cJSON_Delete(bootstrap_root);
        return false;
    }

    int protocol_version = 0;
    int mqtt_port = 0;
    std::string mqtt_host;
    int mqtt_protocol = 0;
    std::string mqtt_http_base_url;
    std::string mqtt_username;
    std::string mqtt_password;
    std::string mqtt_device_key;
    std::string telemetry_topic;
    std::string shadow_report_topic;
    std::string shadow_desired_topic;
    std::string ota_notify_topic;
    std::string ota_progress_topic;
    std::string commands_topic;
    std::string pc_status_topic;
    std::string home_prefix_topic;
    int mqtt_keepalive = 0;
    bool mqtt_home_enabled = false;
    std::string activation_code;
    std::string activation_message;

    AddIntAlias(bootstrap_data, "protocolVersion", protocol_version);
    AddIntAlias(bootstrap_data, "protocol_version", protocol_version);
    AddIntAlias(bootstrap_data, "mqttPort", mqtt_port);
    AddIntAlias(bootstrap_data, "mqtt_port", mqtt_port);
    AddStringAlias(bootstrap_data, "mqttHost", mqtt_host);
    AddStringAlias(bootstrap_data, "mqtt_host", mqtt_host);
    if (protocol_version <= 0) {
        protocol_version = kRodakAiotProtocolVersion;
    }
    if (protocol_version != kRodakAiotProtocolVersion) {
        SetError("AIoT bootstrap protocol version is unsupported");
        cJSON_Delete(bootstrap_root);
        return false;
    }
    if (mqtt_port <= 0 || mqtt_port > 65535) {
        mqtt_port = kDefaultMqttBrokerPort;
    }

    cJSON* token_root = nullptr;
    cJSON* token_data = nullptr;
    const std::string device_key = MacAddress();
    const std::string client_id = GetClientId();
    const esp_app_desc_t* app_desc = esp_app_get_description();
    const std::string firmware_version = app_desc != nullptr ? app_desc->version : "unknown";

    // A device that already completed manual pairing may rotate an expired
    // token with its persisted secret. Requiring another short-code challenge
    // for routine MQTT recovery would make token expiry strand bound devices.
    if (config.aiot_registered && config.aiot_activated &&
        !config.aiot_device_secret.empty() && !config.has_pairing_request) {
        cJSON* token_body = cJSON_CreateObject();
        cJSON_AddStringToObject(token_body, "protocol", kRodakAiotProtocol);
        cJSON_AddStringToObject(token_body, "productKey", kRodakBigSmartProductKey);
        cJSON_AddStringToObject(token_body, "deviceKey", device_key.c_str());
        cJSON_AddStringToObject(token_body, "credentialSecret",
                                config.aiot_device_secret.c_str());
        const std::string token_json = JsonToString(token_body);
        cJSON_Delete(token_body);

        HttpResponse token_response;
        if (!PerformHttpRequest(origin + kAiotTokenPath, HTTP_METHOD_POST, token_json, {},
                                kMaxAiotResponseBytes, token_response, error)) {
            SetError(error);
            cJSON_Delete(bootstrap_root);
            return false;
        }
        const int business_code = ResponseBusinessCode(token_response);
        if (token_response.status_code == 401 || token_response.status_code == 403 ||
            token_response.status_code == 404 || business_code == 401 ||
            business_code == 403 || business_code == 404) {
            // The server no longer accepts this identity (for example after a
            // remote unbind). Fail closed and fall through to a new challenge.
            ResetAiotCredentials(config);
            ResetMqttConfig(config);
        } else if (!ParseResponse(token_response, token_root, token_data, error)) {
            SetError("AIoT token refresh failed: " + error);
            cJSON_Delete(bootstrap_root);
            return false;
        }
    }

    if (token_data == nullptr) {
    // Pairing is an explicit user-mediated gate. The device may prepare a
    // random secret and display a short code, but must not obtain cloud
    // credentials until the owner confirms that code in Rodak.
    if (config.aiot_device_secret.empty()) {
        config.aiot_device_secret = GenerateDeviceSecret();
    }
    config.aiot_pending = true;
    {
        std::lock_guard<std::recursive_mutex> lock(config_mutex_);
        if (!PersistAiotIdentity(config)) {
            SetError("Failed to persist AIoT device secret");
            cJSON_Delete(bootstrap_root);
            return false;
        }
    }

    std::string pairing_status;
    if (config.pairing_request_id.empty() || config.pairing_request_token.empty()) {
        cJSON* request_body = cJSON_CreateObject();
        cJSON_AddStringToObject(request_body, "protocol", kRodakAiotProtocol);
        cJSON_AddNumberToObject(request_body, "protocolVersion", kRodakAiotProtocolVersion);
        cJSON_AddStringToObject(request_body, "productKey", kRodakBigSmartProductKey);
        cJSON_AddStringToObject(request_body, "product_key", kRodakBigSmartProductKey);
        cJSON_AddStringToObject(request_body, "deviceCode", device_key.c_str());
        cJSON_AddStringToObject(request_body, "deviceKey", device_key.c_str());
        cJSON_AddStringToObject(request_body, "deviceSecret", config.aiot_device_secret.c_str());
        cJSON_AddStringToObject(request_body, "credentialSecret", config.aiot_device_secret.c_str());
        cJSON_AddStringToObject(request_body, "deviceName", "RodakOS RYMCU BigSmart");
        cJSON_AddStringToObject(request_body, "clientId", client_id.c_str());
        cJSON_AddStringToObject(request_body, "firmwareVersion", firmware_version.c_str());
        cJSON_AddStringToObject(request_body, "hardwareVersion", "rymcu_bigsmart");
        cJSON* application = cJSON_CreateObject();
        cJSON_AddStringToObject(application, "name", "rodakos");
        cJSON_AddStringToObject(application, "version", firmware_version.c_str());
        cJSON_AddItemToObject(request_body, "application", application);
        cJSON* board = cJSON_CreateObject();
        cJSON_AddStringToObject(board, "type", "rymcu_bigsmart");
        cJSON_AddStringToObject(board, "productKey", kRodakBigSmartProductKey);
        cJSON_AddItemToObject(request_body, "board", board);
        const std::string request_json = JsonToString(request_body);
        cJSON_Delete(request_body);
        const std::string pairing_url = origin + kAiotBindingRequestPath;
        HttpResponse response;
        if (!PerformHttpRequest(pairing_url, HTTP_METHOD_POST, request_json, {},
                                kMaxAiotResponseBytes, response, error)) {
            SetError(error);
            cJSON_Delete(bootstrap_root);
            return false;
        }
        if (response.status_code < 200 || response.status_code >= 300) {
            SetError("AIoT pairing request failed: HTTP status " +
                     std::to_string(response.status_code));
            cJSON_Delete(bootstrap_root);
            return false;
        }
        DevicePairingResponse pairing_response;
        if (!ParseDevicePairingResponse(response.body,
                                        DevicePairingResponseType::kCreateRequest,
                                        pairing_response, error)) {
            SetError("AIoT pairing request failed: " + error);
            cJSON_Delete(bootstrap_root);
            return false;
        }
        config.pairing_request_id = pairing_response.request_id;
        config.pairing_request_token = pairing_response.request_token;
        config.pairing_code = pairing_response.pairing_code;
        config.pairing_expires_at = pairing_response.expires_at;
        pairing_status = pairing_response.raw_status;
    } else {
        const std::string status_url = origin + kAiotBindingStatusPrefix +
                                       config.pairing_request_id + "/status";
        HttpResponse response;
        if (!PerformHttpRequest(status_url, HTTP_METHOD_GET, {}, config.pairing_request_token,
                                kMaxAiotResponseBytes, response, error)) {
            SetError(error);
            cJSON_Delete(bootstrap_root);
            return false;
        }
        const int business_code = ResponseBusinessCode(response);
        if (response.status_code == 401 || response.status_code == 404 ||
            response.status_code == 410 || business_code == 401 ||
            business_code == 404 || business_code == 410) {
            ResetPairingRequest(config);
            bool cleared = false;
            {
                std::lock_guard<std::recursive_mutex> lock(config_mutex_);
                cleared = PersistAiotIdentity(config);
            }
            SetError(cleared ? "配对申请已失效，请重新发起绑定"
                             : "配对申请已失效，但本地状态清理失败");
            cJSON_Delete(bootstrap_root);
            return false;
        }
        if (response.status_code < 200 || response.status_code >= 300) {
            SetError("AIoT pairing status failed: HTTP status " +
                     std::to_string(response.status_code));
            cJSON_Delete(bootstrap_root);
            return false;
        }
        DevicePairingResponse pairing_response;
        if (!ParseDevicePairingResponse(response.body,
                                        DevicePairingResponseType::kStatus,
                                        pairing_response, error)) {
            SetError("AIoT pairing status failed: " + error);
            cJSON_Delete(bootstrap_root);
            return false;
        }
        pairing_status = pairing_response.raw_status;
        if (!pairing_response.expires_at.empty()) {
            config.pairing_expires_at = pairing_response.expires_at;
        }
        cJSON* root = nullptr;
        cJSON* data = nullptr;
        if (!ParseResponse(response, root, data, error)) {
            SetError("AIoT pairing status failed: " + error);
            cJSON_Delete(bootstrap_root);
            return false;
        }
        token_root = root;
        token_data = data;
    }
    if (config.pairing_request_id.empty() || config.pairing_request_token.empty() ||
        config.pairing_code.empty()) {
        ResetPairingRequest(config);
        SetError("AIoT pairing response is incomplete");
        cJSON_Delete(token_root);
        cJSON_Delete(bootstrap_root);
        return false;
    }
    bool pairing_persisted = false;
    {
        std::lock_guard<std::recursive_mutex> lock(config_mutex_);
        config.pairing_status = Lowercase(pairing_status);
        config.has_pairing_request = !config.pairing_request_id.empty() &&
                                     !config.pairing_request_token.empty() &&
                                     !config.pairing_code.empty();
        if (config.has_pairing_request) {
            pairing_persisted = PersistAiotIdentity(config);
        }
    }
    if (!pairing_persisted) {
        SetError("Failed to persist pairing request");
        cJSON_Delete(token_root);
        cJSON_Delete(bootstrap_root);
        return false;
    }
    const DevicePairingStatus classified_status =
        ClassifyDevicePairingStatus(config.pairing_status);
    if (!MayEnableDeviceCloud(classified_status)) {
        if (ShouldResetDevicePairingRequest(classified_status)) {
            ResetPairingRequest(config);
            bool cleared = false;
            {
                std::lock_guard<std::recursive_mutex> lock(config_mutex_);
                cleared = PersistAiotIdentity(config);
            }
            SetError(!cleared ? "配对终态清理失败，请重试"
                              : (classified_status == DevicePairingStatus::kRejected
                                     ? "配对申请已拒绝，请重新发起绑定"
                                     : "配对申请已过期，请重新发起绑定"));
            cJSON_Delete(token_root);
            cJSON_Delete(bootstrap_root);
            return false;
        }
        SetError(config.pairing_code.empty()
                     ? "等待设备绑定确认"
                     : "等待设备绑定确认，配对码：" + config.pairing_code);
        cJSON_Delete(token_root);
        cJSON_Delete(bootstrap_root);
        return false;
    }
    }

    if (config.aiot_device_secret.empty()) {
        config.aiot_device_secret = GenerateDeviceSecret();
    }
    // Keep the confirmed candidate unusable until its token and MQTT settings
    // have committed together. A reset during this phase retries from NVS.
    config.aiot_pending = true;
    // Preserve the same secret across a reset so a retried status exchange can
    // still prove the identity represented by the confirmed request.
    bool secret_persisted = false;
    {
        std::lock_guard<std::recursive_mutex> lock(config_mutex_);
        secret_persisted = PersistAiotIdentity(config);
    }
    if (!secret_persisted) {
        SetError("Failed to persist AIoT device secret");
        cJSON_Delete(token_root);
        cJSON_Delete(bootstrap_root);
        return false;
    }
    // Never let a cached token win over the candidate returned by this exchange.
    config.aiot_access_token.clear();
    config.aiot_registered = false;
    config.aiot_activated = false;
    if (token_data == nullptr) {
        SetError("AIoT pairing confirmation did not include credentials");
        cJSON_Delete(token_root);
        cJSON_Delete(bootstrap_root);
        return false;
    }
    AddStringAlias(token_data, "deviceToken", config.aiot_access_token);
    AddStringAlias(token_data, "accessToken", config.aiot_access_token);
    AddStringAlias(token_data, "token", config.aiot_access_token);
    AddStringAlias(token_data, "mqttToken", config.aiot_access_token);

    const cJSON* mqtt_info = cJSON_GetObjectItemCaseSensitive(token_data, "unifiedMqtt");
    if (!cJSON_IsObject(mqtt_info)) {
        mqtt_info = cJSON_GetObjectItemCaseSensitive(token_data, "unified_mqtt");
    }
    if (!cJSON_IsObject(mqtt_info)) {
        mqtt_info = cJSON_GetObjectItemCaseSensitive(token_data, "mqttConnectInfo");
    }
    if (!cJSON_IsObject(mqtt_info)) {
        mqtt_info = cJSON_GetObjectItemCaseSensitive(token_data, "mqtt_connect_info");
    }
    if (!cJSON_IsObject(mqtt_info)) {
        mqtt_info = cJSON_GetObjectItemCaseSensitive(token_data, "mqtt");
    }
    if (!cJSON_IsObject(mqtt_info)) {
        mqtt_info = bootstrap_data;
    }
    cJSON* mqtt_object = const_cast<cJSON*>(mqtt_info);
    AddStringAlias(mqtt_object, "brokerAddress", mqtt_host);
    AddStringAlias(mqtt_object, "broker_address", mqtt_host);
    AddStringAlias(mqtt_object, "host", mqtt_host);
    AddStringAlias(mqtt_object, "address", mqtt_host);
    AddIntAlias(mqtt_object, "protocolVersion", mqtt_protocol);
    AddIntAlias(mqtt_object, "protocol_version", mqtt_protocol);
    AddIntAlias(mqtt_object, "brokerPort", mqtt_port);
    AddIntAlias(mqtt_object, "broker_port", mqtt_port);
    AddIntAlias(mqtt_object, "port", mqtt_port);
    AddStringAlias(mqtt_object, "username", mqtt_username);
    AddStringAlias(mqtt_object, "password", mqtt_password);
    AddStringAlias(mqtt_object, "httpBearerToken", mqtt_password);
    AddStringAlias(mqtt_object, "http_bearer_token", mqtt_password);
    AddStringAlias(mqtt_object, "deviceKey", mqtt_device_key);
    AddStringAlias(mqtt_object, "device_key", mqtt_device_key);
    AddStringAlias(mqtt_object, "httpBaseUrl", mqtt_http_base_url);
    AddStringAlias(mqtt_object, "http_base_url", mqtt_http_base_url);
    AddStringAlias(mqtt_object, "baseUrl", mqtt_http_base_url);
    AddIntAlias(mqtt_object, "keepalive", mqtt_keepalive);
    cJSON* home_enabled = cJSON_GetObjectItemCaseSensitive(mqtt_object, "homeEnabled");
    if (!cJSON_IsNumber(home_enabled)) {
        home_enabled = cJSON_GetObjectItemCaseSensitive(mqtt_object, "home_enabled");
    }
    if (cJSON_IsNumber(home_enabled)) {
        mqtt_home_enabled = home_enabled->valueint != 0;
    }
    const cJSON* broker = cJSON_GetObjectItemCaseSensitive(mqtt_object, "broker");
    if (cJSON_IsObject(broker)) {
        AddStringAlias(const_cast<cJSON*>(broker), "address", mqtt_host);
        AddStringAlias(const_cast<cJSON*>(broker), "host", mqtt_host);
        AddStringAlias(const_cast<cJSON*>(broker), "brokerAddress", mqtt_host);
        AddIntAlias(const_cast<cJSON*>(broker), "port", mqtt_port);
        AddIntAlias(const_cast<cJSON*>(broker), "brokerPort", mqtt_port);
    }
    const cJSON* topics = cJSON_GetObjectItemCaseSensitive(mqtt_object, "topics");
    if (!cJSON_IsObject(topics)) {
        topics = cJSON_GetObjectItemCaseSensitive(bootstrap_data, "topics");
    }
    cJSON* topic_object = const_cast<cJSON*>(topics);
    AddStringAlias(topic_object, "telemetry", telemetry_topic);
    AddStringAlias(topic_object, "shadowReport", shadow_report_topic);
    AddStringAlias(topic_object, "shadow_report", shadow_report_topic);
    AddStringAlias(topic_object, "shadowDesired", shadow_desired_topic);
    AddStringAlias(topic_object, "shadow_desired", shadow_desired_topic);
    AddStringAlias(topic_object, "otaNotify", ota_notify_topic);
    AddStringAlias(topic_object, "ota_notify", ota_notify_topic);
    AddStringAlias(topic_object, "otaProgress", ota_progress_topic);
    AddStringAlias(topic_object, "ota_progress", ota_progress_topic);
    AddStringAlias(topic_object, "commands", commands_topic);
    AddStringAlias(topic_object, "pcStatus", pc_status_topic);
    AddStringAlias(topic_object, "pc_status", pc_status_topic);
    AddStringAlias(topic_object, "homePrefix", home_prefix_topic);
    AddStringAlias(topic_object, "home_prefix", home_prefix_topic);
    AddStringAlias(token_data, "activationCode", activation_code);
    AddStringAlias(token_data, "activation_code", activation_code);
    AddStringAlias(token_data, "activationMessage", activation_message);
    AddStringAlias(token_data, "activation_message", activation_message);

    // A missing descriptor revokes the cached voice capability on a successful
    // token refresh, even when an older response advertised it.
    config.realtime_voice_url.clear();
    config.realtime_voice_protocol_version = kRodakRealtimeVoiceProtocolVersion;
    config.realtime_voice_downlink_sample_rate_hz = 24000;
    config.realtime_voice_downlink_frame_duration_ms = 60;
    config.realtime_voice_max_audio_frame_bytes = 8192;
    config.realtime_voice_max_control_bytes = 64 * 1024;
    config.realtime_voice_vad_strategies.clear();
    config.realtime_voice_preferred_vad_strategy = kRealtimeVoiceVadServerAuthoritative;
    config.has_realtime_voice_config = false;

    // The canonical token response may advertise a realtime voice capability.
    // It is optional during the rollout so an existing MQTT-only device can
    // refresh credentials before the voice endpoint is enabled server-side.
    const cJSON* realtime_voice = cJSON_GetObjectItemCaseSensitive(token_data, "realtimeVoice");
    if (!cJSON_IsObject(realtime_voice)) {
        realtime_voice = cJSON_GetObjectItemCaseSensitive(token_data, "realtime_voice");
    }
    if (!cJSON_IsObject(realtime_voice)) {
        realtime_voice = cJSON_GetObjectItemCaseSensitive(bootstrap_data, "realtimeVoice");
    }
    if (!cJSON_IsObject(realtime_voice)) {
        realtime_voice = cJSON_GetObjectItemCaseSensitive(bootstrap_data, "realtime_voice");
    }
    if (cJSON_IsObject(realtime_voice)) {
        RealtimeVoiceDescriptor voice_descriptor;
        std::string voice_error;
        if (!ParseRealtimeVoiceDescriptor(realtime_voice, voice_descriptor, voice_error)) {
            SetError("Realtime voice descriptor is invalid: " + voice_error);
            cJSON_Delete(token_root);
            cJSON_Delete(bootstrap_root);
            return false;
        }
        config.realtime_voice_url = voice_descriptor.endpoint;
        config.realtime_voice_protocol_version = voice_descriptor.protocol_version;
        config.realtime_voice_downlink_sample_rate_hz =
            voice_descriptor.downlink_sample_rate_hz;
        config.realtime_voice_downlink_frame_duration_ms =
            voice_descriptor.downlink_frame_duration_ms;
        config.realtime_voice_max_audio_frame_bytes =
            voice_descriptor.max_audio_frame_bytes;
        config.realtime_voice_max_control_bytes = voice_descriptor.max_control_bytes;
        config.realtime_voice_vad_strategies = voice_descriptor.vad_strategies;
        config.realtime_voice_preferred_vad_strategy =
            voice_descriptor.preferred_vad_strategy;
        config.has_realtime_voice_config = true;
    }
    cJSON_Delete(token_root);

    if (config.aiot_access_token.empty()) {
        SetError("AIoT token response did not contain an access token");
        cJSON_Delete(bootstrap_root);
        return false;
    }
    if (mqtt_host.empty()) {
        // The descriptor may omit the broker host when HTTP and MQTT share an
        // authority. Deriving it here keeps the device usable with a custom
        // HTTP port while the MQTT port remains separately advertised.
        mqtt_host = UrlHost(origin);
    }
    if (mqtt_device_key.empty()) {
        mqtt_device_key = device_key;
    }
    if (mqtt_username.empty()) {
        mqtt_username = mqtt_device_key;
    }
    if (mqtt_password.empty()) {
        mqtt_password = config.aiot_access_token;
    }
    if (mqtt_http_base_url.empty()) {
        mqtt_http_base_url = origin;
    }
    config.mqtt_protocol_version = mqtt_protocol > 0 ? mqtt_protocol : 2;
    config.mqtt_broker_address = mqtt_host;
    config.mqtt_broker_port = mqtt_port;
    config.mqtt_username = mqtt_username;
    config.mqtt_password = mqtt_password;
    config.mqtt_keepalive = mqtt_keepalive > 0 ? mqtt_keepalive : 240;
    config.mqtt_device_key = mqtt_device_key;
    config.mqtt_home_enabled = mqtt_home_enabled;
    config.mqtt_http_base_url = mqtt_http_base_url;
    config.mqtt_topic_telemetry = telemetry_topic;
    config.mqtt_topic_shadow_report = shadow_report_topic;
    config.mqtt_topic_shadow_desired = shadow_desired_topic;
    config.mqtt_topic_ota_notify = ota_notify_topic;
    config.mqtt_topic_ota_progress = ota_progress_topic;
    config.mqtt_topic_commands = commands_topic;
    config.mqtt_topic_pc_status = pc_status_topic;
    config.mqtt_topic_home_prefix = home_prefix_topic;
    config.activation_code = activation_code;
    config.activation_message = activation_message;
    config.has_activation_code = !activation_code.empty();
    config.aiot_registered = true;
    config.aiot_activated = true;
    ResetPairingRequest(config);
    FinalizeMqttConfig(config);
    config.has_aiot_config = HasCompleteAiotConfig(config);

    {
        std::lock_guard<std::recursive_mutex> lock(config_mutex_);
        if (config_generation != config_generation_) {
            SetError("AIoT provisioning endpoint changed while refresh was in progress");
            cJSON_Delete(bootstrap_root);
            return false;
        }
    }

    // Persist the autonomous credentials as one guarded transaction. The
    // credentials and MQTT connection parameters live in separate NVS
    // namespaces, so restore the complete prior snapshot if either commit
    // fails or the realtime voice descriptor cache cannot be cleared.
    bool credentials_persisted = false;
    bool generation_matches = false;
    bool rollback_ok = true;
    {
        std::lock_guard<std::recursive_mutex> lock(config_mutex_);
        generation_matches = config_generation == config_generation_;
        if (generation_matches) {
            auto previous_config = std::unique_ptr<DeviceCloudConfig>(
                new (std::nothrow) DeviceCloudConfig());
            if (previous_config == nullptr) {
                SetError("Not enough memory to snapshot AIoT credentials");
            } else {
                Load(*previous_config);

                // Mark the candidate incomplete until both namespaces have
                // committed and the pending marker is cleared below. A reset
                // between these writes is therefore rejected by Load().
                config.aiot_pending = true;
                const bool aiot_saved = PersistAiotIdentity(config);
                const bool mqtt_saved = aiot_saved && PersistMqttConfig(config);
                bool realtime_voice_saved = false;
                if (aiot_saved && mqtt_saved) {
                    config.aiot_pending = false;
                    realtime_voice_saved = PersistRealtimeVoiceConfig(config);
                    if (realtime_voice_saved) {
                        credentials_persisted = PersistAiotIdentity(config);
                        config.has_aiot_config = HasCompleteAiotConfig(config);
                    }
                }

                if (!credentials_persisted) {
                    const bool realtime_voice_restored =
                        PersistRealtimeVoiceConfig(*previous_config);
                    const bool aiot_restored = PersistAiotIdentity(*previous_config);
                    const bool mqtt_restored = PersistMqttConfig(*previous_config);
                    rollback_ok = realtime_voice_restored && aiot_restored && mqtt_restored;
                    if (!rollback_ok) {
                        ESP_LOGE(TAG, "Failed to restore cloud credentials after AIoT transaction failure");
                    }
                }
            }
        }
    }
    if (!credentials_persisted) {
        if (!generation_matches) {
            SetError("AIoT provisioning endpoint changed while refresh was in progress");
        } else if (!rollback_ok) {
            SetError("AIoT credentials state is uncertain after persistence failure");
        } else {
            SetError("Failed to persist AIoT credentials");
        }
        cJSON_Delete(bootstrap_root);
        return false;
    }
    cJSON_Delete(bootstrap_root);
    ESP_LOGI(TAG, "Rodak AIoT enrollment complete: device=%s broker=%s:%d",
             config.mqtt_device_key.c_str(), config.mqtt_broker_address.c_str(),
             config.mqtt_broker_port);
    return true;
}

bool DeviceCloudConfigService::Refresh(DeviceCloudConfig& config) {
    std::lock_guard<std::mutex> refresh_lock(refresh_mutex_);
    {
        std::lock_guard<std::recursive_mutex> config_lock(config_mutex_);
        Load(config);
    }
    if (config.provisioning_url.empty()) {
        config.provisioning_url = kDefaultProvisioningUrl;
    }
    if (!IsValidSerialProvisioningBootstrapUrl(config.provisioning_url) ||
        IsForbiddenLegacyProvisioningHost(config.provisioning_url)) {
        SetError("请配置有效的 Rodak AIoT 引导地址，不支持旧语音服务地址");
        return false;
    }
    // Canonical Rodak AIoT is the only onboarding path. A missing realtime
    // voice descriptor is a capability gap, not a reason to discard MQTT
    // credentials; the next token refresh can fill it in.
    return RefreshAiot(config) && config.has_aiot_config;
}

ProvisioningUrlSaveResult DeviceCloudConfigService::SaveProvisioningUrl(
    const std::string& url, ProvisioningUrlSaveMode mode) {
    std::lock_guard<std::recursive_mutex> lock(config_mutex_);
    const std::string requested_url = url.empty() ? kDefaultProvisioningUrl : url;
    std::string provisioning_url;
    if (!NormalizeSerialProvisioningBootstrapUrl(requested_url, provisioning_url) ||
        IsForbiddenLegacyProvisioningHost(provisioning_url)) {
        last_error_ = "不支持旧语音服务地址，请配置 Rodak AIoT 引导地址";
        return ProvisioningUrlSaveResult::kFailedRolledBack;
    }
    auto previous_config = std::unique_ptr<DeviceCloudConfig>(
        new (std::nothrow) DeviceCloudConfig());
    auto empty_config = std::unique_ptr<DeviceCloudConfig>(
        new (std::nothrow) DeviceCloudConfig());
    if (previous_config == nullptr || empty_config == nullptr) {
        last_error_ = "Not enough memory to snapshot device cloud config";
        return ProvisioningUrlSaveResult::kFailedRolledBack;
    }
    Load(*previous_config);
    std::string previous_url;
    if (mode == ProvisioningUrlSaveMode::kPreserveCredentials &&
        NormalizeSerialProvisioningBootstrapUrl(previous_config->provisioning_url,
                                                previous_url) &&
        previous_url == provisioning_url) {
        last_error_.clear();
        return ProvisioningUrlSaveResult::kUnchanged;
    }
    empty_config->realtime_voice_protocol_version = 1;
    ResetMqttConfig(*empty_config);
    empty_config->aiot_device_secret = previous_config->aiot_device_secret;
    ResetAiotCredentials(*empty_config);

    // An explicit provisioning request is a credential-rotation boundary.
    // Invalidate cached cloud credentials even when the URL is unchanged so
    // the next boot must fetch fresh MQTT/realtime voice credentials.

    Settings settings(kCloudNamespace, true);
    const bool url_saved = settings.SetString(kProvisioningUrlKey, provisioning_url) &&
                           settings.Commit();
    if (!url_saved) {
        (void)settings.Commit();
        Settings rollback_settings(kCloudNamespace, true);
        const bool url_restored =
            rollback_settings.SetString(kProvisioningUrlKey,
                                        previous_config->provisioning_url) &&
            rollback_settings.Commit();
        if (!url_restored) {
            ESP_LOGE(TAG, "Failed to restore provisioning URL after write failure");
        }
        last_error_ = "Failed to persist provisioning URL";
        return ClassifyProvisioningUrlSaveFailure(url_restored, true, true);
    }
    ++config_generation_;

    const bool realtime_voice_cleared = PersistRealtimeVoiceConfig(*empty_config);
    const bool aiot_cleared = realtime_voice_cleared && PersistAiotIdentity(*empty_config);
    const bool mqtt_cleared = aiot_cleared && PersistMqttConfig(*empty_config);
    if (!mqtt_cleared) {
        Settings rollback_settings(kCloudNamespace, true);
        const bool url_restored =
            rollback_settings.SetString(kProvisioningUrlKey,
                                        previous_config->provisioning_url) &&
            rollback_settings.Commit();
        const bool realtime_voice_restored = PersistRealtimeVoiceConfig(*previous_config);
        const bool aiot_restored = PersistAiotIdentity(*previous_config);
        const bool mqtt_restored = PersistMqttConfig(*previous_config);
        if (!url_restored || !realtime_voice_restored || !aiot_restored || !mqtt_restored) {
            ESP_LOGE(TAG, "Failed to restore device cloud config after credential invalidation error");
        }
        last_error_ = "Failed to invalidate cached device cloud credentials";
        return ClassifyProvisioningUrlSaveFailure(
            url_restored, realtime_voice_restored && aiot_restored, mqtt_restored);
    }

    last_error_.clear();
    return ProvisioningUrlSaveResult::kSaved;
}

bool DeviceCloudConfigService::Unbind(DeviceCloudConfig& config) {
    std::lock_guard<std::mutex> refresh_lock(refresh_mutex_);
    {
        std::lock_guard<std::recursive_mutex> lock(config_mutex_);
        Load(config);
    }
    const auto initial_action = GetDeviceUnbindRecoveryAction(
        config.unbind_pending, config.unbind_server_acknowledged,
        !config.aiot_access_token.empty());
    const std::string origin = UrlOrigin(config.provisioning_url);
    if (initial_action == DeviceUnbindRecoveryAction::kAlreadyClean) {
        SetError("设备尚未完成绑定");
        return false;
    }
    if (initial_action == DeviceUnbindRecoveryAction::kRequestServer) {
        HttpResponse response;
        std::string error;
        const bool request_ok = PerformHttpRequest(origin + kAiotUnbindPath, HTTP_METHOD_POST,
                                                   "{}", config.aiot_access_token,
                                                   kMaxAiotResponseBytes, response, error);
        if (!request_ok) {
            SetError(error);
            return false;
        }
        if (response.status_code < 200 || response.status_code >= 300) {
            SetError("设备解绑失败：HTTP status " +
                     std::to_string(response.status_code));
            return false;
        }
        cJSON* root = nullptr;
        cJSON* data = nullptr;
        if (response.body.empty() || !ParseResponse(response, root, data, error)) {
            SetError("设备解绑失败：" +
                     (error.empty() ? std::string("响应无效") : error));
            cJSON_Delete(root);
            return false;
        }
        const cJSON* code = cJSON_GetObjectItemCaseSensitive(root, "code");
        if (!cJSON_IsNumber(code) || code->valueint != 200) {
            SetError("设备解绑失败：服务端业务响应未成功");
            cJSON_Delete(root);
            return false;
        }
        const cJSON* status = cJSON_GetObjectItemCaseSensitive(data, "status");
        if (!cJSON_IsString(status) || status->valuestring == nullptr ||
            Lowercase(status->valuestring) != "unbound") {
            SetError("设备解绑失败：服务端未确认 unbound");
            cJSON_Delete(root);
            return false;
        }
        cJSON_Delete(root);
        config.unbind_server_acknowledged = true;
        config.unbind_pending = true;
        {
            std::lock_guard<std::recursive_mutex> lock(config_mutex_);
            if (!PersistAiotIdentity(config)) {
                SetError("设备解绑确认状态持久化失败");
                return false;
            }
        }
    }

    DeviceCloudConfig empty;
    empty.provisioning_url = config.provisioning_url;
    empty.realtime_voice_protocol_version = 1;
    ResetMqttConfig(empty);
    ResetAiotCredentials(empty);
    ResetPairingRequest(empty);
    // Make a reset during multi-namespace cleanup fail closed. Load() rejects
    // every cloud credential while this marker remains set.
    empty.aiot_pending = true;
    empty.unbind_pending = true;
    empty.unbind_server_acknowledged = true;
    const bool aiot_invalidated = PersistAiotIdentity(empty);
    const bool mqtt_cleared = aiot_invalidated && PersistMqttConfig(empty);
    const bool realtime_voice_cleared = mqtt_cleared && PersistRealtimeVoiceConfig(empty);
    bool cleanup_committed = false;
    if (realtime_voice_cleared) {
        empty.aiot_pending = false;
        empty.unbind_pending = false;
        empty.unbind_server_acknowledged = false;
        cleanup_committed = PersistAiotIdentity(empty);
    }
    if (!cleanup_committed) {
        SetError("设备解绑后清理本地凭据失败");
        config = empty;
        return false;
    }
    config = empty;
    last_error_.clear();
    return true;
}

std::string DeviceCloudConfigService::GetClientId() {
    std::lock_guard<std::recursive_mutex> lock(config_mutex_);
    Settings settings(kBoardNamespace, true);
    std::string uuid = settings.GetString(kUuidKey, "");
    if (uuid.empty()) {
        uuid = GenerateUuid();
        settings.SetString(kUuidKey, uuid);
    }
    return uuid;
}

std::string DeviceCloudConfigService::last_error() const {
    std::lock_guard<std::recursive_mutex> lock(config_mutex_);
    return last_error_;
}

void DeviceCloudConfigService::SetError(const std::string& message) {
    std::lock_guard<std::recursive_mutex> lock(config_mutex_);
    last_error_ = message;
    ESP_LOGW(TAG, "%s", last_error_.c_str());
}

}  // namespace rodakos

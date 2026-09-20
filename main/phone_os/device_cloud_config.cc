#include "phone_os/device_cloud_config.h"

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

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace rodakos {
namespace {
constexpr const char* TAG = "DeviceCloud";
constexpr const char* kCloudNamespace = "device_cloud";
constexpr const char* kLegacyVoiceNamespace = "voice_cloud";
constexpr const char* kLegacyXiaozhiNamespace = "xiaozhi";
constexpr const char* kWebsocketNamespace = "websocket";
constexpr const char* kMqttNamespace = "unified_mqtt";
constexpr const char* kBoardNamespace = "board";
constexpr const char* kProvisioningUrlKey = "prov_url";
constexpr const char* kLegacyOtaUrlKey = "ota_url";
constexpr const char* kUrlKey = "url";
constexpr const char* kTokenKey = "token";
constexpr const char* kVersionKey = "version";
constexpr const char* kUuidKey = "uuid";
constexpr const char* kAiotSecretKey = "device_secret";
constexpr const char* kAiotTokenKey = "access_token";
constexpr const char* kAiotRegisteredKey = "registered";
constexpr const char* kAiotActivatedKey = "activated";
constexpr const char* kAiotPendingKey = "pending";
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
constexpr const char* kDefaultProvisioningUrl = "https://api.tenclass.net/xiaozhi/ota/";
constexpr int kDefaultMqttBrokerPort = 1883;
constexpr int kDefaultMqttKeepalive = 240;
constexpr int kProvisioningTimeoutMs = 10000;
constexpr size_t kMaxProvisioningResponseBytes = 8192;
constexpr size_t kMaxAiotResponseBytes = 16384;

constexpr const char* kAiotBootstrapPath = "/api/v1/aiot/devices/bootstrap";
constexpr const char* kAiotRegisterPath = "/api/v1/aiot/devices/register";
constexpr const char* kAiotActivatePath = "/api/v1/aiot/devices/activate";
constexpr const char* kAiotTokenPath = "/api/v1/aiot/devices/auth/token";

struct HttpResponse {
    int status_code = 0;
    std::string body;
};

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
    const std::string lower = Lowercase(url);
    return lower.find("/api/v1/aiot/devices/bootstrap") != std::string::npos;
}

bool IsLegacyExternalUrl(const std::string& url) {
    const std::string lower = Lowercase(url);
    return lower.find("tenclass.net") != std::string::npos;
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

std::string ResolveLegacyProvisioningUrl(const std::string& configured_url) {
    if (configured_url.empty()) {
        return {};
    }
    const std::string lower = Lowercase(configured_url);
    if (lower.find("/xiaozhi/ota/") != std::string::npos) {
        return configured_url;
    }
    return ResolveApiUrl(configured_url, "/xiaozhi/ota/");
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

void AddStringIfPresent(cJSON* root, const char* key, std::string& output) {
    cJSON* item = cJSON_GetObjectItem(root, key);
    if (cJSON_IsString(item) && item->valuestring != nullptr) {
        output = item->valuestring;
    }
}

void AddIntIfPresent(cJSON* root, const char* key, int& output) {
    cJSON* item = cJSON_GetObjectItem(root, key);
    if (cJSON_IsNumber(item)) {
        output = item->valueint;
    }
}

void AddBoolIfPresent(cJSON* root, const char* key, bool& output) {
    cJSON* item = cJSON_GetObjectItem(root, key);
    if (cJSON_IsBool(item)) {
        output = cJSON_IsTrue(item);
    } else if (cJSON_IsNumber(item)) {
        output = item->valueint != 0;
    }
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
                         settings.SetBool(kAiotPendingKey, config.aiot_pending);
    if (!written || !settings.Commit()) {
        (void)settings.Commit();
        return false;
    }
    return true;
}

bool PersistWebsocketConfig(const DeviceCloudConfig& config) {
    Settings settings(kWebsocketNamespace, true);
    const bool written = settings.SetString(kUrlKey, config.websocket_url) &&
                         settings.SetString(kTokenKey, config.websocket_token) &&
                         settings.SetInt(kVersionKey, config.websocket_version);
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
    config.provisioning_url = cloud_settings.GetString(kProvisioningUrlKey, "");
    bool should_migrate_provisioning_url = false;
    if (config.provisioning_url.empty()) {
        Settings legacy_voice_settings(kLegacyVoiceNamespace, false);
        config.provisioning_url = legacy_voice_settings.GetString(kLegacyOtaUrlKey, "");
        should_migrate_provisioning_url = !config.provisioning_url.empty();
    }
    if (config.provisioning_url.empty()) {
        Settings legacy_settings(kLegacyXiaozhiNamespace, false);
        const std::string legacy_url = legacy_settings.GetString(kLegacyOtaUrlKey, "");
        if (!legacy_url.empty()) {
            config.provisioning_url = legacy_url;
            should_migrate_provisioning_url = true;
        }
    }
    if (config.provisioning_url.empty()) {
        config.provisioning_url = kDefaultProvisioningUrl;
    }
    if (should_migrate_provisioning_url) {
        Settings write_settings(kCloudNamespace, true);
        write_settings.SetString(kProvisioningUrlKey, config.provisioning_url);
    }

    Settings ws_settings(kWebsocketNamespace, false);
    config.websocket_url = ws_settings.GetString(kUrlKey, "");
    config.websocket_token = ws_settings.GetString(kTokenKey, "");
    config.websocket_version = ws_settings.GetInt(kVersionKey, 1);
    if (config.websocket_version <= 0) {
        config.websocket_version = 1;
    }
    config.has_websocket_config = !config.websocket_url.empty();

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
    }
    config.has_aiot_config = HasCompleteAiotConfig(config);
    return config.has_websocket_config || config.has_aiot_config;
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
    std::string register_path = kAiotRegisterPath;
    std::string activate_path = kAiotActivatePath;
    std::string token_path = kAiotTokenPath;
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

    auto readEndpoint = [&](const char* key, std::string& output, const char* fallback) {
        const cJSON* endpoints = cJSON_GetObjectItemCaseSensitive(bootstrap_data, "endpoints");
        if (cJSON_IsObject(endpoints)) {
            AddStringAlias(const_cast<cJSON*>(endpoints), key, output);
        }
        if (output.empty()) {
            output = fallback;
        }
    };
    readEndpoint("register", register_path, kAiotRegisterPath);
    readEndpoint("activate", activate_path, kAiotActivatePath);
    readEndpoint("token", token_path, kAiotTokenPath);
    // Some deployments use explicit names for the token endpoint.
    if (token_path == kAiotTokenPath) {
        readEndpoint("authToken", token_path, kAiotTokenPath);
        readEndpoint("auth_token", token_path, kAiotTokenPath);
    }

    auto resolveEndpoint = [&](const std::string& endpoint) {
        if (endpoint.rfind("http://", 0) == 0 || endpoint.rfind("https://", 0) == 0) {
            // Bootstrap is unauthenticated and must not be able to redirect
            // the device secret to an unrelated host. Custom endpoints may
            // still be used, but only on the exact bootstrap origin.
            return Lowercase(UrlOrigin(endpoint)) == Lowercase(origin) ? endpoint : std::string();
        }
        if (endpoint.empty() || endpoint.rfind("//", 0) == 0) {
            return std::string();
        }
        if (endpoint.front() != '/') {
            return origin + "/" + endpoint;
        }
        return origin + endpoint;
    };

    const std::string register_url = resolveEndpoint(register_path);
    const std::string activate_url = resolveEndpoint(activate_path);
    const std::string token_url = resolveEndpoint(token_path);
    if (register_url.empty() || activate_url.empty() || token_url.empty()) {
        SetError("AIoT bootstrap returned an endpoint outside its origin");
        cJSON_Delete(bootstrap_root);
        return false;
    }

    if (config.aiot_device_secret.empty()) {
        config.aiot_device_secret = GenerateDeviceSecret();
    }
    // Keep the cached identity unusable until the full register/activate/token
    // exchange and the paired MQTT commit complete. This also makes a reset
    // during the network phase safe: the next boot retries enrollment.
    config.aiot_pending = true;
    // Persist a generated secret before the first network call. If power is
    // lost after register succeeds, the retry must use the same credential;
    // otherwise the server correctly rejects the second secret as a conflict.
    bool secret_persisted = false;
    {
        std::lock_guard<std::recursive_mutex> lock(config_mutex_);
        secret_persisted = PersistAiotIdentity(config);
    }
    if (!secret_persisted) {
        SetError("Failed to persist AIoT device secret");
        cJSON_Delete(bootstrap_root);
        return false;
    }
    // Never let a cached token win over the token returned by this enrollment.
    // Keep the old value in NVS until the new exchange has completed so a
    // transient network failure can continue using the active MQTT session.
    config.aiot_access_token.clear();
    config.aiot_registered = false;
    config.aiot_activated = false;
    const std::string device_key = MacAddress();
    const std::string client_id = GetClientId();
    const esp_app_desc_t* app_desc = esp_app_get_description();
    const std::string firmware_version = app_desc != nullptr ? app_desc->version : "unknown";

    cJSON* register_body = cJSON_CreateObject();
    cJSON_AddStringToObject(register_body, "protocol", kRodakAiotProtocol);
    cJSON_AddNumberToObject(register_body, "protocolVersion", kRodakAiotProtocolVersion);
    cJSON_AddNumberToObject(register_body, "protocol_version", kRodakAiotProtocolVersion);
    cJSON_AddStringToObject(register_body, "productKey", kRodakBigSmartProductKey);
    cJSON_AddStringToObject(register_body, "product_key", kRodakBigSmartProductKey);
    cJSON_AddStringToObject(register_body, "deviceCode", device_key.c_str());
    cJSON_AddStringToObject(register_body, "deviceKey", device_key.c_str());
    cJSON_AddStringToObject(register_body, "deviceName", "RodakOS RYMCU BigSmart");
    cJSON_AddStringToObject(register_body, "credentialKey", kRodakBigSmartProductKey);
    cJSON_AddStringToObject(register_body, "credentialSecret", config.aiot_device_secret.c_str());
    // Existing XiaoZhi-era records derive their first secret from these stable
    // values. Sending the proof lets the server perform a safe one-time
    // legacy -> Rodak migration before replacing it with the random secret.
    const std::string legacy_secret =
        std::string("xiaozhi-chatbot:") + device_key + ":" + client_id;
    cJSON_AddStringToObject(register_body, "legacyProductKey",
                            "xiaozhi-rymcu-bigsmart");
    cJSON_AddStringToObject(register_body, "legacyCredentialSecret", legacy_secret.c_str());
    cJSON_AddStringToObject(register_body, "clientId", client_id.c_str());
    cJSON_AddStringToObject(register_body, "firmwareVersion", firmware_version.c_str());
    cJSON_AddStringToObject(register_body, "hardwareVersion", "rymcu_bigsmart");

    cJSON* application = cJSON_CreateObject();
    cJSON_AddStringToObject(application, "name", "rodakos");
    cJSON_AddStringToObject(application, "version", firmware_version.c_str());
    cJSON_AddItemToObject(register_body, "application", application);
    cJSON* board = cJSON_CreateObject();
    cJSON_AddStringToObject(board, "type", "rymcu_bigsmart");
    cJSON_AddStringToObject(board, "name", "RodakOS RYMCU BigSmart");
    cJSON_AddStringToObject(board, "product_key", kRodakBigSmartProductKey);
    cJSON_AddStringToObject(board, "protocol", kRodakAiotProtocol);
    cJSON_AddItemToObject(register_body, "board", board);
    cJSON* board_payload = cJSON_CreateObject();
    cJSON_AddItemToObject(board_payload, "application", cJSON_Duplicate(application, true));
    cJSON_AddItemToObject(board_payload, "board", cJSON_Duplicate(board, true));
    cJSON_AddStringToObject(board_payload, "product_key", kRodakBigSmartProductKey);
    cJSON_AddStringToObject(board_payload, "protocol", kRodakAiotProtocol);
    cJSON_AddItemToObject(register_body, "boardPayload", board_payload);
    const std::string register_json = JsonToString(register_body);
    cJSON_Delete(register_body);

    HttpResponse register_response;
    if (!PerformHttpRequest(register_url, HTTP_METHOD_POST, register_json, {},
                            kMaxAiotResponseBytes, register_response, error)) {
        SetError(error);
        cJSON_Delete(bootstrap_root);
        return false;
    }
    cJSON* register_root = nullptr;
    cJSON* register_data = nullptr;
    if (!ParseResponse(register_response, register_root, register_data, error)) {
        SetError("AIoT register failed: " + error);
        cJSON_Delete(bootstrap_root);
        return false;
    }
    AddStringAlias(register_data, "activationCode", activation_code);
    AddStringAlias(register_data, "activation_code", activation_code);
    AddStringAlias(register_data, "activationMessage", activation_message);
    AddStringAlias(register_data, "activation_message", activation_message);
    cJSON_Delete(register_root);

    cJSON* activate_body = cJSON_CreateObject();
    cJSON_AddStringToObject(activate_body, "protocol", kRodakAiotProtocol);
    cJSON_AddStringToObject(activate_body, "productKey", kRodakBigSmartProductKey);
    cJSON_AddStringToObject(activate_body, "product_key", kRodakBigSmartProductKey);
    cJSON_AddStringToObject(activate_body, "deviceCode", device_key.c_str());
    cJSON_AddStringToObject(activate_body, "deviceKey", device_key.c_str());
    cJSON_AddStringToObject(activate_body, "credentialKey", kRodakBigSmartProductKey);
    cJSON_AddStringToObject(activate_body, "credentialSecret", config.aiot_device_secret.c_str());
    if (!activation_code.empty()) {
        cJSON_AddStringToObject(activate_body, "activationCode", activation_code.c_str());
    }
    const std::string activate_json = JsonToString(activate_body);
    cJSON_Delete(activate_body);

    HttpResponse activate_response;
    if (!PerformHttpRequest(activate_url, HTTP_METHOD_POST, activate_json, {},
                            kMaxAiotResponseBytes, activate_response, error)) {
        SetError(error);
        cJSON_Delete(bootstrap_root);
        return false;
    }
    cJSON* activate_root = nullptr;
    cJSON* activate_data = nullptr;
    if (!ParseResponse(activate_response, activate_root, activate_data, error)) {
        SetError("AIoT activate failed: " + error);
        cJSON_Delete(bootstrap_root);
        return false;
    }
    AddStringAlias(activate_data, "activationCode", activation_code);
    AddStringAlias(activate_data, "activation_code", activation_code);
    AddStringAlias(activate_data, "activationMessage", activation_message);
    AddStringAlias(activate_data, "activation_message", activation_message);
    cJSON_Delete(activate_root);

    cJSON* token_body = cJSON_CreateObject();
    cJSON_AddStringToObject(token_body, "protocol", kRodakAiotProtocol);
    cJSON_AddStringToObject(token_body, "productKey", kRodakBigSmartProductKey);
    cJSON_AddStringToObject(token_body, "product_key", kRodakBigSmartProductKey);
    cJSON_AddStringToObject(token_body, "deviceCode", device_key.c_str());
    cJSON_AddStringToObject(token_body, "deviceKey", device_key.c_str());
    cJSON_AddStringToObject(token_body, "credentialKey", kRodakBigSmartProductKey);
    cJSON_AddStringToObject(token_body, "credentialSecret", config.aiot_device_secret.c_str());
    const std::string token_json = JsonToString(token_body);
    cJSON_Delete(token_body);

    HttpResponse token_response;
    if (!PerformHttpRequest(token_url, HTTP_METHOD_POST, token_json, {},
                            kMaxAiotResponseBytes, token_response, error)) {
        SetError(error);
        cJSON_Delete(bootstrap_root);
        return false;
    }
    cJSON* token_root = nullptr;
    cJSON* token_data = nullptr;
    if (!ParseResponse(token_response, token_root, token_data, error)) {
        SetError("AIoT token request failed: " + error);
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
    // fails or the legacy websocket cache cannot be cleared.
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
                bool websocket_saved = false;
                if (aiot_saved && mqtt_saved) {
                    config.aiot_pending = false;
                    websocket_saved = PersistWebsocketConfig(DeviceCloudConfig{});
                    if (websocket_saved) {
                        credentials_persisted = PersistAiotIdentity(config);
                        config.has_aiot_config = HasCompleteAiotConfig(config);
                    }
                }

                if (!credentials_persisted) {
                    const bool websocket_restored = PersistWebsocketConfig(*previous_config);
                    const bool aiot_restored = PersistAiotIdentity(*previous_config);
                    const bool mqtt_restored = PersistMqttConfig(*previous_config);
                    rollback_ok = websocket_restored && aiot_restored && mqtt_restored;
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
    uint32_t config_generation = 0;
    {
        std::lock_guard<std::recursive_mutex> config_lock(config_mutex_);
        Load(config);
        config_generation = config_generation_;
    }
    if (config.provisioning_url.empty()) {
        config.provisioning_url = kDefaultProvisioningUrl;
    }

    // A Rodak AIoT bootstrap is an explicit GET descriptor followed by the
    // register -> activate -> token exchange. Local deployments may still
    // have `/xiaozhi/ota/` saved from the legacy firmware; derive the
    // autonomous endpoint from its origin and only fall back when that route
    // is genuinely unavailable. The external Tenclass endpoint remains a
    // legacy-only compatibility path.
    if (!IsLegacyExternalUrl(config.provisioning_url)) {
        const bool explicit_aiot = IsExplicitAiotUrl(config.provisioning_url);
        if (RefreshAiot(config) || explicit_aiot) {
            return config.has_aiot_config;
        }
        const std::string aiot_error = last_error();
        if (aiot_error.find("HTTP status 404") == std::string::npos &&
            aiot_error.find("HTTP status 405") == std::string::npos) {
            return false;
        }
        ESP_LOGW(TAG, "AIoT endpoint unavailable; trying legacy bootstrap compatibility path");
    }

    const std::string legacy_provisioning_url =
        ResolveLegacyProvisioningUrl(config.provisioning_url);
    if (legacy_provisioning_url.empty()) {
        SetError("Legacy provisioning URL has no valid origin");
        return false;
    }
    const std::string payload = BuildSystemInfoJson();
    esp_http_client_config_t http_config = {};
    http_config.url = legacy_provisioning_url.c_str();
    http_config.method = HTTP_METHOD_POST;
    http_config.timeout_ms = kProvisioningTimeoutMs;
    http_config.buffer_size = 1024;
    http_config.buffer_size_tx = 1024;
    http_config.crt_bundle_attach = esp_crt_bundle_attach;
    http_config.user_agent = "RodakOS/device-cloud";

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) {
        SetError("Failed to create cloud provisioning client");
        return false;
    }

    const std::string client_id = GetClientId();
    const std::string mac = MacAddress();
    // Keep the established bootstrap header for existing configured endpoints;
    // the Rodak identity is carried independently in the metadata below.
    esp_http_client_set_header(client, "Activation-Version", "1");
    esp_http_client_set_header(client, "Device-Id", mac.c_str());
    esp_http_client_set_header(client, "Client-Id", client_id.c_str());
    esp_http_client_set_header(client, "Rodak-Protocol", kRodakAiotProtocol);
    const std::string protocol_version = std::to_string(kRodakAiotProtocolVersion);
    esp_http_client_set_header(client, "Rodak-Protocol-Version", protocol_version.c_str());
    esp_http_client_set_header(client, "Rodak-Product-Key", kRodakBigSmartProductKey);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    ESP_LOGI(TAG, "Refreshing legacy device cloud config from %s", legacy_provisioning_url.c_str());
    if (payload.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        SetError("Provisioning request payload is too large");
        esp_http_client_cleanup(client);
        return false;
    }

    esp_err_t err = esp_http_client_open(client, payload.size());
    if (err != ESP_OK) {
        SetError(std::string("Provisioning open failed: ") + esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    const int write_len = esp_http_client_write(client, payload.c_str(), payload.size());
    if (write_len != static_cast<int>(payload.size())) {
        SetError(write_len < 0 ? "Provisioning request write failed" : "Provisioning request write incomplete");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    const int64_t content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        SetError("Provisioning response header fetch failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    const int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        SetError("Provisioning request returned HTTP " + std::to_string(status_code));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    std::vector<char> response(kMaxProvisioningResponseBytes + 1, '\0');
    const int read_len = esp_http_client_read_response(client, response.data(), kMaxProvisioningResponseBytes);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (read_len <= 0) {
        SetError("Provisioning response is empty");
        return false;
    }
    response[std::min(static_cast<size_t>(read_len), kMaxProvisioningResponseBytes)] = '\0';

    if (!ParseProvisioningResponse(std::string(response.data(), read_len), config)) {
        return false;
    }
    {
        std::lock_guard<std::recursive_mutex> config_lock(config_mutex_);
        if (config_generation != config_generation_) {
            last_error_ = "Provisioning endpoint changed while refresh was in progress";
            return false;
        }
        // DeviceCloudConfig contains many strings. Keep rollback snapshots off
        // the small ESP-IDF service stacks while NVS persistence is nested.
        auto previous_config = std::unique_ptr<DeviceCloudConfig>(
            new (std::nothrow) DeviceCloudConfig());
        auto empty_config = std::unique_ptr<DeviceCloudConfig>(
            new (std::nothrow) DeviceCloudConfig());
        if (previous_config == nullptr || empty_config == nullptr) {
            last_error_ = "Not enough memory to snapshot device cloud config";
            return false;
        }
        Load(*previous_config);
        empty_config->websocket_version = 1;
        ResetMqttConfig(*empty_config);
        bool save_ok = config.has_websocket_config
                           ? PersistWebsocketConfig(config)
                           : PersistWebsocketConfig(*empty_config);
        save_ok = PersistAiotIdentity(config) && save_ok;
        if (config.has_mqtt_config) {
            save_ok = PersistMqttConfig(config) && save_ok;
        } else {
            save_ok = PersistMqttConfig(*empty_config) && save_ok;
        }
        if (!save_ok) {
            const bool websocket_restored = PersistWebsocketConfig(*previous_config);
            const bool aiot_restored = PersistAiotIdentity(*previous_config);
            const bool mqtt_restored = PersistMqttConfig(*previous_config);
            if (!websocket_restored || !aiot_restored || !mqtt_restored) {
                ESP_LOGE(TAG, "Failed to restore cloud config after persistence error");
            }
            last_error_ = "Failed to persist device cloud credentials";
            return false;
        }
    }
    return config.has_websocket_config;
}

ProvisioningUrlSaveResult DeviceCloudConfigService::SaveProvisioningUrl(
    const std::string& url) {
    std::lock_guard<std::recursive_mutex> lock(config_mutex_);
    const std::string provisioning_url = url.empty() ? kDefaultProvisioningUrl : url;
    auto previous_config = std::unique_ptr<DeviceCloudConfig>(
        new (std::nothrow) DeviceCloudConfig());
    auto empty_config = std::unique_ptr<DeviceCloudConfig>(
        new (std::nothrow) DeviceCloudConfig());
    if (previous_config == nullptr || empty_config == nullptr) {
        last_error_ = "Not enough memory to snapshot device cloud config";
        return ProvisioningUrlSaveResult::kFailedRolledBack;
    }
    Load(*previous_config);
    empty_config->websocket_version = 1;
    ResetMqttConfig(*empty_config);
    empty_config->aiot_device_secret = previous_config->aiot_device_secret;
    ResetAiotCredentials(*empty_config);

    // An explicit provisioning request is a credential-rotation boundary.
    // Invalidate cached cloud credentials even when the URL is unchanged so
    // the next boot must fetch fresh MQTT/WebSocket credentials.

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

    const bool websocket_cleared = PersistWebsocketConfig(*empty_config);
    const bool aiot_cleared = websocket_cleared && PersistAiotIdentity(*empty_config);
    const bool mqtt_cleared = aiot_cleared && PersistMqttConfig(*empty_config);
    if (!mqtt_cleared) {
        Settings rollback_settings(kCloudNamespace, true);
        const bool url_restored =
            rollback_settings.SetString(kProvisioningUrlKey,
                                        previous_config->provisioning_url) &&
            rollback_settings.Commit();
        const bool websocket_restored = PersistWebsocketConfig(*previous_config);
        const bool aiot_restored = PersistAiotIdentity(*previous_config);
        const bool mqtt_restored = PersistMqttConfig(*previous_config);
        if (!url_restored || !websocket_restored || !aiot_restored || !mqtt_restored) {
            ESP_LOGE(TAG, "Failed to restore device cloud config after credential invalidation error");
        }
        last_error_ = "Failed to invalidate cached device cloud credentials";
        return ClassifyProvisioningUrlSaveFailure(
            url_restored, websocket_restored && aiot_restored, mqtt_restored);
    }

    last_error_.clear();
    return ProvisioningUrlSaveResult::kSaved;
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

bool DeviceCloudConfigService::ParseProvisioningResponse(const std::string& response,
                                                         DeviceCloudConfig& config) {
    cJSON* root = cJSON_Parse(response.c_str());
    if (root == nullptr) {
        SetError("Provisioning response is not JSON");
        return false;
    }

    config.websocket_url.clear();
    config.websocket_token.clear();
    config.websocket_version = 1;
    config.activation_code.clear();
    config.activation_message.clear();
    config.has_websocket_config = false;
    ResetMqttConfig(config);
    ResetAiotCredentials(config);
    config.has_activation_code = false;

    cJSON* websocket = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(websocket)) {
        AddStringIfPresent(websocket, kUrlKey, config.websocket_url);
        AddStringIfPresent(websocket, kTokenKey, config.websocket_token);
        AddIntIfPresent(websocket, kVersionKey, config.websocket_version);
        config.has_websocket_config = !config.websocket_url.empty();
    }

    cJSON* unified_mqtt = cJSON_GetObjectItem(root, "unifiedMqtt");
    if (!cJSON_IsObject(unified_mqtt)) {
        unified_mqtt = cJSON_GetObjectItem(root, "unified_mqtt");
    }
    if (cJSON_IsObject(unified_mqtt)) {
        AddIntIfPresent(unified_mqtt, "protocol_version", config.mqtt_protocol_version);
        AddStringIfPresent(unified_mqtt, "broker_address", config.mqtt_broker_address);
        AddIntIfPresent(unified_mqtt, "broker_port", config.mqtt_broker_port);
        AddStringIfPresent(unified_mqtt, "username", config.mqtt_username);
        AddStringIfPresent(unified_mqtt, "password", config.mqtt_password);
        AddIntIfPresent(unified_mqtt, "keepalive", config.mqtt_keepalive);
        AddStringIfPresent(unified_mqtt, "device_key", config.mqtt_device_key);
        AddBoolIfPresent(unified_mqtt, "home_enabled", config.mqtt_home_enabled);
        AddStringIfPresent(unified_mqtt, "http_base_url", config.mqtt_http_base_url);

        cJSON* topics = cJSON_GetObjectItem(unified_mqtt, "topics");
        if (cJSON_IsObject(topics)) {
            AddStringIfPresent(topics, "telemetry", config.mqtt_topic_telemetry);
            AddStringIfPresent(topics, "shadow_report", config.mqtt_topic_shadow_report);
            AddStringIfPresent(topics, "shadow_desired", config.mqtt_topic_shadow_desired);
            AddStringIfPresent(topics, "ota_notify", config.mqtt_topic_ota_notify);
            AddStringIfPresent(topics, "ota_progress", config.mqtt_topic_ota_progress);
            AddStringIfPresent(topics, "commands", config.mqtt_topic_commands);
            AddStringIfPresent(topics, "pc_status", config.mqtt_topic_pc_status);
            AddStringIfPresent(topics, "home_prefix", config.mqtt_topic_home_prefix);
        }
        FinalizeMqttConfig(config);
    }

    cJSON* activation = cJSON_GetObjectItem(root, "activation");
    if (cJSON_IsObject(activation)) {
        AddStringIfPresent(activation, "code", config.activation_code);
        AddStringIfPresent(activation, "message", config.activation_message);
        config.has_activation_code = !config.activation_code.empty();
    }

    cJSON_Delete(root);
    if (!config.has_websocket_config) {
        SetError(config.has_activation_code ? "Activate the device in the cloud console" : "No websocket config from device cloud");
        return false;
    }
    if (config.websocket_version <= 0) {
        config.websocket_version = 1;
    }
    {
        std::lock_guard<std::recursive_mutex> lock(config_mutex_);
        last_error_.clear();
    }
    ESP_LOGI(TAG, "Device cloud websocket config ready: version=%d url=%s",
             config.websocket_version, config.websocket_url.c_str());
    if (config.has_mqtt_config) {
        ESP_LOGI(TAG, "Device cloud MQTT config ready: protocol=%d broker=%s:%d",
                 config.mqtt_protocol_version, config.mqtt_broker_address.c_str(),
                 config.mqtt_broker_port);
    }
    return true;
}

std::string DeviceCloudConfigService::BuildSystemInfoJson() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddStringToObject(root, "protocol", kRodakAiotProtocol);
    cJSON_AddNumberToObject(root, "protocol_version", kRodakAiotProtocolVersion);
    cJSON_AddStringToObject(root, "product_key", kRodakBigSmartProductKey);
    cJSON_AddStringToObject(root, "language", "zh-CN");

    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);
    cJSON_AddNumberToObject(root, "flash_size", flash_size);
    cJSON_AddNumberToObject(root, "minimum_free_heap_size", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(root, "psram_size", heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    cJSON_AddStringToObject(root, "mac_address", MacAddress().c_str());
    cJSON_AddStringToObject(root, "uuid", GetClientId().c_str());
    cJSON_AddStringToObject(root, "chip_model_name", CONFIG_IDF_TARGET);

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    cJSON* chip = cJSON_CreateObject();
    cJSON_AddNumberToObject(chip, "model", chip_info.model);
    cJSON_AddNumberToObject(chip, "cores", chip_info.cores);
    cJSON_AddNumberToObject(chip, "revision", chip_info.revision);
    cJSON_AddNumberToObject(chip, "features", chip_info.features);
    cJSON_AddItemToObject(root, "chip_info", chip);

    const esp_app_desc_t* app_desc = esp_app_get_description();
    cJSON* app = cJSON_CreateObject();
    cJSON_AddStringToObject(app, "name", app_desc->project_name);
    cJSON_AddStringToObject(app, "version", app_desc->version);
    cJSON_AddStringToObject(app, "idf_version", app_desc->idf_ver);
    cJSON_AddItemToObject(root, "application", app);

    const esp_partition_t* ota_partition = esp_ota_get_running_partition();
    cJSON* ota = cJSON_CreateObject();
    cJSON_AddStringToObject(ota, "label", ota_partition != nullptr ? ota_partition->label : "factory");
    cJSON_AddItemToObject(root, "ota", ota);
    const std::string board_json = BuildBoardJson();
    cJSON_AddRawToObject(root, "board", board_json.c_str());

    std::string json = JsonToString(root);
    cJSON_Delete(root);
    return json;
}

std::string DeviceCloudConfigService::BuildBoardJson() {
    cJSON* board = cJSON_CreateObject();
    // `type` is still the Board Manager hardware discriminator. The cloud
    // product identity is explicit instead of being derived from that name.
    cJSON_AddStringToObject(board, "type", "rymcu_bigsmart");
    cJSON_AddStringToObject(board, "product_key", kRodakBigSmartProductKey);
    cJSON_AddStringToObject(board, "protocol", kRodakAiotProtocol);
    cJSON_AddStringToObject(board, "board_manager_type", "rymcu_bigsmart");
    cJSON_AddStringToObject(board, "name", "RodakOS RYMCU BigSmart");
    cJSON_AddStringToObject(board, "mac", MacAddress().c_str());
    std::string json = JsonToString(board);
    cJSON_Delete(board);
    return json;
}

void DeviceCloudConfigService::SetError(const std::string& message) {
    std::lock_guard<std::recursive_mutex> lock(config_mutex_);
    last_error_ = message;
    ESP_LOGW(TAG, "%s", last_error_.c_str());
}

}  // namespace rodakos

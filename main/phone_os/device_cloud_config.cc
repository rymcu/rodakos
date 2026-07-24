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
#include <cstdio>
#include <cstring>
#include <limits>
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
constexpr const char* kMqttPcStatusTopicKey = "pc_status";
constexpr const char* kMqttHomePrefixTopicKey = "home_prefix";
constexpr const char* kDefaultProvisioningUrl = "https://api.tenclass.net/xiaozhi/ota/";
constexpr int kDefaultMqttBrokerPort = 1883;
constexpr int kDefaultMqttKeepalive = 240;
constexpr int kProvisioningTimeoutMs = 10000;
constexpr size_t kMaxProvisioningResponseBytes = 8192;

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
    config.mqtt_topic_pc_status.clear();
    config.mqtt_topic_home_prefix.clear();
    config.has_mqtt_config = false;
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
    config.mqtt_topic_pc_status = mqtt_settings.GetString(kMqttPcStatusTopicKey, "");
    config.mqtt_topic_home_prefix = mqtt_settings.GetString(kMqttHomePrefixTopicKey, "");
    FinalizeMqttConfig(config);
    return config.has_websocket_config;
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

    const std::string payload = BuildSystemInfoJson();
    esp_http_client_config_t http_config = {};
    http_config.url = config.provisioning_url.c_str();
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
    esp_http_client_set_header(client, "Activation-Version", "1");
    esp_http_client_set_header(client, "Device-Id", mac.c_str());
    esp_http_client_set_header(client, "Client-Id", client_id.c_str());
    esp_http_client_set_header(client, "Content-Type", "application/json");
    ESP_LOGI(TAG, "Refreshing device cloud config from %s", config.provisioning_url.c_str());
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
        std::lock_guard<std::recursive_mutex> config_lock(config_mutex_);
        if (config_generation == config_generation_) {
            ClearWebsocketConfig();
            ClearMqttConfig();
        }
        return false;
    }
    {
        std::lock_guard<std::recursive_mutex> config_lock(config_mutex_);
        if (config_generation != config_generation_) {
            last_error_ = "Provisioning endpoint changed while refresh was in progress";
            return false;
        }
        if (config.has_websocket_config) {
            SaveWebsocketConfig(config);
        }
        if (config.has_mqtt_config) {
            SaveMqttConfig(config);
        } else {
            ClearMqttConfig();
        }
    }
    return config.has_websocket_config;
}

bool DeviceCloudConfigService::SaveProvisioningUrl(const std::string& url) {
    std::lock_guard<std::recursive_mutex> lock(config_mutex_);
    Settings settings(kCloudNamespace, true);
    settings.SetString(kProvisioningUrlKey, url.empty() ? kDefaultProvisioningUrl : url);
    ++config_generation_;
    ClearWebsocketConfig();
    ClearMqttConfig();
    {
        std::lock_guard<std::recursive_mutex> lock(config_mutex_);
        last_error_.clear();
    }
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

bool DeviceCloudConfigService::SaveWebsocketConfig(const DeviceCloudConfig& config) {
    Settings settings(kWebsocketNamespace, true);
    settings.SetString(kUrlKey, config.websocket_url);
    settings.SetString(kTokenKey, config.websocket_token);
    settings.SetInt(kVersionKey, config.websocket_version);
    return true;
}

bool DeviceCloudConfigService::SaveMqttConfig(const DeviceCloudConfig& config) {
    Settings settings(kMqttNamespace, true);
    settings.SetInt(kMqttProtocolVersionKey, config.mqtt_protocol_version);
    settings.SetString(kMqttBrokerAddressKey, config.mqtt_broker_address);
    settings.SetInt(kMqttBrokerPortKey, config.mqtt_broker_port);
    settings.SetString(kMqttUsernameKey, config.mqtt_username);
    settings.SetString(kMqttPasswordKey, config.mqtt_password);
    settings.SetInt(kMqttKeepaliveKey, config.mqtt_keepalive);
    settings.SetString(kMqttDeviceKey, config.mqtt_device_key);
    settings.SetBool(kMqttHomeEnabledKey, config.mqtt_home_enabled);
    settings.SetString(kMqttHttpBaseUrlKey, config.mqtt_http_base_url);
    settings.SetString(kMqttTelemetryTopicKey, config.mqtt_topic_telemetry);
    settings.SetString(kMqttShadowReportTopicKey, config.mqtt_topic_shadow_report);
    settings.SetString(kMqttShadowDesiredTopicKey, config.mqtt_topic_shadow_desired);
    settings.SetString(kMqttOtaNotifyTopicKey, config.mqtt_topic_ota_notify);
    settings.SetString(kMqttOtaProgressTopicKey, config.mqtt_topic_ota_progress);
    settings.SetString(kMqttPcStatusTopicKey, config.mqtt_topic_pc_status);
    settings.SetString(kMqttHomePrefixTopicKey, config.mqtt_topic_home_prefix);
    return true;
}

void DeviceCloudConfigService::ClearWebsocketConfig() {
    Settings settings(kWebsocketNamespace, true);
    settings.SetString(kUrlKey, "");
    settings.SetString(kTokenKey, "");
    settings.SetInt(kVersionKey, 1);
}

void DeviceCloudConfigService::ClearMqttConfig() {
    DeviceCloudConfig config;
    ResetMqttConfig(config);
    SaveMqttConfig(config);
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
    config.has_activation_code = false;

    cJSON* websocket = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(websocket)) {
        AddStringIfPresent(websocket, kUrlKey, config.websocket_url);
        AddStringIfPresent(websocket, kTokenKey, config.websocket_token);
        AddIntIfPresent(websocket, kVersionKey, config.websocket_version);
        config.has_websocket_config = !config.websocket_url.empty();
    }

    cJSON* unified_mqtt = cJSON_GetObjectItem(root, "unifiedMqtt");
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
    cJSON_AddStringToObject(board, "type", "rymcu_bigsmart");
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

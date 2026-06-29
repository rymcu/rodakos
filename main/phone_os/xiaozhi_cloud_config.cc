#include "phone_os/xiaozhi_cloud_config.h"

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
constexpr const char* TAG = "XiaozhiCloud";
constexpr const char* kOtaNamespace = "xiaozhi";
constexpr const char* kWebsocketNamespace = "websocket";
constexpr const char* kBoardNamespace = "board";
constexpr const char* kOtaUrlKey = "ota_url";
constexpr const char* kUrlKey = "url";
constexpr const char* kTokenKey = "token";
constexpr const char* kVersionKey = "version";
constexpr const char* kUuidKey = "uuid";
constexpr const char* kDefaultOtaUrl = "https://api.tenclass.net/xiaozhi/ota/";
constexpr int kOtaTimeoutMs = 10000;
constexpr size_t kMaxOtaResponseBytes = 8192;

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

}  // namespace

const char* XiaozhiCloudConfigService::DefaultOtaUrl() {
    return kDefaultOtaUrl;
}

bool XiaozhiCloudConfigService::Load(XiaozhiCloudConfig& config) {
    Settings cloud_settings(kOtaNamespace, false);
    config.ota_url = cloud_settings.GetString(kOtaUrlKey, kDefaultOtaUrl);
    if (config.ota_url.empty()) {
        config.ota_url = kDefaultOtaUrl;
    }

    Settings ws_settings(kWebsocketNamespace, false);
    config.websocket_url = ws_settings.GetString(kUrlKey, "");
    config.websocket_token = ws_settings.GetString(kTokenKey, "");
    config.websocket_version = ws_settings.GetInt(kVersionKey, 1);
    if (config.websocket_version <= 0) {
        config.websocket_version = 1;
    }
    config.has_websocket_config = !config.websocket_url.empty();
    return config.has_websocket_config;
}

bool XiaozhiCloudConfigService::RefreshFromOta(XiaozhiCloudConfig& config) {
    Load(config);
    if (config.ota_url.empty()) {
        config.ota_url = kDefaultOtaUrl;
    }

    const std::string payload = BuildSystemInfoJson();
    esp_http_client_config_t http_config = {};
    http_config.url = config.ota_url.c_str();
    http_config.method = HTTP_METHOD_POST;
    http_config.timeout_ms = kOtaTimeoutMs;
    http_config.buffer_size = 1024;
    http_config.buffer_size_tx = 1024;
    http_config.crt_bundle_attach = esp_crt_bundle_attach;
    http_config.user_agent = "RodakOS/xiaozhi";

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) {
        SetError("Failed to create OTA client");
        return false;
    }

    const std::string client_id = GetClientId();
    const std::string mac = MacAddress();
    esp_http_client_set_header(client, "Activation-Version", "1");
    esp_http_client_set_header(client, "Device-Id", mac.c_str());
    esp_http_client_set_header(client, "Client-Id", client_id.c_str());
    esp_http_client_set_header(client, "Content-Type", "application/json");
    ESP_LOGI(TAG, "Refreshing XiaoZhi cloud config from %s", config.ota_url.c_str());
    if (payload.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        SetError("OTA request payload is too large");
        esp_http_client_cleanup(client);
        return false;
    }

    esp_err_t err = esp_http_client_open(client, payload.size());
    if (err != ESP_OK) {
        SetError(std::string("OTA open failed: ") + esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    const int write_len = esp_http_client_write(client, payload.c_str(), payload.size());
    if (write_len != static_cast<int>(payload.size())) {
        SetError(write_len < 0 ? "OTA request write failed" : "OTA request write incomplete");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    const int64_t content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        SetError("OTA response header fetch failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    const int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        SetError("OTA request returned HTTP " + std::to_string(status_code));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    std::vector<char> response(kMaxOtaResponseBytes + 1, '\0');
    const int read_len = esp_http_client_read_response(client, response.data(), kMaxOtaResponseBytes);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (read_len <= 0) {
        SetError("OTA response is empty");
        return false;
    }
    response[std::min(static_cast<size_t>(read_len), kMaxOtaResponseBytes)] = '\0';

    if (!ParseOtaResponse(std::string(response.data(), read_len), config)) {
        return false;
    }

    if (config.has_websocket_config) {
        SaveWebsocketConfig(config);
    }
    return config.has_websocket_config;
}

std::string XiaozhiCloudConfigService::GetClientId() {
    Settings settings(kBoardNamespace, true);
    std::string uuid = settings.GetString(kUuidKey, "");
    if (uuid.empty()) {
        uuid = GenerateUuid();
        settings.SetString(kUuidKey, uuid);
    }
    return uuid;
}

bool XiaozhiCloudConfigService::SaveWebsocketConfig(const XiaozhiCloudConfig& config) {
    Settings settings(kWebsocketNamespace, true);
    settings.SetString(kUrlKey, config.websocket_url);
    settings.SetString(kTokenKey, config.websocket_token);
    settings.SetInt(kVersionKey, config.websocket_version);
    return true;
}

bool XiaozhiCloudConfigService::ParseOtaResponse(const std::string& response,
                                                 XiaozhiCloudConfig& config) {
    cJSON* root = cJSON_Parse(response.c_str());
    if (root == nullptr) {
        SetError("OTA response is not JSON");
        return false;
    }

    cJSON* websocket = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(websocket)) {
        AddStringIfPresent(websocket, kUrlKey, config.websocket_url);
        AddStringIfPresent(websocket, kTokenKey, config.websocket_token);
        AddIntIfPresent(websocket, kVersionKey, config.websocket_version);
        config.has_websocket_config = !config.websocket_url.empty();
    }

    cJSON* activation = cJSON_GetObjectItem(root, "activation");
    if (cJSON_IsObject(activation)) {
        AddStringIfPresent(activation, "code", config.activation_code);
        AddStringIfPresent(activation, "message", config.activation_message);
        config.has_activation_code = !config.activation_code.empty();
    }

    cJSON_Delete(root);
    if (!config.has_websocket_config) {
        SetError(config.has_activation_code ? "Activate on xiaozhi.me" : "No websocket config from XiaoZhi cloud");
        return false;
    }
    if (config.websocket_version <= 0) {
        config.websocket_version = 1;
    }
    last_error_.clear();
    ESP_LOGI(TAG, "XiaoZhi websocket config ready: version=%d url=%s",
             config.websocket_version, config.websocket_url.c_str());
    return true;
}

std::string XiaozhiCloudConfigService::BuildSystemInfoJson() {
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

std::string XiaozhiCloudConfigService::BuildBoardJson() {
    cJSON* board = cJSON_CreateObject();
    cJSON_AddStringToObject(board, "type", "rymcu_bigsmart");
    cJSON_AddStringToObject(board, "name", "RodakOS RYMCU BigSmart");
    cJSON_AddStringToObject(board, "mac", MacAddress().c_str());
    std::string json = JsonToString(board);
    cJSON_Delete(board);
    return json;
}

void XiaozhiCloudConfigService::SetError(const std::string& message) {
    last_error_ = message;
    ESP_LOGW(TAG, "%s", last_error_.c_str());
}

}  // namespace rodakos

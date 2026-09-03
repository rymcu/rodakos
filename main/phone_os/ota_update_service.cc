#include "phone_os/ota_update_service.h"

#include "phone_os/device_cloud_config.h"
#include "rodak_sha256.h"
#include "rodakos_adapters/file_service.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_image_format.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <unistd.h>
#include <vector>

namespace rodakos {
namespace {
constexpr const char* TAG = "OtaUpdate";
constexpr size_t kIoBufferSize = 32 * 1024;
constexpr size_t kMaxJsonResponseBytes = 16 * 1024;
constexpr int kHttpTimeoutMs = 20 * 1000;
constexpr int kReportRetryInitialMs = 2 * 1000;
constexpr int kReportRetryMaxMs = 30 * 1000;
constexpr int kReportTaskCreationRetryMs = 5 * 1000;

struct DownloadTaskContext {
    OtaUpdateService* service = nullptr;
    std::string payload;
};

std::string JsonString(cJSON* object, const char* key) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}

uint64_t JsonUint64(cJSON* object, const char* key) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) && item->valuedouble > 0
               ? static_cast<uint64_t>(item->valuedouble)
               : 0;
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

std::string UrlEncode(const std::string& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (const unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded.push_back(static_cast<char>(ch));
        } else {
            encoded.push_back('%');
            encoded.push_back(kHex[ch >> 4]);
            encoded.push_back(kHex[ch & 0x0f]);
        }
    }
    return encoded;
}

std::string TrimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::string RedactUrlQuery(const std::string& url) {
    const size_t query = url.find('?');
    return query == std::string::npos ? url : url.substr(0, query) + "?<redacted>";
}

bool IsHttpUrl(const std::string& value) {
    return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

bool HasSameOrigin(const std::string& url, const std::string& base_url) {
    const std::string base = TrimTrailingSlash(base_url);
    return url == base || (url.size() > base.size() && url.rfind(base + "/", 0) == 0);
}

bool IsTerminalResultCode(int code) {
    return code >= 400 && code < 500 && code != 401 && code != 403 &&
           code != 408 && code != 425 && code != 429;
}

bool ReadJsonResponse(esp_http_client_handle_t client, std::string& response) {
    std::vector<char> buffer(kMaxJsonResponseBytes + 1, '\0');
    const int read = esp_http_client_read_response(client, buffer.data(), kMaxJsonResponseBytes);
    if (read <= 0) {
        return false;
    }
    response.assign(buffer.data(), static_cast<size_t>(read));
    return true;
}

bool PerformJsonRequest(const std::string& url, esp_http_client_method_t method,
                        const std::string& token, const std::string& body,
                        std::string& response, int* response_status = nullptr) {
    if (response_status != nullptr) {
        *response_status = 0;
    }
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = method;
    config.timeout_ms = kHttpTimeoutMs;
    config.buffer_size = 2048;
    config.buffer_size_tx = 2048;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.user_agent = "RodakOS/ota-v2";

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }

    const std::string authorization = "Bearer " + token;
    esp_http_client_set_header(client, "Authorization", authorization.c_str());
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_err_t err = esp_http_client_open(client, body.size());
    if (err == ESP_OK && !body.empty()) {
        const int written = esp_http_client_write(client, body.c_str(), body.size());
        if (written != static_cast<int>(body.size())) {
            err = ESP_FAIL;
        }
    }
    if (err == ESP_OK && esp_http_client_fetch_headers(client) < 0) {
        err = ESP_FAIL;
    }
    const int status = esp_http_client_get_status_code(client);
    if (response_status != nullptr) {
        *response_status = status;
    }
    const bool ok = err == ESP_OK && status >= 200 && status < 300 &&
                    ReadJsonResponse(client, response);
    if (!ok) {
        const std::string safe_url = RedactUrlQuery(url);
        ESP_LOGW(TAG, "HTTP request failed: %s status=%d url=%s",
                 esp_err_to_name(err), status, safe_url.c_str());
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

bool ParseApiData(const std::string& response, cJSON*& root, cJSON*& data) {
    root = cJSON_Parse(response.c_str());
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = nullptr;
        return false;
    }
    cJSON* code = cJSON_GetObjectItemCaseSensitive(root, "code");
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsNumber(code) || code->valueint != 200 || !cJSON_IsObject(data)) {
        ESP_LOGW(TAG, "Rodak OTA API rejected request: code=%d message=%s",
                 cJSON_IsNumber(code) ? code->valueint : 0,
                 JsonString(root, "message").c_str());
        cJSON_Delete(root);
        root = nullptr;
        data = nullptr;
        return false;
    }
    return true;
}
}  // namespace

OtaUpdateService::OtaUpdateService(DeviceCloudConfigService& config_service,
                                   FileService* file_service)
    : config_service_(config_service), file_service_(file_service) {
    report_retry_timer_ = xTimerCreateStatic(
        "ota_report_retry", pdMS_TO_TICKS(kReportTaskCreationRetryMs), pdFALSE,
        this, ReportRetryTimerCallback, &report_retry_timer_storage_);
}

void OtaUpdateService::SetProgressPublisher(ProgressPublisher publisher) {
    std::lock_guard<std::mutex> lock(progress_publisher_mutex_);
    progress_publisher_ = std::move(publisher);
}

bool OtaUpdateService::HandleNotification(const std::string& payload) {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) {
        ESP_LOGW(TAG, "Ignoring OTA notification while another task is active");
        return false;
    }

    auto* context = new DownloadTaskContext{this, payload};
    const BaseType_t created =
        xTaskCreate(DownloadTask, "ota_download", 10 * 1024, context, 4, nullptr);
    if (created != pdPASS) {
        delete context;
        busy_.store(false);
        return false;
    }
    return true;
}

void OtaUpdateService::DownloadTask(void* arg) {
    std::unique_ptr<DownloadTaskContext> context(static_cast<DownloadTaskContext*>(arg));
    if (context && context->service != nullptr) {
        context->service->RunDownload(context->payload);
        context->service->busy_.store(false);
    }
    // FreeRTOS task deletion does not unwind C++ locals.
    context.reset();
    vTaskDelete(nullptr);
}

void OtaUpdateService::RunDownload(const std::string& notification_payload) {
    cJSON* notification = cJSON_Parse(notification_payload.c_str());
    if (!cJSON_IsObject(notification)) {
        cJSON_Delete(notification);
        ESP_LOGW(TAG, "OTA notification is not valid JSON");
        return;
    }
    const std::string task_no = JsonString(notification, "taskNo");
    const std::string notification_from_version = JsonString(notification, "fromVersion");
    const std::string target_version = JsonString(notification, "toVersion");
    cJSON_Delete(notification);
    if (task_no.empty()) {
        ESP_LOGW(TAG, "OTA notification does not contain taskNo");
        return;
    }

    OtaUpdateRecord existing;
    const OtaUpdateLoadResult existing_state = LoadOtaUpdateRecordStatus(existing);
    if (existing_state == OtaUpdateLoadResult::kCorrupt ||
        existing_state == OtaUpdateLoadResult::kError) {
        ESP_LOGE(TAG, "Refusing OTA because the recovery journal is unavailable");
        OtaUpdateRecord failed;
        failed.task_no = task_no;
        failed.detail = "设备 OTA 恢复状态不可用";
        failed.error_code = "STATE_CORRUPT";
        PublishProgress(task_no, 0, "failed", failed.detail);
        ReportResultUntilSuccess(failed, false);
        return;
    }
    if (existing.phase == OtaUpdatePhase::kReportAcknowledged) {
        if (!ClearCompletedRecord(existing)) {
            ESP_LOGE(TAG, "Refusing OTA because the acknowledged result cannot be cleared");
            return;
        }
        if (report_retry_timer_ != nullptr) {
            xTimerStop(report_retry_timer_, 0);
        }
    }
    if (!existing.task_no.empty() && existing.phase != OtaUpdatePhase::kIdle) {
        if (existing.task_no == task_no &&
            existing.phase == OtaUpdatePhase::kAwaitingStagedAck) {
            if (!CompleteStagedHandoff()) {
                ScheduleStateRetry("duplicate staged notification acknowledgement failure");
            }
            return;
        }
        if (existing.task_no == task_no &&
            (existing.phase == OtaUpdatePhase::kPending ||
             existing.phase == OtaUpdatePhase::kApplying ||
             existing.phase == OtaUpdatePhase::kRestoring)) {
            const esp_partition_t* recovery = esp_partition_find_first(
                ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
            if (recovery != nullptr && esp_ota_set_boot_partition(recovery) == ESP_OK) {
                ESP_LOGI(TAG, "Resuming staged OTA task %s in Recovery", task_no.c_str());
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
            }
        }
        ESP_LOGW(TAG, "Rejecting OTA task %s while task %s is awaiting completion",
                 task_no.c_str(), existing.task_no.c_str());
        return;
    }

    DeviceCloudConfig config;
    config_service_.Load(config);
    if (!config.has_mqtt_config || config.mqtt_http_base_url.empty() ||
        config.mqtt_password.empty()) {
        FailStaging(task_no, "CONFIG_MISSING", "缺少 Rodak OTA HTTP 配置");
        return;
    }

    const esp_app_desc_t* app_desc = esp_app_get_description();
    const std::string running_version = app_desc != nullptr ? app_desc->version : "";
    const std::string base_url = TrimTrailingSlash(config.mqtt_http_base_url);
    ESP_LOGI(TAG, "Checking OTA task %s before requesting a download ticket",
             task_no.c_str());

    UpgradeCheck check;
    if (!RequestUpgradeCheck(base_url, config.mqtt_password, check)) {
        ESP_LOGE(TAG, "OTA check request failed for task %s", task_no.c_str());
        FailStaging(task_no, "CHECK_FAILED", "无法完成 OTA 升级检查");
        return;
    }
    if (!check.upgrade_available || check.task_no != task_no ||
        running_version.empty() || notification_from_version.empty() ||
        target_version.empty() || check.from_version.empty() || check.to_version.empty() ||
        check.from_version != running_version ||
        check.from_version != notification_from_version ||
        check.to_version != target_version || check.from_version == check.to_version) {
        ESP_LOGE(TAG,
                 "OTA check invalid: available=%d task=%s expected_task=%s "
                 "from=%s running=%s notified_from=%s to=%s notified_to=%s",
                 check.upgrade_available, check.task_no.c_str(), task_no.c_str(),
                 check.from_version.c_str(), running_version.c_str(),
                 notification_from_version.c_str(), check.to_version.c_str(),
                 target_version.c_str());
        FailStaging(task_no, "OTA_CHECK_INVALID",
                    "OTA 检查结果与通知或当前固件不一致");
        return;
    }
    ESP_LOGI(TAG, "OTA check accepted: task=%s from=%s to=%s", task_no.c_str(),
             check.from_version.c_str(), check.to_version.c_str());
    PublishProgress(task_no, 0, "check", "OTA 升级检查通过");

    if (file_service_ == nullptr || (!file_service_->IsMounted() && !file_service_->Init())) {
        FailStaging(task_no, "SD_MOUNT_FAILED", "无法挂载 SD 卡");
        return;
    }
    if (!file_service_->CreateDirectory(kOtaDirectory)) {
        FailStaging(task_no, "SD_DIRECTORY_FAILED", "无法创建 OTA 暂存目录");
        return;
    }

    PublishProgress(task_no, 0, "prepare", "正在申请下载凭证");
    std::string ticket_id;
    if (!RequestTicket(base_url, config.mqtt_password, task_no, ticket_id)) {
        FailStaging(task_no, "TICKET_FAILED", "无法获取 OTA 下载凭证");
        return;
    }
    ESP_LOGI(TAG, "OTA download ticket accepted: task=%s", task_no.c_str());

    Manifest manifest;
    if (!RequestManifest(base_url, config.mqtt_password, task_no, ticket_id, manifest)) {
        FailStaging(task_no, "MANIFEST_FAILED", "无法获取 OTA manifest");
        return;
    }
    if (manifest.task_no != task_no || manifest.version.empty() ||
        manifest.version != target_version || manifest.url.empty() ||
        manifest.file_size == 0 || manifest.checksum_type != "sha256" ||
        manifest.checksum_value.size() != 64) {
        FailStaging(task_no, "MANIFEST_INVALID", "OTA manifest 字段不完整或校验算法不受支持");
        return;
    }
    if (task_no.size() > kOtaTaskNoMaxBytes ||
        manifest.version.size() > kOtaVersionMaxBytes ||
        target_version.size() > kOtaVersionMaxBytes) {
        FailStaging(task_no, "MANIFEST_TOO_LONG", "OTA 任务号或版本字符串超过设备上限");
        return;
    }
    ESP_LOGI(TAG, "OTA manifest accepted: task=%s version=%s size=%llu",
             task_no.c_str(), manifest.version.c_str(),
             static_cast<unsigned long long>(manifest.file_size));

    const esp_partition_t* main_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    if (main_partition == nullptr || manifest.file_size > main_partition->size) {
        FailStaging(task_no, "IMAGE_TOO_LARGE", "固件超过主应用分区容量");
        return;
    }

    FileService::Capacity capacity;
    if (!file_service_->GetCapacity(capacity) ||
        capacity.free_bytes < manifest.file_size + main_partition->size) {
        FailStaging(task_no, "SD_SPACE_LOW", "SD 卡空间不足，无法保留新固件和回滚备份");
        return;
    }

    OtaUpdateRecord record = existing;
    record.task_no = task_no;
    record.target_version = manifest.version;
    if (!BackupRunningImage(record)) {
        FailStaging(task_no, "BACKUP_FAILED", "无法备份当前固件到 SD 卡");
        return;
    }
    if (!DownloadToSd(manifest, config.mqtt_password, base_url)) {
        FailStaging(task_no, "DOWNLOAD_FAILED", "固件下载或 SHA-256 校验失败");
        return;
    }

    record.phase = OtaUpdatePhase::kAwaitingStagedAck;
    record.pending_size = manifest.file_size;
    record.pending_sha256 = manifest.checksum_value;
    record.detail = "固件已暂存到 SD 卡，等待 Recovery 应用";
    record.error_code.clear();
    if (!SaveOtaUpdateRecord(record)) {
        FailStaging(task_no, "STATE_SAVE_FAILED", "无法保存 OTA 恢复状态");
        return;
    }
    if (!CompleteStagedHandoff()) {
        ScheduleStateRetry("staged progress acknowledgement failure");
    }
}

bool OtaUpdateService::CompleteStagedHandoff() {
    OtaUpdateRecord record;
    const OtaUpdateLoadResult load_result = LoadOtaUpdateRecordStatus(record);
    if (load_result != OtaUpdateLoadResult::kValid) {
        ESP_LOGE(TAG, "Cannot complete staged handoff because the OTA journal is unavailable");
        return false;
    }
    if (record.phase != OtaUpdatePhase::kAwaitingStagedAck) {
        return true;
    }
    if (file_service_ == nullptr || (!file_service_->IsMounted() && !file_service_->Init())) {
        ESP_LOGW(TAG, "OTA staged handoff is waiting for the SD card: task=%s",
                 record.task_no.c_str());
        return false;
    }
    if (!VerifySdImage(kOtaPendingImagePath, record.pending_size,
                       record.pending_sha256) ||
        !VerifySdImage(kOtaInstalledImagePath, record.installed_size,
                       record.installed_sha256)) {
        FailStaging(record.task_no, "STAGED_IMAGE_INVALID",
                    "SD 卡中的升级固件或回滚备份校验失败");
        return true;
    }
    if (record.task_no.empty() ||
        !PublishProgress(record.task_no, 100, "staged", record.detail, true)) {
        ESP_LOGW(TAG, "OTA staged progress is still awaiting acknowledgement: task=%s",
                 record.task_no.c_str());
        return false;
    }

    OtaUpdateRecord current;
    if (LoadOtaUpdateRecordStatus(current) != OtaUpdateLoadResult::kValid) {
        ESP_LOGE(TAG, "Cannot persist staged acknowledgement because the OTA journal changed");
        return false;
    }
    if (current.phase != OtaUpdatePhase::kAwaitingStagedAck ||
        current.task_no != record.task_no) {
        return true;
    }
    current.phase = OtaUpdatePhase::kPending;
    if (!SaveOtaUpdateRecord(current)) {
        ESP_LOGE(TAG, "Failed to persist acknowledged staged state: task=%s",
                 current.task_no.c_str());
        return false;
    }
    ESP_LOGI(TAG, "OTA staged progress acknowledged: task=%s", current.task_no.c_str());

    const esp_partition_t* recovery = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
    const esp_err_t err = recovery != nullptr ? esp_ota_set_boot_partition(recovery)
                                               : ESP_ERR_NOT_FOUND;
    if (err != ESP_OK) {
        FailStaging(current.task_no, "RECOVERY_SELECT_FAILED", "无法切换到 Recovery 分区");
        return true;
    }
    if (report_retry_timer_ != nullptr) {
        xTimerStop(report_retry_timer_, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return true;
}

bool OtaUpdateService::RequestUpgradeCheck(const std::string& base_url,
                                           const std::string& token,
                                           UpgradeCheck& check) {
    std::string response;
    if (!PerformJsonRequest(base_url + "/api/v1/ota/check", HTTP_METHOD_GET,
                            token, {}, response)) {
        return false;
    }
    cJSON* root = nullptr;
    cJSON* data = nullptr;
    if (!ParseApiData(response, root, data)) {
        return false;
    }
    cJSON* upgrade_available =
        cJSON_GetObjectItemCaseSensitive(data, "upgradeAvailable");
    check.upgrade_available = cJSON_IsTrue(upgrade_available);
    check.task_no = JsonString(data, "taskNo");
    check.from_version = JsonString(data, "fromVersion");
    check.to_version = JsonString(data, "toVersion");
    cJSON_Delete(root);
    return true;
}

bool OtaUpdateService::RequestTicket(const std::string& base_url, const std::string& token,
                                     const std::string& task_no, std::string& ticket_id) {
    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "taskNo", task_no.c_str());
    const std::string body_text = EncodeJson(body);
    cJSON_Delete(body);

    std::string response;
    if (!PerformJsonRequest(base_url + "/api/v1/ota/download-ticket", HTTP_METHOD_POST,
                            token, body_text, response)) {
        return false;
    }
    cJSON* root = nullptr;
    cJSON* data = nullptr;
    if (!ParseApiData(response, root, data)) {
        return false;
    }
    ticket_id = JsonString(data, "ticketId");
    cJSON_Delete(root);
    return !ticket_id.empty();
}

bool OtaUpdateService::RequestManifest(const std::string& base_url, const std::string& token,
                                       const std::string& task_no,
                                       const std::string& ticket_id, Manifest& manifest) {
    const std::string url = base_url + "/api/v1/ota/manifest/" + UrlEncode(task_no) +
                            "?ticketId=" + UrlEncode(ticket_id);
    std::string response;
    if (!PerformJsonRequest(url, HTTP_METHOD_GET, token, {}, response)) {
        return false;
    }
    cJSON* root = nullptr;
    cJSON* data = nullptr;
    if (!ParseApiData(response, root, data)) {
        return false;
    }
    manifest.task_no = JsonString(data, "taskNo");
    manifest.version = JsonString(data, "version");
    manifest.url = JsonString(data, "url");
    manifest.file_size = JsonUint64(data, "fileSize");
    manifest.checksum_type = JsonString(data, "checksumType");
    manifest.checksum_value = JsonString(data, "checksumValue");
    std::transform(manifest.checksum_type.begin(), manifest.checksum_type.end(),
                   manifest.checksum_type.begin(), [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    std::transform(manifest.checksum_value.begin(), manifest.checksum_value.end(),
                   manifest.checksum_value.begin(), [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    cJSON_Delete(root);
    return true;
}

bool OtaUpdateService::DownloadToSd(const Manifest& manifest, const std::string& token,
                                    const std::string& http_base_url) {
    if (!IsHttpUrl(manifest.url)) {
        return false;
    }
    if (file_service_->Exists(kOtaPendingPartPath)) {
        file_service_->DeleteFile(kOtaPendingPartPath);
    }

    const std::string output_path = SdPath(kOtaPendingPartPath);
    FILE* output = std::fopen(output_path.c_str(), "wb");
    if (output == nullptr) {
        return false;
    }

    esp_http_client_config_t config = {};
    config.url = manifest.url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = kHttpTimeoutMs;
    config.buffer_size = 4096;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.user_agent = "RodakOS/ota-v2";
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        std::fclose(output);
        return false;
    }
    std::string authorization;
    if (HasSameOrigin(manifest.url, http_base_url)) {
        authorization = "Bearer " + token;
        esp_http_client_set_header(client, "Authorization", authorization.c_str());
    }

    esp_err_t err = esp_http_client_open(client, 0);
    const int64_t content_length = err == ESP_OK ? esp_http_client_fetch_headers(client) : -1;
    const int status = esp_http_client_get_status_code(client);
    bool ok = err == ESP_OK && status >= 200 && status < 300 &&
              (content_length <= 0 || static_cast<uint64_t>(content_length) == manifest.file_size);

    Sha256 sha;
    ok = ok && sha.Start();
    std::unique_ptr<unsigned char[]> buffer(
        new (std::nothrow) unsigned char[kIoBufferSize]);
    if (buffer == nullptr) {
        std::fclose(output);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    uint64_t total = 0;
    int last_progress = -1;
    while (ok && total < manifest.file_size) {
        const int read = esp_http_client_read(client, reinterpret_cast<char*>(buffer.get()),
                                              static_cast<int>(std::min<uint64_t>(
                                                  kIoBufferSize, manifest.file_size - total)));
        if (read <= 0) {
            ok = false;
            break;
        }
        ok = std::fwrite(buffer.get(), 1, static_cast<size_t>(read), output) ==
                 static_cast<size_t>(read) &&
             sha.Update(buffer.get(), static_cast<size_t>(read));
        total += static_cast<uint64_t>(read);
        const int progress = static_cast<int>((total * 90) / manifest.file_size);
        if (progress / 5 != last_progress / 5) {
            last_progress = progress;
            PublishProgress(manifest.task_no, progress, "download", "正在下载固件到 SD 卡");
        }
    }

    std::array<unsigned char, 32> digest = {};
    ok = ok && total == manifest.file_size && sha.Finish(digest);
    if (ok) {
        ok = std::fflush(output) == 0;
    }
    if (ok) {
        ok = fsync(fileno(output)) == 0;
    }
    std::fclose(output);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ok = ok && Sha256ToHex(digest) == manifest.checksum_value;
    if (!ok) {
        file_service_->DeleteFile(kOtaPendingPartPath);
        return false;
    }
    if (file_service_->Exists(kOtaPendingImagePath)) {
        file_service_->DeleteFile(kOtaPendingImagePath);
    }
    return file_service_->Rename(kOtaPendingPartPath, kOtaPendingImagePath);
}

bool OtaUpdateService::BackupRunningImage(OtaUpdateRecord& record) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr || running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        ESP_LOGE(TAG, "OTA staging must run from the main ota_0 partition");
        return false;
    }

    const esp_partition_pos_t position = {
        .offset = running->address,
        .size = running->size,
    };
    esp_image_metadata_t metadata = {};
    if (esp_image_get_metadata(&position, &metadata) != ESP_OK || metadata.image_len == 0) {
        return false;
    }

    if (file_service_->Exists(kOtaInstalledPartPath)) {
        file_service_->DeleteFile(kOtaInstalledPartPath);
    }
    FILE* output = std::fopen(SdPath(kOtaInstalledPartPath).c_str(), "wb");
    if (output == nullptr) {
        return false;
    }

    Sha256 sha;
    bool ok = sha.Start();
    std::unique_ptr<unsigned char[]> buffer(
        new (std::nothrow) unsigned char[kIoBufferSize]);
    if (buffer == nullptr) {
        std::fclose(output);
        file_service_->DeleteFile(kOtaInstalledPartPath);
        return false;
    }
    uint64_t offset = 0;
    while (ok && offset < metadata.image_len) {
        const size_t length = static_cast<size_t>(
            std::min<uint64_t>(kIoBufferSize, metadata.image_len - offset));
        ok = esp_partition_read(running, offset, buffer.get(), length) == ESP_OK &&
             std::fwrite(buffer.get(), 1, length, output) == length &&
             sha.Update(buffer.get(), length);
        offset += length;
    }
    std::array<unsigned char, 32> digest = {};
    ok = ok && sha.Finish(digest);
    if (ok) {
        ok = std::fflush(output) == 0;
    }
    if (ok) {
        ok = fsync(fileno(output)) == 0;
    }
    std::fclose(output);
    if (!ok) {
        file_service_->DeleteFile(kOtaInstalledPartPath);
        return false;
    }

    if (file_service_->Exists(kOtaInstalledImagePath)) {
        file_service_->DeleteFile(kOtaInstalledImagePath);
    }
    if (!file_service_->Rename(kOtaInstalledPartPath, kOtaInstalledImagePath)) {
        return false;
    }
    record.installed_size = metadata.image_len;
    record.installed_sha256 = Sha256ToHex(digest);
    return true;
}

bool OtaUpdateService::PromotePendingImage(OtaUpdateRecord& record) {
    if (file_service_ == nullptr || (!file_service_->IsMounted() && !file_service_->Init())) {
        return false;
    }
    if (!file_service_->Exists(kOtaPendingImagePath)) {
        if (file_service_->Exists(kOtaInstalledImagePath) &&
            VerifySdImage(kOtaInstalledImagePath, record.pending_size,
                          record.pending_sha256)) {
            record.installed_size = record.pending_size;
            record.installed_sha256 = record.pending_sha256;
            return true;
        }
        return BackupRunningImage(record);
    }
    if (file_service_->Exists(kOtaInstalledImagePath)) {
        file_service_->DeleteFile(kOtaInstalledImagePath);
    }
    if (!file_service_->Rename(kOtaPendingImagePath, kOtaInstalledImagePath)) {
        return false;
    }
    record.installed_size = record.pending_size;
    record.installed_sha256 = record.pending_sha256;
    return true;
}

bool OtaUpdateService::VerifySdImage(const char* relative_path, uint64_t expected_size,
                                     const std::string& expected_sha256) {
    if (expected_size == 0 || expected_sha256.size() != 64) {
        return false;
    }
    FILE* input = std::fopen(SdPath(relative_path).c_str(), "rb");
    if (input == nullptr) {
        return false;
    }

    Sha256 sha;
    bool ok = sha.Start();
    std::unique_ptr<unsigned char[]> buffer(
        new (std::nothrow) unsigned char[kIoBufferSize]);
    if (buffer == nullptr) {
        std::fclose(input);
        return false;
    }
    uint64_t total = 0;
    while (ok) {
        const size_t read = std::fread(buffer.get(), 1, kIoBufferSize, input);
        if (read > 0) {
            total += read;
            ok = sha.Update(buffer.get(), read);
        }
        if (read < kIoBufferSize) {
            ok = ok && std::feof(input) != 0;
            break;
        }
    }
    std::fclose(input);

    std::array<unsigned char, 32> digest = {};
    ok = ok && sha.Finish(digest);
    return ok && total == expected_size && Sha256ToHex(digest) == expected_sha256;
}

bool OtaUpdateService::ConfirmRunningImage() {
    local_health_confirmed_.store(true);
    if (local_boot_confirmed_.load()) {
        return true;
    }

    bool expected = false;
    if (!confirming_.compare_exchange_strong(expected, true)) {
        ScheduleStateRetry("confirmation already in progress");
        return false;
    }

    const ConfirmationResult result = ConfirmRunningImageImpl();
    const bool confirmed = result == ConfirmationResult::kSuccess;
    if (confirmed) {
        local_boot_confirmed_.store(true);
        ESP_LOGI(TAG, "Local boot confirmation complete");
    }
    confirming_.store(false);
    if (result == ConfirmationResult::kRetryableFailure) {
        ScheduleStateRetry("local boot confirmation failure");
    }
    return confirmed;
}

OtaUpdateService::ConfirmationResult OtaUpdateService::ConfirmRunningImageImpl() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr || running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        ESP_LOGE(TAG, "Cannot confirm a non-ota_0 running image");
        return ConfirmationResult::kTerminalFailure;
    }

    OtaUpdateRecord record;
    const OtaUpdateLoadResult load_result = LoadOtaUpdateRecordStatus(record);
    if (load_result == OtaUpdateLoadResult::kCorrupt) {
        ESP_LOGE(TAG, "Cannot confirm image because the OTA journal is corrupt");
        return ConfirmationResult::kTerminalFailure;
    }
    if (load_result == OtaUpdateLoadResult::kError) {
        ESP_LOGE(TAG, "Cannot confirm image because the OTA journal is unavailable");
        return ConfirmationResult::kRetryableFailure;
    }
    if (record.phase == OtaUpdatePhase::kPending ||
        record.phase == OtaUpdatePhase::kApplying ||
        record.phase == OtaUpdatePhase::kRestoring) {
        const esp_partition_t* recovery = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
        if (recovery != nullptr && esp_ota_set_boot_partition(recovery) == ESP_OK) {
            ESP_LOGW(TAG, "Resuming interrupted OTA apply in Recovery");
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }
        ESP_LOGE(TAG, "Failed to resume interrupted OTA apply in Recovery");
        return ConfirmationResult::kRetryableFailure;
    }

    const bool new_image_handoff = record.phase == OtaUpdatePhase::kReadyToBoot;
    const bool rollback_handoff_transition =
        record.phase == OtaUpdatePhase::kRollbackReadyToBoot ||
        record.phase == OtaUpdatePhase::kRollbackBooting;
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to confirm running image: %s", esp_err_to_name(err));
        return ConfirmationResult::kRetryableFailure;
    }
    if (!new_image_handoff && !rollback_handoff_transition) {
        return ConfirmationResult::kSuccess;
    }
    if (new_image_handoff) {
        record.phase = OtaUpdatePhase::kConfirmed;
        record.detail = "新固件已通过本地启动确认，等待联网确认";
        record.error_code.clear();
    } else {
        record.phase = OtaUpdatePhase::kRollbackPending;
        record.detail = "新固件启动失败，上一版本已恢复";
        if (record.error_code.empty()) {
            record.error_code = "BOOT_ROLLBACK";
        }
    }
    if (!SaveOtaUpdateRecord(record)) {
        ESP_LOGE(TAG, "Failed to persist OTA boot confirmation state");
        return ConfirmationResult::kRetryableFailure;
    }
    return ConfirmationResult::kSuccess;
}

void OtaUpdateService::OnNetworkReady() {
    if (local_health_confirmed_.load() && !local_boot_confirmed_.load() &&
        !ConfirmRunningImage()) {
        return;
    }

    OtaUpdateRecord record;
    OtaUpdateLoadResult load_result = LoadOtaUpdateRecordStatus(record);
    if (load_result != OtaUpdateLoadResult::kValid) {
        if (load_result == OtaUpdateLoadResult::kError) {
            ScheduleStateRetry("OTA journal load failure");
        }
        return;
    }
    if (record.phase == OtaUpdatePhase::kReportAcknowledged) {
        if (!ClearCompletedRecord(record)) {
            ScheduleStateRetry("acknowledged OTA cleanup failure");
        }
        return;
    }
    if (record.phase == OtaUpdatePhase::kAwaitingStagedAck) {
        if (record.task_no.empty() || busy_.load()) {
            return;
        }
        bool expected = false;
        if (!staged_handoff_running_.compare_exchange_strong(expected, true)) {
            return;
        }
        if (xTaskCreate(StagedHandoffTask, "ota_staged_ack", 6144, this, 3, nullptr) !=
            pdPASS) {
            staged_handoff_running_.store(false);
            ScheduleStateRetry("staged acknowledgement task creation failure");
        } else if (report_retry_timer_ != nullptr) {
            xTimerStop(report_retry_timer_, 0);
        }
        return;
    }
    if ((record.phase != OtaUpdatePhase::kConfirmed &&
         record.phase != OtaUpdatePhase::kRollbackPending &&
         record.phase != OtaUpdatePhase::kFailed) ||
        record.task_no.empty()) {
        return;
    }

    bool expected = false;
    if (!reporting_.compare_exchange_strong(expected, true)) {
        return;
    }
    if (xTaskCreate(ReportTask, "ota_report", 6144, this, 3, nullptr) != pdPASS) {
        reporting_.store(false);
        ScheduleStateRetry("OTA result task creation failure");
    } else if (report_retry_timer_ != nullptr) {
        xTimerStop(report_retry_timer_, 0);
    }
}

void OtaUpdateService::StagedHandoffTask(void* arg) {
    auto* service = static_cast<OtaUpdateService*>(arg);
    if (service != nullptr) {
        const bool completed = service->CompleteStagedHandoff();
        service->staged_handoff_running_.store(false);
        if (!completed) {
            service->ScheduleStateRetry("staged progress acknowledgement failure");
        }
    }
    vTaskDelete(nullptr);
}

bool OtaUpdateService::ScheduleStateRetry(const char* reason) {
    if (report_retry_timer_ == nullptr ||
        xTimerReset(report_retry_timer_, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to schedule OTA state retry after %s", reason);
        return false;
    }
    ESP_LOGW(TAG, "Scheduled OTA state retry after %s", reason);
    return true;
}

void OtaUpdateService::ReportRetryTimerCallback(TimerHandle_t timer) {
    auto* service = static_cast<OtaUpdateService*>(pvTimerGetTimerID(timer));
    if (service == nullptr) {
        return;
    }

    bool expected = false;
    if (!service->retry_worker_running_.compare_exchange_strong(expected, true)) {
        xTimerReset(timer, 0);
        return;
    }
    if (xTaskCreate(RetryTask, "ota_state_retry", 6144, service, 3, nullptr) != pdPASS) {
        service->retry_worker_running_.store(false);
        if (xTimerReset(timer, 0) != pdPASS) {
            ESP_LOGE(TAG, "Failed to reschedule OTA state retry worker");
        }
    }
}

void OtaUpdateService::RetryTask(void* arg) {
    auto* service = static_cast<OtaUpdateService*>(arg);
    if (service != nullptr) {
        service->OnNetworkReady();
        service->retry_worker_running_.store(false);
    }
    vTaskDelete(nullptr);
}

void OtaUpdateService::ReportTask(void* arg) {
    auto* service = static_cast<OtaUpdateService*>(arg);
    if (service != nullptr) {
        int retry_delay_ms = kReportRetryInitialMs;
        while (!service->ReportPendingResult()) {
            vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
            retry_delay_ms = std::min(retry_delay_ms * 2, kReportRetryMaxMs);
        }
        service->reporting_.store(false);
    }
    vTaskDelete(nullptr);
}

bool OtaUpdateService::ReportPendingResult() {
    OtaUpdateRecord record;
    const OtaUpdateLoadResult load_result = LoadOtaUpdateRecordStatus(record);
    if (load_result == OtaUpdateLoadResult::kEmpty) {
        return true;
    }
    if (load_result == OtaUpdateLoadResult::kError) {
        return false;
    }
    if (load_result == OtaUpdateLoadResult::kCorrupt) {
        ESP_LOGE(TAG, "Cannot report OTA result because the recovery journal is corrupt");
        return true;
    }

    if (record.phase == OtaUpdatePhase::kReportAcknowledged) {
        return ClearCompletedRecord(record);
    }

    const bool success = record.phase == OtaUpdatePhase::kConfirmed;
    if (success) {
        const bool already_promoted = record.installed_size == record.pending_size &&
                                      record.installed_sha256 == record.pending_sha256;
        if (already_promoted || PromotePendingImage(record)) {
            record.detail = "新固件已通过启动与联网确认";
        } else {
            record.detail = "新固件已确认，但 SD 回滚备份更新失败";
            ESP_LOGW(TAG, "New image confirmed but SD backup promotion failed");
        }
        if (!SaveOtaUpdateRecord(record)) {
            ESP_LOGW(TAG, "Failed to persist updated SD backup metadata");
        }
    }

    bool already_acknowledged = false;
    {
        std::lock_guard<std::mutex> lock(report_mutex_);
        already_acknowledged = acknowledged_task_no_ == record.task_no;
    }
    if (!already_acknowledged) {
        const ReportResultOutcome outcome = ReportResultHttp(record, success);
        if (outcome == ReportResultOutcome::kRetryableFailure) {
            return false;
        }
        if (outcome == ReportResultOutcome::kTerminalFailure) {
            ESP_LOGE(TAG, "Rodak rejected OTA result for task %s; clearing local task",
                     record.task_no.c_str());
            PublishProgress(record.task_no, 0, "result_rejected",
                            "Rodak 已拒绝 OTA 结果，本地任务已结束");
        } else {
            PublishProgress(record.task_no, success ? 100 : 0,
                            success ? "success" : "failed", record.detail);
        }
        {
            std::lock_guard<std::mutex> lock(report_mutex_);
            acknowledged_task_no_ = record.task_no;
        }
    }

    record.phase = OtaUpdatePhase::kReportAcknowledged;
    if (!SaveOtaUpdateRecord(record)) {
        ESP_LOGE(TAG, "Failed to persist OTA result acknowledgement");
        return false;
    }
    return ClearCompletedRecord(record);
}

bool OtaUpdateService::ClearCompletedRecord(OtaUpdateRecord& record) {
    record.phase = OtaUpdatePhase::kIdle;
    record.task_no.clear();
    record.target_version.clear();
    record.pending_size = 0;
    record.pending_sha256.clear();
    record.detail.clear();
    record.error_code.clear();
    if (!SaveOtaUpdateRecord(record)) {
        ESP_LOGE(TAG, "Failed to clear acknowledged OTA result");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(report_mutex_);
        acknowledged_task_no_.clear();
    }
    return true;
}

OtaUpdateService::ReportResultOutcome OtaUpdateService::ReportResultHttp(
    const OtaUpdateRecord& record, bool success) {
    DeviceCloudConfig config;
    config_service_.Load(config);
    if (!config.has_mqtt_config || config.mqtt_http_base_url.empty() ||
        config.mqtt_password.empty()) {
        DeviceCloudConfig refreshed;
        config_service_.Refresh(refreshed);
        return ReportResultOutcome::kRetryableFailure;
    }

    cJSON* body = cJSON_CreateObject();
    cJSON_AddBoolToObject(body, "success", success);
    cJSON_AddStringToObject(body, "detail", record.detail.c_str());
    if (!record.error_code.empty()) {
        cJSON_AddStringToObject(body, "errorCode", record.error_code.c_str());
        cJSON_AddStringToObject(body, "errorMessage", record.detail.c_str());
    }
    const std::string body_text = EncodeJson(body);
    cJSON_Delete(body);

    std::string response;
    int response_status = 0;
    const std::string url = TrimTrailingSlash(config.mqtt_http_base_url) +
                            "/api/v1/ota/tasks/" + UrlEncode(record.task_no) + "/result";
    if (!PerformJsonRequest(url, HTTP_METHOD_POST, config.mqtt_password, body_text, response,
                            &response_status)) {
        if (response_status == 401 || response_status == 403) {
            DeviceCloudConfig refreshed;
            config_service_.Refresh(refreshed);
        }
        return IsTerminalResultCode(response_status)
                   ? ReportResultOutcome::kTerminalFailure
                   : ReportResultOutcome::kRetryableFailure;
    }
    cJSON* root = cJSON_Parse(response.c_str());
    cJSON* code = cJSON_IsObject(root) ? cJSON_GetObjectItemCaseSensitive(root, "code") : nullptr;
    const int result_code = cJSON_IsNumber(code) ? code->valueint : 0;
    cJSON_Delete(root);
    if (result_code == 200) {
        return ReportResultOutcome::kSuccess;
    }
    if (result_code == 401 || result_code == 403) {
        DeviceCloudConfig refreshed;
        config_service_.Refresh(refreshed);
    }
    return IsTerminalResultCode(result_code)
               ? ReportResultOutcome::kTerminalFailure
               : ReportResultOutcome::kRetryableFailure;
}

void OtaUpdateService::ReportResultUntilSuccess(const OtaUpdateRecord& record, bool success) {
    int retry_delay_ms = kReportRetryInitialMs;
    while (true) {
        const ReportResultOutcome outcome = ReportResultHttp(record, success);
        if (outcome == ReportResultOutcome::kSuccess) {
            return;
        }
        if (outcome == ReportResultOutcome::kTerminalFailure) {
            ESP_LOGE(TAG, "Rodak rejected non-persisted OTA result for task %s",
                     record.task_no.c_str());
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
        retry_delay_ms = std::min(retry_delay_ms * 2, kReportRetryMaxMs);
    }
}

bool OtaUpdateService::PublishProgress(const std::string& task_no, int progress_percent,
                                       const std::string& step_code,
                                       const std::string& detail, bool wait_for_ack) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "protocolVersion", 2);
    cJSON_AddStringToObject(root, "taskNo", task_no.c_str());
    cJSON_AddNumberToObject(root, "progressPercent", std::clamp(progress_percent, 0, 100));
    cJSON_AddStringToObject(root, "stepCode", step_code.c_str());
    if (!detail.empty()) {
        cJSON_AddStringToObject(root, "detail", detail.c_str());
    }
    const std::string payload = EncodeJson(root);
    cJSON_Delete(root);
    std::lock_guard<std::mutex> lock(progress_publisher_mutex_);
    return progress_publisher_ && progress_publisher_(payload, wait_for_ack);
}

void OtaUpdateService::FailStaging(const std::string& task_no,
                                   const std::string& error_code,
                                   const std::string& detail) {
    ESP_LOGE(TAG, "OTA staging failed: %s (%s)", detail.c_str(), error_code.c_str());
    PublishProgress(task_no, 0, "failed", detail);
    OtaUpdateRecord failed;
    const OtaUpdateLoadResult load_result = LoadOtaUpdateRecordStatus(failed);
    const bool can_persist = load_result == OtaUpdateLoadResult::kEmpty ||
                             load_result == OtaUpdateLoadResult::kValid;
    failed.phase = OtaUpdatePhase::kFailed;
    failed.task_no = task_no;
    failed.pending_size = 0;
    failed.pending_sha256.clear();
    failed.detail = detail;
    failed.error_code = error_code;
    const bool saved = can_persist && SaveOtaUpdateRecord(failed);
    if (can_persist && !saved) {
        ESP_LOGE(TAG, "Failed to persist OTA failure result");
    }
    if (saved) {
        OnNetworkReady();
    } else {
        ReportResultUntilSuccess(failed, false);
    }
}

std::string OtaUpdateService::SdPath(const char* relative_path) const {
    if (file_service_ == nullptr) {
        return relative_path;
    }
    return std::string(file_service_->GetMountPoint()) + relative_path;
}

}  // namespace rodakos

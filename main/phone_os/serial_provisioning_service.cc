#include "phone_os/serial_provisioning_service.h"

#include "phone_os/device_cloud_config.h"
#include "phone_os/serial_provisioning_protocol.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "rodakos_adapters/wifi_config.h"
#include "settings.h"

#include <cJSON.h>
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

namespace rodakos {
namespace {
constexpr const char* TAG = "SerialProvisioning";
constexpr const char* kTransactionNamespace = "serial_prov";
constexpr const char* kTransactionPendingKey = "pending";
constexpr const char* kFramePrefix = kSerialProvisioningFramePrefix;
constexpr const char* kReadyLine = "\nRODAK_PROVISION_READY {\"version\":1}\n";
constexpr size_t kMaxFrameBytes = kSerialProvisioningMaxFrameBytes;
constexpr size_t kMaxSsidBytes = 32;
// WPA-PSK passwords are limited to 63 octets; reserving the NUL byte avoids
// silently truncating a value in wifi_config_t.
constexpr size_t kMaxPasswordBytes = 63;
constexpr size_t kMaxBootstrapUrlBytes = kSerialProvisioningMaxBootstrapUrlBytes;
constexpr TickType_t kPollIntervalTicks = pdMS_TO_TICKS(20);
constexpr int64_t kReadyIntervalUs = 5000000;
constexpr size_t kMaxDrainBytesPerPoll = 512;
constexpr size_t kUsbSerialRxBufferBytes = kMaxFrameBytes * 2;
constexpr size_t kUsbSerialTxBufferBytes = 512;
constexpr TickType_t kProvisioningReplyDrainTicks = pdMS_TO_TICKS(250);

bool HasControlCharacter(const std::string& value) {
    for (const unsigned char character : value) {
        if (character < 0x20 || character == 0x7f) {
            return true;
        }
    }
    return false;
}

bool ReadStringField(cJSON* root, const char* key, size_t max_bytes, bool allow_empty,
                     std::string& output, std::string& error) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        error = std::string("missing_") + key;
        return false;
    }
    output = item->valuestring;
    if ((!allow_empty && output.empty()) || output.size() > max_bytes ||
        HasControlCharacter(output)) {
        error = std::string("invalid_") + key;
        return false;
    }
    return true;
}

int ReadUsbSerialJtagBytes(uint8_t* buffer, size_t capacity) {
    if (buffer == nullptr || capacity == 0) {
        return 0;
    }

    // Read the ring buffer directly. The VFS and stdio layers can retain a
    // descriptor-level read state, while the driver API is the unambiguous RX
    // path after PrepareConsoleInput() has installed the driver.
    if (!usb_serial_jtag_is_driver_installed()) {
        static std::atomic<bool> missing_driver_logged{false};
        if (!missing_driver_logged.exchange(true)) {
            ESP_LOGE(TAG, "USB Serial/JTAG provisioning RX driver is unavailable");
        }
        return 0;
    }
    return usb_serial_jtag_read_bytes(buffer, static_cast<uint32_t>(capacity), 0);
}

bool SetPendingTransaction(bool pending) {
    Settings settings(kTransactionNamespace, true);
    if (!settings.SetBool(kTransactionPendingKey, pending) || !settings.Commit()) {
        (void)settings.Commit();
        return false;
    }
    return true;
}

bool HasPendingTransaction() {
    Settings settings(kTransactionNamespace, false);
    return settings.GetBool(kTransactionPendingKey, false);
}
}  // namespace

SerialProvisioningService::SerialProvisioningService(
    WiFiAdapter* wifi, DeviceCloudConfigService& cloud_config,
    CloudRefreshCallback cloud_refresh)
    : wifi_(wifi),
      cloud_config_(cloud_config),
      cloud_refresh_callback_(std::move(cloud_refresh)) {
    stopped_semaphore_ = xSemaphoreCreateBinaryStatic(&stopped_semaphore_storage_);
}

SerialProvisioningService::~SerialProvisioningService() {
    Stop();
}

bool SerialProvisioningService::RecoverPendingTransaction(
    DeviceCloudConfigService& cloud_config) {
    if (!HasPendingTransaction()) {
        return true;
    }

    ESP_LOGW(TAG,
             "Interrupted provisioning transaction detected; clearing WiFi and cloud cache");
    WiFiConfig wifi_config;
    const bool wifi_cleared = wifi_config.ClearCredentials();
    const bool cloud_reset =
        cloud_config.SaveProvisioningUrl(DeviceCloudConfigService::DefaultProvisioningUrl()) ==
        ProvisioningUrlSaveResult::kSaved;
    const bool marker_cleared = wifi_cleared && cloud_reset && SetPendingTransaction(false);
    if (!marker_cleared) {
        ESP_LOGE(TAG, "Unable to finish interrupted provisioning recovery; retrying next boot");
    } else {
        ESP_LOGI(TAG, "Interrupted provisioning recovery complete");
    }
    return marker_cleared;
}

bool SerialProvisioningService::Start() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (running_.load()) {
        return true;
    }
    if (stopped_semaphore_ == nullptr) {
        return false;
    }
    // A previous task may have signalled completion before a caller starts
    // the service again. Drain that signal before creating the new task.
    xSemaphoreTake(stopped_semaphore_, 0);

    if (!PrepareConsoleInput()) {
        return false;
    }

    cloud_refresh_pending_.store(false);
    running_.store(true);
    // Voice wake/AFE reserves most internal SRAM; provisioning only parses a
    // bounded 2 KiB frame and can run with a smaller stack.
    if (xTaskCreatePinnedToCore(TaskEntry, "serial_prov", 2048, this, 2, &task_,
                                usb_serial_driver_core_) != pdPASS) {
        running_.store(false);
        cloud_refresh_pending_.store(false);
        task_ = nullptr;
        RestoreConsoleInput();
        return false;
    }
    return true;
}

void SerialProvisioningService::Stop() {
    TaskHandle_t task = nullptr;
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (!running_.load() && task_ == nullptr) {
            return;
        }
        running_.store(false);
        cloud_refresh_pending_.store(false);
        task = task_;
    }

    if (task != nullptr && stopped_semaphore_ != nullptr) {
        xSemaphoreTake(stopped_semaphore_, portMAX_DELAY);
    }

    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    task_ = nullptr;
}

bool SerialProvisioningService::PrepareConsoleInput() {
    if (usb_serial_jtag_is_driver_installed()) {
        ESP_LOGE(TAG, "USB Serial/JTAG driver is already owned by another service");
        return false;
    }

    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = kUsbSerialTxBufferBytes,
        .rx_buffer_size = kUsbSerialRxBufferBytes,
    };
    usb_serial_driver_core_ = xPortGetCoreID();
    const esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB Serial/JTAG driver: %s",
                 esp_err_to_name(err));
        usb_serial_driver_core_ = tskNO_AFFINITY;
        RestoreConsoleInput();
        return false;
    }
    owns_usb_serial_driver_ = true;

    // The hardware RX FIFO is only 64 bytes. Backing the existing console VFS
    // with the official ring buffer keeps complete provisioning frames intact;
    // Run() reads that same ring directly through the driver API.
    usb_serial_jtag_vfs_use_driver();
    ESP_LOGI(TAG,
             "USB Serial/JTAG provisioning RX ready (direct driver, ring=%u, frame_limit=%u)",
             static_cast<unsigned>(kUsbSerialRxBufferBytes),
             static_cast<unsigned>(kMaxFrameBytes));
    return true;
}

void SerialProvisioningService::RestoreConsoleInput() {
    if (owns_usb_serial_driver_) {
        std::fflush(stdout);
        std::fflush(stderr);
        const esp_err_t drain_err = usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(250));
        usb_serial_jtag_vfs_use_nonblocking();
        const esp_err_t uninstall_err = usb_serial_jtag_driver_uninstall();
        owns_usb_serial_driver_ = false;
        usb_serial_driver_core_ = tskNO_AFFINITY;
        if (drain_err != ESP_OK) {
            ESP_LOGW(TAG, "USB Serial/JTAG TX drain timed out");
        }
        if (uninstall_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to uninstall USB Serial/JTAG driver: %s",
                     esp_err_to_name(uninstall_err));
        }
    }
}

void SerialProvisioningService::TaskEntry(void* arg) {
    auto* service = static_cast<SerialProvisioningService*>(arg);
    if (service != nullptr) {
        service->Run();
        service->RestoreConsoleInput();
        {
            std::lock_guard<std::mutex> lock(service->lifecycle_mutex_);
            service->task_ = nullptr;
        }
        if (service->stopped_semaphore_ != nullptr) {
            xSemaphoreGive(service->stopped_semaphore_);
        }
    }
    vTaskDelete(nullptr);
}

void SerialProvisioningService::Run() {
    SendReady();
    int64_t last_ready_us = esp_timer_get_time();
    std::string line;
    SerialProvisioningFrameAccumulator accumulator;
    bool first_rx_logged = false;
    uint8_t input[128];

    while (running_.load()) {
        if (cloud_refresh_pending_.exchange(false)) {
            // SendResult() must reach the host before the callback restarts
            // the device and tears down the shared USB console.
            if (owns_usb_serial_driver_) {
                const esp_err_t drain_err =
                    usb_serial_jtag_wait_tx_done(kProvisioningReplyDrainTicks);
                if (drain_err != ESP_OK) {
                    ESP_LOGW(TAG, "Provisioning result TX drain timed out before cloud restart");
                }
            }
            if (cloud_refresh_callback_) {
                cloud_refresh_callback_();
            }
            continue;
        }
        const int64_t now_us = esp_timer_get_time();
        if (now_us - last_ready_us >= kReadyIntervalUs) {
            SendReady();
            last_ready_us = now_us;
        }
        size_t drained = 0;
        while (running_.load() && drained < kMaxDrainBytesPerPoll) {
            const int count = ReadUsbSerialJtagBytes(input, sizeof(input));
            if (count <= 0) {
                break;
            }
            if (!first_rx_logged) {
                ESP_LOGI(TAG, "USB Serial/JTAG provisioning RX received first chunk (%d bytes)",
                         count);
                first_rx_logged = true;
            }
            drained += static_cast<size_t>(count);

            for (int index = 0; index < count; ++index) {
                const auto result = accumulator.Push(input[index], line);
                if (result == SerialProvisioningFrameResult::kFrameTooLarge) {
                    SendResult(false, "frame_too_large");
                } else if (result == SerialProvisioningFrameResult::kLineReady &&
                           !line.empty()) {
                    HandleLine(line);
                }
            }
        }
        if (drained == 0 || drained < kMaxDrainBytesPerPoll) {
            vTaskDelay(kPollIntervalTicks);
        }
    }
}

bool SerialProvisioningService::HandleLine(const std::string& line) {
    if (line.rfind(kFramePrefix, 0) != 0) {
        // Console input is shared with the monitor; ignore unrelated lines
        // without logging their contents.
        return false;
    }
    // The accumulator enforces this for live input. Keep the guard here too
    // so a line supplied by a future transport cannot bypass wire framing.
    if (line.size() >= kMaxFrameBytes) {
        SendResult(false, "frame_too_large");
        return false;
    }

    const std::string json = line.substr(strlen(kFramePrefix));
    Request request;
    std::string error;
    if (!ParseRequest(json, request, error)) {
        SendResult(false, error.c_str());
        return false;
    }
    if (!ApplyRequest(request, error)) {
        SendResult(false, error.c_str());
        return false;
    }
    SendResult(true);
    return true;
}

bool SerialProvisioningService::ParseRequest(const std::string& json,
                                             Request& request,
                                             std::string& error) const {
    if (json.empty() || json.size() > kSerialProvisioningMaxJsonBytes) {
        error = "invalid_frame";
        return false;
    }
    if (ContainsSerialProvisioningJsonNul(json)) {
        error = "invalid_json";
        return false;
    }

    const char* parse_end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(json.data(), json.size(), &parse_end, 0);
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root != nullptr) {
            cJSON_Delete(root);
        }
        error = "invalid_json";
        return false;
    }
    if (parse_end == nullptr ||
        std::any_of(parse_end, json.data() + json.size(), [](char character) {
            return !std::isspace(static_cast<unsigned char>(character));
        })) {
        cJSON_Delete(root);
        error = "invalid_json";
        return false;
    }

    cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (version != nullptr && (!cJSON_IsNumber(version) || version->valuedouble != 1.0)) {
        cJSON_Delete(root);
        error = "unsupported_version";
        return false;
    }

    const bool valid = ReadStringField(root, "ssid", kMaxSsidBytes, false,
                                       request.ssid, error) &&
                       ReadStringField(root, "password", kMaxPasswordBytes, true,
                                       request.password, error) &&
                       ReadStringField(root, "bootstrap_url", kMaxBootstrapUrlBytes, false,
                                       request.bootstrap_url, error);
    if (valid && !IsValidSerialProvisioningBootstrapUrl(request.bootstrap_url)) {
        error = "invalid_bootstrap_url";
    }
    cJSON_Delete(root);
    return valid && error.empty();
}

bool SerialProvisioningService::ApplyRequest(const Request& request, std::string& error) {
    WiFiConfig previous_wifi;
    std::string previous_ssid;
    std::string previous_password;
    const bool had_previous_wifi = previous_wifi.LoadCredentials(previous_ssid, previous_password);

    if (!SetPendingTransaction(true)) {
        error = "nvs_write_failed";
        return false;
    }

    WiFiConfig wifi_config;
    if (!wifi_config.SaveCredentials(request.ssid, request.password)) {
        const bool restored = had_previous_wifi
                                  ? previous_wifi.SaveCredentials(previous_ssid, previous_password)
                                  : previous_wifi.ClearCredentials();
        if (restored) {
            (void)SetPendingTransaction(false);
        }
        error = "nvs_write_failed";
        return false;
    }
    const ProvisioningUrlSaveResult cloud_save_result =
        cloud_config_.SaveProvisioningUrl(request.bootstrap_url);
    if (cloud_save_result != ProvisioningUrlSaveResult::kSaved) {
        bool wifi_restored = false;
        if (had_previous_wifi) {
            wifi_restored = previous_wifi.SaveCredentials(previous_ssid, previous_password);
        } else {
            wifi_restored = previous_wifi.ClearCredentials();
        }
        if (!wifi_restored) {
            ESP_LOGE(TAG, "Failed to restore WiFi credentials after cloud NVS error");
        } else if (ShouldClearSerialProvisioningPendingAfterCloudSaveFailure(
                       wifi_restored, cloud_save_result)) {
            (void)SetPendingTransaction(false);
        }
        if (cloud_save_result == ProvisioningUrlSaveResult::kStateUncertain) {
            ESP_LOGE(TAG,
                     "Cloud provisioning state is uncertain; preserving pending recovery marker");
            error = "nvs_state_uncertain";
        } else {
            error = "nvs_write_failed";
        }
        return false;
    }

    if (wifi_ == nullptr) {
        ESP_LOGW(TAG, "Provisioned credentials saved but WiFi adapter is unavailable");
        if (!SetPendingTransaction(false)) {
            error = "nvs_write_failed";
            return false;
        }
        return true;
    }

    if (!SetPendingTransaction(false)) {
        ESP_LOGE(TAG, "Provisioning data saved but transaction marker could not be cleared");
        error = "nvs_write_failed";
        return false;
    }

    // Force a fresh association so the IP event also starts the cloud client
    // when provisioning is performed on an already-connected device. Wait
    // for the adapter's generation barrier instead of guessing with a delay.
    if (!wifi_->DisconnectAndWait(2000)) {
        ESP_LOGW(TAG, "Timed out waiting for the previous WiFi connection to end");
        error = "wifi_disconnect_timeout";
        return false;
    }
    bool connect_started = false;
    for (int attempt = 0; attempt < 4 && !connect_started; ++attempt) {
        connect_started = wifi_->Connect(
            request.ssid, request.password,
            [this](WiFiStatus status) {
                if (!running_.load()) {
                    return;
                }
                if (status == WiFiStatus::kConnected) {
                    ESP_LOGI(TAG, "Provisioned WiFi connected; scheduling cloud refresh");
                    cloud_refresh_pending_.store(true);
                } else if (status == WiFiStatus::kFailed) {
                    ESP_LOGW(TAG, "Provisioned WiFi connection failed; credentials remain saved");
                }
            });
        if (!connect_started && attempt + 1 < 4) {
            (void)wifi_->DisconnectAndWait(2000);
        }
    }
    if (!connect_started) {
        // Keep the validated credentials in NVS. A later boot or a manual
        // retry can still use them when the adapter becomes available.
        error = "wifi_start_failed";
        return false;
    }
    return true;
}

void SerialProvisioningService::SendReady() {
    std::fwrite(kReadyLine, 1, strlen(kReadyLine), stdout);
    std::fflush(stdout);
}

void SerialProvisioningService::SendResult(bool ok, const char* error) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        std::fputs("RODAK_PROVISION_RESULT {\"ok\":false,\"error\":\"internal_error\"}\n",
                   stdout);
        std::fflush(stdout);
        return;
    }
    cJSON_AddBoolToObject(root, "ok", ok);
    if (!ok && error != nullptr && error[0] != '\0') {
        cJSON_AddStringToObject(root, "error", error);
    }
    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == nullptr) {
        std::fputs("RODAK_PROVISION_RESULT {\"ok\":false,\"error\":\"internal_error\"}\n",
                   stdout);
        std::fflush(stdout);
        return;
    }
    // Emit the complete response through one stdio call.  Logs share this
    // console, so separate writes could otherwise be interleaved between the
    // prefix, JSON body, and terminating LF.
    std::fprintf(stdout, "RODAK_PROVISION_RESULT %s\n", json);
    std::fflush(stdout);
    cJSON_free(json);
}

}  // namespace rodakos

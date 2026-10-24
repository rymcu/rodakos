#include "phone_os/resource_failure_injection.h"
#include "phone_os/serial_provisioning_service.h"
#ifdef RODAKOS_RELEASE_TESTS
#include "phone_os/voice_lifecycle_diagnostic.h"
#include "phone_os/voice_prepare_priority_diagnostic.h"
#include "phone_os/lvgl_alloc_failure.h"
#include <esp_heap_trace.h>
#endif

#include "phone_os/device_cloud_config.h"
#include "phone_os/serial_provisioning_protocol.h"
#include "phone_os/serial_provisioning_binding.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "rodakos_adapters/wifi_config.h"
#include "settings.h"

#include <cJSON.h>
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace rodakos {
namespace {
constexpr const char* TAG = "SerialProvisioning";
constexpr const char* kTransactionNamespace = "serial_prov";
constexpr const char* kTransactionPendingKey = "pending";
constexpr const char* kFramePrefix = kSerialProvisioningFramePrefix;
constexpr const char* kReadyLine = "\nRODAK_PROVISION_READY {\"version\":1,\"server_trust\":1}\n";
constexpr size_t kMaxFrameBytes = kSerialProvisioningMaxFrameBytes;
constexpr size_t kMaxSsidBytes = 32;
// WPA-PSK passwords are limited to 63 octets; reserving the NUL byte avoids
// silently truncating a value in wifi_config_t.
constexpr size_t kMaxPasswordBytes = 63;
constexpr size_t kMaxBootstrapUrlBytes = kSerialProvisioningMaxBootstrapUrlBytes;
// USB trust validation includes NVS decoding and the SDK's nested X.509/PSA
// public-key import. The old 4 KiB budget overflowed on repeated provisioning.
// The 8 KiB COM3 probe used 4216 bytes; retain 1928 bytes of measured margin.
constexpr uint32_t kProvisioningTaskStackBytes = 6144;
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
    CloudRefreshCallback cloud_refresh, VoiceTestCallback voice_test,
    AppLaunchCallback app_launch)
    : wifi_(wifi),
      cloud_config_(cloud_config),
      cloud_refresh_callback_(std::move(cloud_refresh)),
      voice_test_callback_(std::move(voice_test)),
      app_launch_callback_(std::move(app_launch)) {
    stopped_semaphore_ = xSemaphoreCreateBinaryStatic(&stopped_semaphore_storage_);
}

void SerialProvisioningService::SetAppLaunchCallback(AppLaunchCallback callback) {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    app_launch_callback_ = std::move(callback);
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
    auto recovery_config = std::unique_ptr<DeviceCloudConfig>(new (std::nothrow) DeviceCloudConfig);
    if (recovery_config == nullptr) return false;
    cloud_config.Load(*recovery_config);
    // A USB-installed pin must survive interruption. Clearing WiFi prevents
    // mixed onboarding data from connecting; recovery must never downgrade TLS.
    const bool cloud_reset = !recovery_config->server_trust_error &&
        (!recovery_config->server_trust.empty() || cloud_config.SaveProvisioningUrl(
            DeviceCloudConfigService::DefaultProvisioningUrl(),
            ProvisioningUrlSaveMode::kForceRefresh) == ProvisioningUrlSaveResult::kSaved);
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
    if (xTaskCreatePinnedToCore(TaskEntry, "serial_prov", kProvisioningTaskStackBytes, this, 2, &task_,
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
                    if (line.rfind(kFramePrefix, 0) == 0) {
                        ESP_LOGI(TAG, "Provisioning health: stack_min_free=%u internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u",
                                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
                    }
                }
            }
        }
        if (drained == 0 || drained < kMaxDrainBytesPerPoll) {
            vTaskDelay(kPollIntervalTicks);
        }
    }
}

#ifdef RODAKOS_RELEASE_TESTS
namespace {
// Release-test heap map: prints each internal heap's free spans (>= 512 B) with the
// sizes of the used blocks on both sides, so post-Camera fragmentation can be
// attributed. The walker runs under the heap lock, so the callback only records
// free spans into fixed storage and patches the following used-block size.
struct HeapMapEntry {
    uintptr_t address;
    uint32_t size;
    uint32_t used_before;
    uint32_t used_after;
    uintptr_t next_address;
};
struct HeapMapCapture {
    static constexpr size_t kCapacity = 160;
    HeapMapEntry entries[kCapacity];
    size_t count = 0;
    size_t dropped = 0;
    size_t blocks = 0;
    uint32_t previous_used = 0;
    bool pending_after = false;
};

bool CaptureHeapBlock(walker_heap_into_t, walker_block_info_t block, void* user_data) {
    auto* capture = static_cast<HeapMapCapture*>(user_data);
    ++capture->blocks;
    const auto size = static_cast<uint32_t>(block.size);
    if (capture->pending_after) {
        auto& last = capture->entries[capture->count - 1];
        last.next_address = reinterpret_cast<uintptr_t>(block.ptr);
        last.used_after = block.used ? size : 0;
        capture->pending_after = false;
    }
    if (!block.used && size >= 512) {
        if (capture->count < HeapMapCapture::kCapacity) {
            capture->entries[capture->count++] = {reinterpret_cast<uintptr_t>(block.ptr), size,
                                                  capture->previous_used, 0, 0};
            capture->pending_after = true;
        } else {
            ++capture->dropped;
        }
    }
    capture->previous_used = block.used ? size : 0;
    return true;
}

void PrintInternalHeapMap() {
    auto* capture = static_cast<HeapMapCapture*>(
        heap_caps_malloc(sizeof(HeapMapCapture), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (capture == nullptr) {
        std::fprintf(stdout, "RODAK_RELEASE_TEST_RESULT {\"heap_map\":false}\n");
        std::fflush(stdout);
        return;
    }
    *capture = HeapMapCapture{};
    heap_caps_walk(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, CaptureHeapBlock, capture);
    std::fprintf(stdout, "RODAK_HEAP_MAP begin blocks=%u dropped=%u free=%u largest=%u\n",
                 static_cast<unsigned>(capture->blocks), static_cast<unsigned>(capture->dropped),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    for (size_t index = 0; index < capture->count; ++index) {
        const auto& entry = capture->entries[index];
        std::fprintf(stdout, "RODAK_HEAP_MAP free addr=0x%08x size=%u before=0x%08x/%u after=0x%08x/%u\n",
                     static_cast<unsigned>(entry.address), static_cast<unsigned>(entry.size), 0u,
                     static_cast<unsigned>(entry.used_before), static_cast<unsigned>(entry.next_address),
                     static_cast<unsigned>(entry.used_after));
    }
    std::fprintf(stdout, "RODAK_HEAP_MAP end\n");
    std::fflush(stdout);
    heap_caps_free(capture);
}

// Release-test memory pressure: holds internal DMA-capable blocks until release, so a real
// driver allocation (for example the DVP ring on STREAMON) fails without an injected fault.
// Each block is the largest free block, capped at 4 KiB, until the internal DMA largest is
// below the requested limit. The handle table is in PSRAM, so it does not use the RAM it holds.
constexpr size_t kPressureSlots = 32;
constexpr size_t kPressureMaxBlock = 4096;
constexpr size_t kPressureMinBlock = 64;
void** g_pressure_blocks = nullptr;
size_t g_pressure_count = 0;

size_t InternalDmaLargest() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
}

void PrintPressureResult(const char* action) {
    std::fprintf(stdout,
                 "RODAK_RELEASE_TEST_RESULT {\"%s\":%u,\"internal_free\":%u,\"internal_dma_largest\":%u}\n",
                 action, static_cast<unsigned>(g_pressure_count),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(InternalDmaLargest()));
    std::fflush(stdout);
}

void HoldDmaPressure(const std::string& arguments) {
    const size_t limit = std::strtoul(arguments.c_str(), nullptr, 10);
    if (g_pressure_blocks == nullptr) {
        g_pressure_blocks = static_cast<void**>(
            heap_caps_calloc(kPressureSlots, sizeof(void*), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    while (g_pressure_blocks != nullptr && g_pressure_count < kPressureSlots) {
        const size_t largest = InternalDmaLargest();
        if (largest < limit || largest < kPressureMinBlock) break;
        const size_t size = std::min(largest, kPressureMaxBlock);
        void* block = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (block == nullptr) break;
        g_pressure_blocks[g_pressure_count++] = block;
    }
    PrintPressureResult("hold_dma");
}

void ReleaseDmaPressure() {
    while (g_pressure_blocks != nullptr && g_pressure_count > 0) {
        heap_caps_free(g_pressure_blocks[--g_pressure_count]);
        g_pressure_blocks[g_pressure_count] = nullptr;
    }
    PrintPressureResult("release_dma");
}

#if CONFIG_HEAP_TRACING_STANDALONE
// Release-test leak trace: records only allocations still alive between start and
// dump, in a PSRAM buffer so tracing does not consume the internal heap it measures.
constexpr size_t kHeapTraceRecords = 600;
heap_trace_record_t* g_heap_trace_records = nullptr;

void StartHeapLeakTrace() {
    if (g_heap_trace_records == nullptr) {
        g_heap_trace_records = static_cast<heap_trace_record_t*>(heap_caps_calloc(
            kHeapTraceRecords, sizeof(heap_trace_record_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    esp_err_t result = g_heap_trace_records == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
    if (result == ESP_OK) {
        heap_trace_stop();
        result = heap_trace_init_standalone(g_heap_trace_records, kHeapTraceRecords);
    }
    if (result == ESP_OK) result = heap_trace_start(HEAP_TRACE_LEAKS);
    std::fprintf(stdout, "RODAK_RELEASE_TEST_RESULT {\"heap_trace_start\":%d}\n", static_cast<int>(result));
    std::fflush(stdout);
}

void DumpHeapLeakTrace() {
    heap_trace_stop();
    heap_trace_summary_t summary{};
    heap_trace_summary(&summary);
    std::fprintf(stdout, "RODAK_HEAP_TRACE begin count=%u overflowed=%d total_allocations=%u total_frees=%u\n",
                 static_cast<unsigned>(summary.count), summary.has_overflowed ? 1 : 0,
                 static_cast<unsigned>(summary.total_allocations), static_cast<unsigned>(summary.total_frees));
    std::fflush(stdout);
    // Only internal-RAM survivors are relevant to the fragmented internal heap.
    heap_trace_dump_caps(MALLOC_CAP_INTERNAL);
    std::fprintf(stdout, "RODAK_HEAP_TRACE end\n");
    std::fflush(stdout);
}
#endif
}  // namespace
#endif

bool SerialProvisioningService::HandleLine(const std::string& line) {
#ifdef RODAKOS_RELEASE_TESTS
    if (voice_lifecycle_diagnostic_ != nullptr) {
        if (voice_lifecycle_diagnostic_->HandleSerialLine(line)) return true;
        if (voice_lifecycle_diagnostic_->BlocksSerialMutation(line)) {
            std::fprintf(stdout, "RODAK_RELEASE_TEST_RESULT {\"accepted\":false,\"reason\":\"voice_cycle_busy\"}\n");
            std::fflush(stdout);
            return false;
        }
    }
#if CONFIG_HEAP_TRACING_STANDALONE
    if (line == "RODAK_RELEASE_TEST_V1 heap_trace_start") {
        StartHeapLeakTrace();
        return true;
    }
    if (line == "RODAK_RELEASE_TEST_V1 heap_trace_dump") {
        DumpHeapLeakTrace();
        return true;
    }
#endif
    constexpr char kLvglAllocFailPrefix[] = "RODAK_RELEASE_TEST_V1 lvgl_alloc_fail ";
    if (line.rfind(kLvglAllocFailPrefix, 0) == 0) {
        char* end = nullptr;
        const char* arguments = line.c_str() + sizeof(kLvglAllocFailPrefix) - 1;
        const unsigned long skip = std::strtoul(arguments, &end, 10);
        const unsigned long count = end != nullptr && *end == ' ' ? std::strtoul(end + 1, nullptr, 10) : 1;
        ArmLvglAllocationFailure(static_cast<uint32_t>(skip), static_cast<uint32_t>(count));
        return true;
    }
    constexpr char kHoldDmaPrefix[] = "RODAK_RELEASE_TEST_V1 hold_dma ";
    if (line.rfind(kHoldDmaPrefix, 0) == 0) {
        HoldDmaPressure(line.substr(sizeof(kHoldDmaPrefix) - 1));
        return true;
    }
    if (line == "RODAK_RELEASE_TEST_V1 release_dma") {
        ReleaseDmaPressure();
        return true;
    }
    if (line == "RODAK_RELEASE_TEST_V1 mqtt_drop") {
        const bool dropped = mqtt_drop_callback_ && mqtt_drop_callback_();
        std::fprintf(stdout, "RODAK_RELEASE_TEST_RESULT {\"mqtt_drop\":%s}\n", dropped ? "true" : "false");
        std::fflush(stdout);
        return dropped;
    }
    // 硬件验收：与设置页“选择 Wi-Fi”相同——只切换并在连上后保存 Wi-Fi，不改设备云配置，
    // 用于验证换网后经 mDNS 找回服务器。SSID 不能含空格。
    constexpr char kWifiSwitchPrefix[] = "RODAK_RELEASE_TEST_V1 wifi_switch ";
    if (line.rfind(kWifiSwitchPrefix, 0) == 0 && wifi_ != nullptr) {
        const std::string arguments = line.substr(sizeof(kWifiSwitchPrefix) - 1);
        const size_t split = arguments.find(' ');
        const std::string ssid = arguments.substr(0, split);
        const std::string password = split == std::string::npos ? std::string() : arguments.substr(split + 1);
        const bool started = !ssid.empty() && wifi_->DisconnectAndWait(2000) &&
            wifi_->Connect(ssid, password, [ssid, password](WiFiStatus status) {
                if (status != WiFiStatus::kConnected) return;
                WiFiConfig config;
                if (!config.SaveCredentials(ssid, password)) {
                    ESP_LOGW(TAG, "Release test WiFi switch connected but credentials were not saved");
                }
            });
        std::fprintf(stdout, "RODAK_RELEASE_TEST_RESULT {\"wifi_switch\":%s}\n",
                     started ? "true" : "false");
        std::fflush(stdout);
        return started;
    }
    constexpr char kLanOnboardingPrefix[] = "RODAK_RELEASE_TEST_V1 lan_onboarding ";
    if (line.rfind(kLanOnboardingPrefix, 0) == 0) {
        const bool queued = lan_onboarding_test_callback_ &&
            lan_onboarding_test_callback_(line.substr(sizeof(kLanOnboardingPrefix) - 1));
        std::fprintf(stdout, "RODAK_RELEASE_TEST_RESULT {\"lan_onboarding\":%s}\n",
                     queued ? "true" : "false");
        std::fflush(stdout);
        return queued;
    }
    if (line == "RODAK_RELEASE_TEST_V1 heap_map") {
        PrintInternalHeapMap();
        return true;
    }
    if (line == "RODAK_RELEASE_TEST_V1 voice_prepare_take") {
        PrintVoicePreparePrioritySnapshot();
        return true;
    }
    constexpr char kResourceTestPrefix[] = "RODAK_RELEASE_TEST_V1 fail_alloc ";
    if (line.rfind(kResourceTestPrefix, 0) == 0) {
        const bool armed = ArmResourceFailure(line.substr(sizeof(kResourceTestPrefix) - 1).c_str());
        std::fprintf(stdout, "RODAK_RELEASE_TEST_RESULT {\"armed\":%s}\n", armed ? "true" : "false");
        std::fflush(stdout);
        return armed;
    }
#endif
    constexpr char kAppLaunchPrefix[] = "RODAK_APP_LAUNCH_V1 ";
    if (line.rfind(kAppLaunchPrefix, 0) == 0) {
        const std::string app_id = line.substr(sizeof(kAppLaunchPrefix) - 1);
        const bool queued = !app_id.empty() && app_id.size() < 64 &&
                            app_launch_callback_ && app_launch_callback_(app_id);
        std::fprintf(stdout, "RODAK_APP_LAUNCH_RESULT {\"queued\":%s}\n",
                     queued ? "true" : "false");
        std::fflush(stdout);
        return queued;
    }
    constexpr char kVoiceTestPrefix[] = "RODAK_VOICE_TEST_V1 ";
    if (line.rfind(kVoiceTestPrefix, 0) == 0) {
        const std::string command = line.substr(sizeof(kVoiceTestPrefix) - 1);
        const bool aec_command = command == "aec_status" || command == "aec_stop" ||
                                 command == "aec_clear" || command.rfind("aec_arm ", 0) == 0 ||
                                 command.rfind("aec_read ", 0) == 0;
        const bool wake_eval = command.rfind("wake_eval ", 0) == 0;
        const bool wake_echo = command.rfind("wake_echo ", 0) == 0;
        const bool valid = aec_command || wake_eval || wake_echo || command == "wake" || command == "stop" ||
                           command == "audio_replay" || command == "audio_live" ||
                           command == "audio_clear" || command.rfind("audio_begin ", 0) == 0 ||
                           command.rfind("audio_chunk ", 0) == 0;
        const bool queued = valid && voice_test_callback_ && voice_test_callback_(command);
        const char* verb = !valid ? "invalid" : aec_command ? "aec" : wake_eval ? "wake_eval" :
                           wake_echo ? "wake_echo" :
                           command == "wake" ? "wake" :
                           command == "stop" ? "stop" : command == "audio_replay" ? "audio_replay" :
                           command == "audio_live" ? "audio_live" :
                           command == "audio_clear" ? "audio_clear" :
                           command.rfind("audio_begin ", 0) == 0 ? "audio_begin" : "audio_chunk";
        std::fprintf(stdout, "RODAK_VOICE_TEST_RESULT {\"ok\":%s,\"command\":\"%s\"}\n",
                     queued ? "true" : "false", verb);
        std::fflush(stdout);
        return queued;
    }
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
    std::string binding_proof;
    if (!ApplyRequest(request, binding_proof, error)) {
        SendResult(false, error.c_str());
        return false;
    }
    const std::string device_key = cloud_config_.GetDeviceKey();
    SendResult(true, nullptr, device_key.c_str(), binding_proof.c_str());
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
    request.binding_nonce.clear();
    const cJSON* binding_nonce = cJSON_GetObjectItemCaseSensitive(root, "binding_nonce");
    if (valid && error.empty() && binding_nonce != nullptr) {
        if (!cJSON_IsString(binding_nonce) || binding_nonce->valuestring == nullptr ||
            !IsValidSerialProvisioningBindingNonce(binding_nonce->valuestring)) {
            error = "invalid_binding_nonce";
        } else {
            request.binding_nonce = binding_nonce->valuestring;
        }
    }
    request.server_trust = {};
    const auto* server_trust = cJSON_GetObjectItemCaseSensitive(root, "server_trust");
    if (valid && error.empty() && server_trust != nullptr &&
        (!ParseServerTrust(server_trust, request.server_trust, error) ||
         !IsServerTrustBootstrap(request.server_trust, request.bootstrap_url))) {
        error = "invalid_server_trust";
    }
    cJSON_Delete(root);
    return valid && error.empty();
}

bool SerialProvisioningService::ApplyRequest(const Request& request, std::string& binding_proof,
                                           std::string& error) {
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
        cloud_config_.SaveSerialProvisioning(request.bootstrap_url, request.binding_nonce,
                                             binding_proof, request.server_trust.empty()
                                                 ? nullptr : &request.server_trust);
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

void SerialProvisioningService::SendResult(bool ok, const char* error, const char* device_key,
                                         const char* binding_proof) {
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
    if (ok && device_key != nullptr && device_key[0] != '\0') {
        cJSON_AddStringToObject(root, "deviceKey", device_key);
    }
    if (ok && binding_proof != nullptr && binding_proof[0] != '\0') {
        cJSON_AddStringToObject(root, "bindingProof", binding_proof);
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

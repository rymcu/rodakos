#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

class WiFiAdapter;

namespace rodakos {

class DeviceCloudConfigService;

// Receives the operator-side WiFi/cloud provisioning frame from the USB
// Serial/JTAG driver used by the active console. Logs and input remain on one
// COM channel, while RX does not depend on the stdin descriptor state.
class SerialProvisioningService {
public:
    using CloudRefreshCallback = std::function<void()>;
    using VoiceTestCallback = std::function<bool(const std::string&)>;
    using AppLaunchCallback = std::function<bool(const std::string&)>;

    SerialProvisioningService(WiFiAdapter* wifi,
                              DeviceCloudConfigService& cloud_config,
                              CloudRefreshCallback cloud_refresh = {},
                              VoiceTestCallback voice_test = {},
                              AppLaunchCallback app_launch = {});
    ~SerialProvisioningService();

    SerialProvisioningService(const SerialProvisioningService&) = delete;
    SerialProvisioningService& operator=(const SerialProvisioningService&) = delete;

    bool Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }
    void SetAppLaunchCallback(AppLaunchCallback callback);

    // Clear a provisioning transaction left behind by an interrupted boot.
    // Recovery is deliberately conservative: an incomplete transaction is
    // treated as unconfigured rather than allowing mixed credentials to run.
    static bool RecoverPendingTransaction(DeviceCloudConfigService& cloud_config);

private:
    struct Request {
        std::string ssid;
        std::string password;
        std::string bootstrap_url;
    };

    static void TaskEntry(void* arg);
    void Run();
    bool HandleLine(const std::string& line);
    bool ParseRequest(const std::string& json, Request& request, std::string& error) const;
    bool ApplyRequest(const Request& request, std::string& error);
    bool PrepareConsoleInput();
    void RestoreConsoleInput();
    void SendReady();
    void SendResult(bool ok, const char* error = nullptr);

    WiFiAdapter* wifi_ = nullptr;
    DeviceCloudConfigService& cloud_config_;
    CloudRefreshCallback cloud_refresh_callback_;
    VoiceTestCallback voice_test_callback_;
    AppLaunchCallback app_launch_callback_;
    mutable std::mutex lifecycle_mutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cloud_refresh_pending_{false};
    bool owns_usb_serial_driver_ = false;
    BaseType_t usb_serial_driver_core_ = tskNO_AFFINITY;
    TaskHandle_t task_ = nullptr;
    StaticSemaphore_t stopped_semaphore_storage_ = {};
    SemaphoreHandle_t stopped_semaphore_ = nullptr;
};

}  // namespace rodakos

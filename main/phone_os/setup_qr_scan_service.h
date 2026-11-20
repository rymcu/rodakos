#pragma once

#include "phone_os/setup_qr_payload.h"
#include "phone_os/setup_qr_provisioning.h"
#include "phone_os/task-retirement.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

class WiFiAdapter;

namespace rodakos {

class AudioFocusService;
class CameraService;
class DeviceCloudConfigService;

// Setup-QR scan (docs/qr-provisioning.md). One PSRAM-stack worker owns the whole flow:
// exclusive audio focus -> camera preview -> quirc decode -> stop camera -> parse. The UI then
// shows a confirmation; Confirm() saves on the internal-RAM task and reconnects WiFi.
enum class SetupQrScanPhase {
    kIdle,
    kStarting,
    kScanning,
    // A payload parsed; waiting for the user to confirm. The camera is already stopped.
    kConfirm,
    kSaving,
    kCompleted,
    kFailed,
};

struct SetupQrScanSnapshot {
    SetupQrScanPhase phase = SetupQrScanPhase::kIdle;
    // Confirmation details; the password is never exposed to the UI.
    SetupQrKind kind = SetupQrKind::kRodak;
    std::string ssid;
    bool open_network = false;
    // Host and optional port of the bootstrap URL, for display only.
    std::string server;
    // Saving would change the bootstrap URL, which restarts the device (see RunSave).
    bool will_restart = false;
    SetupQrApplyStatus result = SetupQrApplyStatus::kStorageFailed;
    // Stable code (qr_* error or apply status) for logs and UI mapping.
    std::string code;
    uint32_t frames_scanned = 0;
    uint32_t revision = 0;
};

class SetupQrScanService {
public:
    SetupQrScanService(CameraService* camera, AudioFocusService* audio_focus, WiFiAdapter* wifi,
                       DeviceCloudConfigService& cloud);
    ~SetupQrScanService();
    SetupQrScanService(const SetupQrScanService&) = delete;
    SetupQrScanService& operator=(const SetupQrScanService&) = delete;

    bool Available() const;
    bool StartScan();
    bool Confirm();
    // Stops the camera, discards a parsed payload and returns to kIdle.
    void Cancel();
    SetupQrScanSnapshot Snapshot() const;

    // Called after kServerChanged, from the worker, to restart the cloud client.
    void SetServerChangedCallback(std::function<void()> callback);

    // The worker stack is in PSRAM; NVS writes disable the cache and must run on an
    // internal-RAM stack. main.cc pumps this from its permanent task, as for LAN onboarding.
    void SetInternalWorkTask(TaskHandle_t task) { internal_work_task_ = task; }
    void PumpInternalStackWork();

private:
    enum class Job { kScan, kSave };
    bool StartJob(Job job);
    static void TaskBody(void* arg);
    void RunScan();
    void RunSave();
    bool Cancelled(uint32_t generation) const;
    void Publish(uint32_t generation, SetupQrScanPhase phase, std::string code = {});
    void ReleaseResources();
    void RunWithInternalStack(const std::function<void()>& work);

    CameraService* camera_ = nullptr;
    AudioFocusService* audio_focus_ = nullptr;
    WiFiAdapter* wifi_ = nullptr;
    DeviceCloudConfigService& cloud_;

    mutable std::mutex mutex_;
    SetupQrScanSnapshot snapshot_;
    // Holds the WiFi password between scan and confirm; cleared on every exit path.
    SetupQrPayload payload_;
    std::function<void()> server_changed_callback_;
    std::atomic<uint32_t> cancel_generation_{0};
    uint32_t job_generation_ = 0;
    Job job_ = Job::kScan;
    std::atomic<bool> busy_{false};
    uint32_t audio_focus_token_ = 0;
    bool camera_started_ = false;

    TaskRetirementOwner retirement_owner_;
    TaskRetirementTicket retirement_;
    TaskHandle_t internal_work_task_ = nullptr;
    std::mutex internal_work_mutex_;
    const std::function<void()>* internal_work_ = nullptr;
    TaskHandle_t internal_work_waiter_ = nullptr;
};

}  // namespace rodakos

#pragma once

#include "phone_os/task-retirement.h"
#include "rodak_ota_state.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

namespace rodakos {

class DeviceCloudConfigService;
struct DeviceCloudConfig;
class FileService;

class OtaUpdateService {
public:
    using ProgressPublisher =
        std::function<bool(const std::string& payload, bool wait_for_ack)>;

    OtaUpdateService(DeviceCloudConfigService& config_service, FileService* file_service);

    void SetProgressPublisher(ProgressPublisher publisher);
    bool HandleNotification(const std::string& payload);
    bool ConfirmRunningImage();
    void OnNetworkReady();
    bool IsBusy() const { return busy_.load(); }
    // Call only from a permanent task whose stack is in internal RAM (app_main). It runs the
    // flash and NVS steps that the PSRAM-stack download task hands over. Register the task
    // first, so a hand-over wakes it at once instead of at its next scheduled pass.
    void SetInternalWorkTask(TaskHandle_t task) { internal_work_task_ = task; }
    void PumpInternalStackWork();

private:
    enum class ConfirmationResult {
        kSuccess,
        kRetryableFailure,
        kTerminalFailure,
    };

    enum class ReportResultOutcome {
        kSuccess,
        kRetryableFailure,
        kTerminalFailure,
    };

    struct UpgradeCheck {
        bool upgrade_available = false;
        std::string task_no;
        std::string from_version;
        std::string to_version;
    };

    struct Manifest {
        std::string task_no;
        std::string version;
        std::string url;
        std::string checksum_type;
        std::string checksum_value;
        std::string signature_type;
        std::string signature_value;
        uint32_t manifest_version = 0;
        uint64_t file_size = 0;
    };

    static void DownloadTask(void* arg);
    // Flash and NVS access disables the cache, so the stack of the caller must be in internal
    // RAM. Runs `work` at once on an internal stack, else waits for PumpInternalStackWork.
    void RunWithInternalStack(const std::function<void()>& work);
    OtaUpdateLoadResult LoadRecord(OtaUpdateRecord& record);
    bool SaveRecord(const OtaUpdateRecord& record);
    void FaultPoint(const char* phase);
    bool SelectRecoveryPartition();
    void RefreshCloudConfig();
    // OTA HTTP sends the AIoT access token as its bearer. Rodak issues it for a short time
    // (10 minutes by default), and a backup plus download can take longer, so each request
    // first renews a token that is close to expiry.
    bool PrepareHttpCredentials(DeviceCloudConfig& cloud);
    static void StagedHandoffTask(void* arg);
    static void ReportTask(void* arg);
    static void RetryTask(void* arg);
    // on_worker_task: the caller is the state-retry worker, so staged work runs on its stack.
    void OnNetworkReady(bool on_worker_task);
    static void ReportRetryTimerCallback(TimerHandle_t timer);
    ConfirmationResult ConfirmRunningImageImpl();
    bool ScheduleStateRetry(const char* reason);
    void RunDownload(const std::string& notification_payload);
    bool CompleteStagedHandoff();
    bool SelectRecoveryAndRestart(const std::string& task_no);
    bool ReportPendingResult();
    bool ClearCompletedRecord(OtaUpdateRecord& record);
    bool RequestUpgradeCheck(const DeviceCloudConfig& cloud, UpgradeCheck& check);
    bool RequestTicket(const DeviceCloudConfig& cloud,
                       const std::string& task_no, std::string& ticket_id);
    bool RequestManifest(const DeviceCloudConfig& cloud,
                         const std::string& task_no, const std::string& ticket_id,
                         Manifest& manifest);
    bool DownloadToSd(const Manifest& manifest, const DeviceCloudConfig& cloud);
    bool BackupRunningImage(OtaUpdateRecord& record);
    bool PromotePendingImage(OtaUpdateRecord& record);
    bool VerifySdImage(const char* relative_path, uint64_t expected_size,
                       const std::string& expected_sha256);
    bool VerifyPendingSignature(const OtaUpdateRecord& record);
    bool WritePendingSignature(const Manifest& manifest);
    bool PublishProgress(const std::string& task_no, int progress_percent,
                         const std::string& step_code, const std::string& detail = {},
                         bool wait_for_ack = false);
    ReportResultOutcome ReportResultHttp(const OtaUpdateRecord& record, bool success);
    void ReportResultUntilSuccess(const OtaUpdateRecord& record, bool success);
    void FailStaging(const std::string& task_no, const std::string& error_code,
                     const std::string& detail);
    std::string SdPath(const char* relative_path) const;

    DeviceCloudConfigService& config_service_;
    FileService* file_service_ = nullptr;
    ProgressPublisher progress_publisher_;
    std::mutex progress_publisher_mutex_;
    std::atomic<bool> busy_{false};
    TaskRetirementOwner download_retirement_owner_;
    TaskRetirementTicket download_retirement_;
    std::string download_payload_;
    std::mutex internal_work_mutex_;
    const std::function<void()>* internal_work_ = nullptr;
    TaskHandle_t internal_work_waiter_ = nullptr;
    TaskHandle_t internal_work_task_ = nullptr;
    std::atomic<bool> staged_handoff_running_{false};
    std::atomic<bool> reporting_{false};
    std::atomic<bool> local_health_confirmed_{false};
    std::atomic<bool> local_boot_confirmed_{false};
    std::atomic<bool> confirming_{false};
    std::atomic<bool> retry_worker_running_{false};
    StaticTimer_t report_retry_timer_storage_ = {};
    TimerHandle_t report_retry_timer_ = nullptr;
    std::mutex report_mutex_;
    std::string acknowledged_task_no_;
};

}  // namespace rodakos

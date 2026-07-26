#pragma once

#include "rodak_ota_state.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

namespace rodakos {

class DeviceCloudConfigService;
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
        uint64_t file_size = 0;
    };

    static void DownloadTask(void* arg);
    static void StagedHandoffTask(void* arg);
    static void ReportTask(void* arg);
    static void RetryTask(void* arg);
    static void ReportRetryTimerCallback(TimerHandle_t timer);
    ConfirmationResult ConfirmRunningImageImpl();
    bool ScheduleStateRetry(const char* reason);
    void RunDownload(const std::string& notification_payload);
    bool CompleteStagedHandoff();
    bool ReportPendingResult();
    bool ClearCompletedRecord(OtaUpdateRecord& record);
    bool RequestUpgradeCheck(const std::string& base_url, const std::string& token,
                             UpgradeCheck& check);
    bool RequestTicket(const std::string& base_url, const std::string& token,
                       const std::string& task_no, std::string& ticket_id);
    bool RequestManifest(const std::string& base_url, const std::string& token,
                         const std::string& task_no, const std::string& ticket_id,
                         Manifest& manifest);
    bool DownloadToSd(const Manifest& manifest, const std::string& token,
                      const std::string& http_base_url);
    bool BackupRunningImage(OtaUpdateRecord& record);
    bool PromotePendingImage(OtaUpdateRecord& record);
    bool VerifySdImage(const char* relative_path, uint64_t expected_size,
                       const std::string& expected_sha256);
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

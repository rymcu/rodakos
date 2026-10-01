#pragma once

#include "rodak_appearance_assets.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace rodakos {

class DeviceCloudConfigService;
class FileService;

struct AppearancePublisherState {
    std::string key_id;
    std::string fingerprint;
    std::string public_key;
    bool trusted = false;
    bool loading = false;
    std::string error;
};

class AppearanceService {
public:
    AppearanceService(DeviceCloudConfigService& cloud, FileService* files);
    ~AppearanceService();
    AppearanceService(const AppearanceService&) = delete;
    AppearanceService& operator=(const AppearanceService&) = delete;

    // Call after Board Manager is initialized. No SD operation runs on the caller.
    bool BeginBootLoad();
    std::shared_ptr<AppearanceBootAssets> WaitBootAssets(uint32_t budget_ms = 1500);
    void RecordAnimationMs(uint32_t duration_ms);
    void ReleaseBootAssets();
    std::shared_ptr<AppearanceBootAssets> GetBootAssets() const;
    bool ConfirmBootHealthy();
    void RejectBootCandidate(const std::string& reason);

    void SetBusyGate(std::function<bool()> gate);
    void SetStatePublisher(std::function<void()> publisher);
    void OnNetworkReady();
    bool ApplyDesiredJson(const std::string& encoded);
    std::string ReportedJson() const;
    bool IsBusy() const { return worker_running_.load(); }

    void RequestPublisher();
    AppearancePublisherState GetPublisherState() const;
    bool ConfirmPublisher(const std::string& key_id);
    bool ForgetPublisher();
    bool SetLocalTheme(const std::string& preset, uint32_t primary);
    bool GetLocalTheme(std::string& preset, uint32_t& primary) const;
    bool ThemeIsLocal() const;

private:
    struct StoredRelease {
        uint32_t revision = 0;
        std::string deployment_id;
        std::string release_id;
        std::string key_id;
        std::string sha256;
        size_t size = 0;
        std::string mode;
        std::string slot;
    };
    static void BootTask(void* arg);
    static void WorkerTask(void* arg);
    void LoadBootAssets();
    void RunWorker();
    void ScheduleWorker();
    bool LoadState();
    bool SaveStateLocked();
    bool FetchPublisher();
    bool DownloadDesired(const std::string& desired);
    void DeferDownload(const std::string& error);
    std::shared_ptr<AppearanceBootAssets> ReadPackage(const StoredRelease& release);
    void SetStatus(const std::string& status, const std::string& error = {}, int progress = -1);
    void NotifyState();
    std::string SdPath(const std::string& relative) const;

    DeviceCloudConfigService& cloud_;
    FileService* files_ = nullptr;
    mutable std::mutex mutex_;
    std::mutex io_mutex_;
    StoredRelease active_;
    StoredRelease pending_;
    StoredRelease previous_;
    uint32_t trial_revision_ = 0;
    uint32_t rejected_revision_ = 0;
    uint32_t selected_revision_ = 0;
    AppearancePublisherState publisher_;
    std::string trusted_key_id_;
    std::string trusted_public_key_;
    std::string trusted_origin_;
    std::string publisher_origin_;
    std::string device_key_;
    std::string desired_;
    uint32_t latest_revision_ = 0;
    unsigned download_failures_ = 0;
    int64_t download_retry_at_ms_ = 0;
    std::string status_ = "idle";
    std::string error_;
    std::string fallback_reason_;
    uint32_t load_ms_ = 0;
    uint32_t animation_ms_ = 0;
    int progress_ = 0;
    bool local_theme_ = false;
    bool boot_candidate_ = false;
    std::string local_preset_;
    uint32_t local_primary_ = 0;
    std::function<bool()> busy_gate_;
    std::function<void()> state_publisher_;
    std::shared_ptr<AppearanceBootAssets> boot_assets_;
    std::atomic<bool> worker_running_{false};
    std::atomic<bool> boot_running_{false};
    std::atomic<bool> boot_playing_{false};
    std::atomic<bool> boot_abandoned_{false};
    std::atomic<bool> publisher_requested_{false};
    int64_t boot_started_ms_ = 0;
    StaticSemaphore_t boot_ready_storage_ = {};
    SemaphoreHandle_t boot_ready_ = nullptr;
};

}  // namespace rodakos

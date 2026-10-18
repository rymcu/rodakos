#pragma once

#include "phone_os/device_cloud_config.h"
#include "phone_os/server_trust.h"
#include "phone_os/task-retirement.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// 无 USB 首次接入（docs/lan-onboarding-design.md 第 3 步，设备端）。
//
// 流程全部在一个 PSRAM 栈的后台任务里完成：发现 → 首次连接记录证书指纹 → 生成 6 位码并
// 发起 binding/request → 轮询直到 Rodak 输码 → SRP 校验与证书绑定 → /trust → 固定证书。
// 之后的 status → token 交给现有的 DeviceCloudConfigService::Refresh。
namespace rodakos {

enum class LanOnboardingPhase {
    kIdle,
    kDiscovering,
    kChooseServer,
    kRequesting,
    // 屏幕显示 code，等待用户在 Rodak 中输入
    kShowCode,
    kVerifying,
    kCompleted,
    kFailed,
};

struct LanOnboardingSnapshot {
    LanOnboardingPhase phase = LanOnboardingPhase::kIdle;
    std::vector<DiscoveredRodakServer> servers;
    std::string code;
    std::string server_name;
    // 失败时给用户看的中文原因
    std::string message;
    int64_t code_expires_at_ms = 0;
    // 连续失败后入口暂时锁定
    int64_t locked_until_ms = 0;
    uint32_t revision = 0;
};

class LanOnboardingService {
public:
    explicit LanOnboardingService(DeviceCloudConfigService& cloud);
    ~LanOnboardingService();
    LanOnboardingService(const LanOnboardingService&) = delete;
    LanOnboardingService& operator=(const LanOnboardingService&) = delete;

    // 未固定服务器且未绑定时才可用（设置页据此决定是否显示入口）。
    bool Available();
    bool StartDiscovery();
    bool Connect(const DiscoveredRodakServer& server);
    // 手动输入的 https://<IPv4>:<port>，用于发现不到的网络（访客网络、AP 隔离）。
    bool ConnectManual(const std::string& address, uint16_t port);
    void Cancel();
    LanOnboardingSnapshot Snapshot() const;

    // 成功固定服务器后回调（在后台任务中调用），用于触发现有的 Refresh。
    void SetCompletedCallback(std::function<void()> callback);

    // 后台任务栈在 PSRAM：NVS/flash 访问会关闭 cache，必须交给内部 RAM 栈的任务执行。
    // 与 OtaUpdateService 相同，由 main 循环所在的常驻任务泵出。
    void SetInternalWorkTask(TaskHandle_t task) { internal_work_task_ = task; }
    void PumpInternalStackWork();

private:
    enum class Job { kDiscover, kConnect };
    bool StartJob(Job job, const DiscoveredRodakServer& server);
    static void TaskBody(void* arg);
    void RunDiscover();
    void RunConnect(const DiscoveredRodakServer& server);
    void Publish(LanOnboardingPhase phase, std::string message = {});
    void RecordFailure(const std::string& message);
    bool Cancelled() const;
    void RunWithInternalStack(const std::function<void()>& work);

    DeviceCloudConfigService& cloud_;
    mutable std::mutex mutex_;
    LanOnboardingSnapshot snapshot_;
    std::function<void()> completed_callback_;
    std::atomic<bool> busy_{false};
    std::atomic<uint32_t> cancel_generation_{0};
    uint32_t job_generation_ = 0;
    Job job_ = Job::kDiscover;
    DiscoveredRodakServer job_server_;
    int consecutive_failures_ = 0;
    TaskRetirementOwner retirement_owner_;
    TaskRetirementTicket retirement_;
    TaskHandle_t internal_work_task_ = nullptr;
    std::mutex internal_work_mutex_;
    const std::function<void()>* internal_work_ = nullptr;
    TaskHandle_t internal_work_waiter_ = nullptr;
};

}  // namespace rodakos

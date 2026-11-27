#pragma once

#include "phone_os/inbox_model.h"
#include "phone_os/inbox_summary.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

struct cJSON;

namespace rodakos {

class DeviceCloudConfigService;
struct DeviceCloudConfig;

// Message center sync (docs/rodak-aiot-contract-v1.md section 7.6). MQTT inbox/changed only marks
// the inbox dirty; one worker task fetches the bounded snapshot and sends actions over pinned
// HTTPS with the device Bearer token. Records live in RAM only and vanish on unbind.
class InboxService final : public InboxSummaryProvider {
public:
    explicit InboxService(DeviceCloudConfigService& cloud);
    ~InboxService();

    InboxService(const InboxService&) = delete;
    InboxService& operator=(const InboxService&) = delete;

    bool Start();
    // Returns true while the device should not open a TLS connection for the inbox, for example
    // during a voice session, a WebRTC stream or an OTA. Checked before each request.
    void SetBusyGate(std::function<bool()> gate);

    // MQTT worker thread: only parses the tiny payload and wakes the worker.
    void OnChangedSignal(const std::string& payload);
    // Wi-Fi or credentials became available, or the user pulled to refresh.
    void RequestSync();
    // LVGL task, from the unbind/rebind callbacks: clear at once, drop late responses.
    void OnBindingChanged(bool bound);

    // UI actions. They only queue work; the result arrives through the view revision.
    void MarkRead(const std::string& notice_id);
    bool Complete(const std::string& notice_id);
    bool Snooze(const std::string& notice_id, uint32_t minutes);
    // Retry a pending reconcile with its stored opId.
    bool Retry(const std::string& notice_id);

    InboxSummary GetInboxSummary() override;
    InboxView GetView();
    uint32_t view_revision();

private:
    struct PendingAction {
        uint32_t binding_epoch = 0;
        InboxActionKind kind = InboxActionKind::kMarkRead;
        std::string notice_id;
    };

    static void TaskEntry(void* arg);
    void Run();
    void Wake();
    bool LoadCloud(DeviceCloudConfig& config, uint32_t& epoch);
    bool Request(DeviceCloudConfig& config, const std::string& path, const std::string& body,
                 cJSON*& root, int& status);
    void SyncOnce();
    void SendAction(const PendingAction& action);
    std::string NewOpId();
    bool QueueAction(const std::string& notice_id, InboxActionKind kind, uint32_t minutes,
                     bool retry);

    DeviceCloudConfigService& cloud_;
    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::function<bool()> busy_gate_;
    InboxModel model_;
    std::deque<PendingAction> actions_;
    // Bumped on every bind change so responses for a previous binding are discarded.
    uint32_t binding_epoch_ = 0;
    bool sync_requested_ = true;
    int64_t next_sync_not_before_us_ = 0;
    uint32_t failures_ = 0;
    uint32_t op_counter_ = 0;
};

}  // namespace rodakos

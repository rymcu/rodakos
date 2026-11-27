#include "phone_os/inbox_service.h"

#include "phone_os/device_cloud_config.h"
#include "phone_os/realtime_voice_contract.h"
#include "phone_os/server_trust_transport.h"

#include <cJSON.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <ctime>
#include <memory>
#include <utility>

#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

namespace rodakos {
namespace {

constexpr const char* TAG = "InboxService";
constexpr const char* kSnapshotPath = "/api/v1/aiot/devices/inbox";
constexpr const char* kActionsPath = "/api/v1/aiot/devices/inbox/actions";
// TLS needs an internal-RAM stack; 7 KiB matches the other pinned-HTTPS workers.
constexpr uint32_t kTaskStackBytes = 7 * 1024;
constexpr UBaseType_t kTaskPriority = 2;
constexpr int kHttpTimeoutMs = 8000;
// The dirty signal is a hint, so polling only covers a lost signal or a missed reconnect.
constexpr int64_t kPollIntervalUs = 15LL * 60 * 1000 * 1000;
constexpr int64_t kBusyRetryUs = 10LL * 1000 * 1000;
constexpr int64_t kMinBackoffUs = 5LL * 1000 * 1000;
constexpr int64_t kMaxBackoffUs = 5LL * 60 * 1000 * 1000;
constexpr size_t kMaxPendingActions = 8;

std::string Origin(const std::string& value) {
    const size_t scheme = value.find("://");
    if ((value.rfind("http://", 0) != 0 && value.rfind("https://", 0) != 0) ||
        scheme == std::string::npos) {
        return {};
    }
    const std::string result = value.substr(0, value.find('/', scheme + 3));
    return result.find_first_of("\r\n@?#") == std::string::npos ? result : std::string();
}

void FreePsram(char* pointer) { heap_caps_free(pointer); }

struct JsonDeleter {
    void operator()(cJSON* value) const { cJSON_Delete(value); }
};
using JsonTree = std::unique_ptr<cJSON, JsonDeleter>;

// One request with the Bearer token. On success root holds the parsed {code:200,data} envelope.
// The response is parsed once; its nesting is checked first because cJSON recurses on this
// task's stack.
bool HttpJson(const DeviceCloudConfig& cloud, const std::string& url, const std::string& body,
              JsonTree& root, int& status) {
    root.reset();
    status = 0;
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = body.empty() ? HTTP_METHOD_GET : HTTP_METHOD_POST;
    config.timeout_ms = kHttpTimeoutMs;
    config.buffer_size = 1024;
    config.buffer_size_tx = 1024;
    config.user_agent = "RodakOS/inbox-v1";
    std::string connect_url;
    if (!ConfigureServerTrustHttp(cloud, url, config, &connect_url)) return false;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) return false;
    if (!ConfigureServerTrustHttpHost(cloud, url, client)) {
        esp_http_client_cleanup(client);
        return false;
    }
    const std::string authorization = "Bearer " + cloud.aiot_access_token;
    esp_http_client_set_header(client, "Authorization", authorization.c_str());
    esp_http_client_set_header(client, "Content-Type", "application/json");
    bool ok = esp_http_client_open(client, static_cast<int>(body.size())) == ESP_OK;
    if (ok && !body.empty()) {
        ok = esp_http_client_write(client, body.data(), static_cast<int>(body.size())) ==
             static_cast<int>(body.size());
    }
    if (ok) ok = esp_http_client_fetch_headers(client) >= 0;
    status = esp_http_client_get_status_code(client);
    ok = ok && status == 200;
    std::unique_ptr<char, decltype(&FreePsram)> buffer(
        ok ? static_cast<char*>(heap_caps_malloc(kInboxMaxResponseBytes + 1,
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
           : nullptr,
        FreePsram);
    int read = -1;
    if (ok && buffer != nullptr) {
        read = esp_http_client_read_response(client, buffer.get(),
                                             static_cast<int>(kInboxMaxResponseBytes + 1));
    }
    ok = ok && read > 0 && static_cast<size_t>(read) <= kInboxMaxResponseBytes;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (!ok) return false;
    {
        // The bounded-depth scan needs a string; it lives only for the check.
        const std::string text(buffer.get(), static_cast<size_t>(read));
        if (!IsBoundedRealtimeVoiceControlJson(text, 8)) {
            ESP_LOGW(TAG, "Inbox response nesting is too deep");
            return false;
        }
    }
    root.reset(cJSON_ParseWithLength(buffer.get(), static_cast<size_t>(read)));
    buffer.reset();
    const cJSON* code = cJSON_GetObjectItemCaseSensitive(root.get(), "code");
    const cJSON* payload = cJSON_GetObjectItemCaseSensitive(root.get(), "data");
    if (!cJSON_IsNumber(code) || code->valueint != 200 || !cJSON_IsObject(payload)) {
        root.reset();
        return false;
    }
    return true;
}

const cJSON* Data(const JsonTree& root) {
    return cJSON_GetObjectItemCaseSensitive(root.get(), "data");
}

class MutexLock {
public:
    explicit MutexLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    ~MutexLock() { xSemaphoreGive(mutex_); }

private:
    SemaphoreHandle_t mutex_;
};

}  // namespace

InboxService::InboxService(DeviceCloudConfigService& cloud) : cloud_(cloud) {
    mutex_ = xSemaphoreCreateMutex();
}

InboxService::~InboxService() {
    if (mutex_ != nullptr) vSemaphoreDelete(mutex_);
}

bool InboxService::Start() {
    if (mutex_ == nullptr || task_ != nullptr) return mutex_ != nullptr;
    // The service lives for the whole uptime, like the other cloud workers.
    if (xTaskCreate(TaskEntry, "inbox_sync", kTaskStackBytes, this, kTaskPriority, &task_) !=
        pdPASS) {
        task_ = nullptr;
        ESP_LOGE(TAG, "Failed to start inbox sync task");
        return false;
    }
    return true;
}

void InboxService::SetBusyGate(std::function<bool()> gate) {
    MutexLock lock(mutex_);
    busy_gate_ = std::move(gate);
}

void InboxService::Wake() {
    if (task_ != nullptr) xTaskNotifyGive(task_);
}

void InboxService::OnChangedSignal(const std::string& payload) {
    InboxChangedSignal signal;
    if (!ParseInboxChangedSignal(payload, signal)) {
        ESP_LOGW(TAG, "Ignoring malformed inbox/changed payload");
        return;
    }
    ESP_LOGI(TAG, "Inbox changed signal: revision=%" PRIu32 " unread=%" PRIu32,
             signal.inbox_revision, signal.unread_count);
    bool wake = false;
    {
        MutexLock lock(mutex_);
        if (model_.ApplyChangedSignal(signal)) {
            sync_requested_ = true;
            next_sync_not_before_us_ = 0;
            wake = true;
        }
    }
    if (wake) Wake();
}

void InboxService::RequestSync() {
    {
        MutexLock lock(mutex_);
        sync_requested_ = true;
        next_sync_not_before_us_ = 0;
    }
    Wake();
}

void InboxService::OnBindingChanged(bool bound) {
    {
        MutexLock lock(mutex_);
        ++binding_epoch_;
        actions_.clear();
        model_.SetBound(false);
        if (bound) model_.SetBound(true);
        sync_requested_ = bound;
        next_sync_not_before_us_ = 0;
        failures_ = 0;
    }
    Wake();
}

std::string InboxService::NewOpId() {
    char text[40] = {};
    std::snprintf(text, sizeof(text), "op-%08" PRIx32 "%08" PRIx32 "-%" PRIu32, esp_random(),
                  esp_random(), ++op_counter_);
    return text;
}

void InboxService::MarkRead(const std::string& notice_id) {
    {
        MutexLock lock(mutex_);
        if (!model_.MarkReadLocally(notice_id) || actions_.size() >= kMaxPendingActions) return;
        actions_.push_back(PendingAction{binding_epoch_, InboxActionKind::kMarkRead, notice_id});
    }
    Wake();
}

bool InboxService::QueueAction(const std::string& notice_id, InboxActionKind kind,
                               uint32_t minutes, bool retry) {
    {
        MutexLock lock(mutex_);
        const InboxItem* item = model_.Find(notice_id);
        if (item == nullptr || actions_.size() >= kMaxPendingActions) return false;
        std::string op_id = item->op_id;
        if (retry) {
            if (item->op_state != InboxOpState::kPendingReconcile) return false;
            kind = item->op_action == "complete" ? InboxActionKind::kComplete
                                                 : InboxActionKind::kSnooze;
            minutes = item->op_snooze_minutes;
        } else {
            op_id = NewOpId();
        }
        if (!model_.BeginAction(notice_id, kind, minutes, op_id)) return false;
        actions_.push_back(PendingAction{binding_epoch_, kind, notice_id});
    }
    Wake();
    return true;
}

bool InboxService::Complete(const std::string& notice_id) {
    return QueueAction(notice_id, InboxActionKind::kComplete, 0, false);
}

bool InboxService::Snooze(const std::string& notice_id, uint32_t minutes) {
    return QueueAction(notice_id, InboxActionKind::kSnooze, minutes, false);
}

bool InboxService::Retry(const std::string& notice_id) {
    return QueueAction(notice_id, InboxActionKind::kComplete, 0, true);
}

InboxSummary InboxService::GetInboxSummary() {
    MutexLock lock(mutex_);
    return model_.Summary();
}

InboxView InboxService::GetView() {
    MutexLock lock(mutex_);
    return model_.View();
}

uint32_t InboxService::view_revision() {
    MutexLock lock(mutex_);
    return model_.revision();
}

void InboxService::TaskEntry(void* arg) {
    static_cast<InboxService*>(arg)->Run();
}

bool InboxService::LoadCloud(DeviceCloudConfig& config, uint32_t& epoch) {
    {
        MutexLock lock(mutex_);
        epoch = binding_epoch_;
    }
    // The bound flag follows the stored identity, so a serial, QR or LAN provisioning, or a
    // failed unbind cleanup, that skipped the Settings callbacks still clears or enables the
    // inbox. An incomplete identity counts as unbound: no records may outlive it.
    const bool bound = cloud_.Load(config) && !config.aiot_access_token.empty() &&
                       !config.mqtt_device_key.empty() &&
                       !Origin(config.mqtt_http_base_url).empty();
    MutexLock lock(mutex_);
    if (epoch != binding_epoch_) return false;
    if (model_.Summary().bound != bound) {
        model_.SetBound(bound);
    }
    return bound;
}

bool InboxService::Request(DeviceCloudConfig& config, const std::string& path,
                           const std::string& body, cJSON*& root, int& status) {
    // The caller owns the returned tree.
    root = nullptr;
    JsonTree tree;
    const std::string origin = Origin(config.mqtt_http_base_url);
    const std::string device_key = config.mqtt_device_key;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        if (!cloud_.IsVoiceConfigCurrent(config)) return false;
        const bool ok = HttpJson(config, origin + path, body, tree, status);
        if (!cloud_.IsVoiceConfigCurrent(config)) return false;
        if (ok) {
            root = tree.release();
            return true;
        }
        if (status != 401 || attempt != 0) return false;
        cloud_.InvalidateAccessTokenFreshness(config.aiot_access_token);
        if (!cloud_.PrepareVoiceConfig(config) || Origin(config.mqtt_http_base_url) != origin ||
            config.mqtt_device_key != device_key) {
            return false;
        }
    }
    return false;
}

void InboxService::SyncOnce() {
    DeviceCloudConfig config;
    uint32_t epoch = 0;
    if (!LoadCloud(config, epoch)) {
        MutexLock lock(mutex_);
        sync_requested_ = false;
        // Without an identity there is nothing to fetch until a bind change or a request.
        next_sync_not_before_us_ = esp_timer_get_time() + kPollIntervalUs;
        return;
    }
    {
        MutexLock lock(mutex_);
        model_.SetSyncing(true);
    }
    cJSON* raw = nullptr;
    int status = 0;
    const bool ok = Request(config, kSnapshotPath, {}, raw, status);
    const JsonTree root(raw);
    InboxSnapshot snapshot;
    std::string error;
    bool parsed = false;
    if (ok) {
        parsed = ParseInboxSnapshot(Data(root), snapshot, error);
        if (!parsed) ESP_LOGW(TAG, "Inbox snapshot rejected: %s", error.c_str());
    }
    MutexLock lock(mutex_);
    model_.SetSyncing(false);
    if (epoch != binding_epoch_) return;
    model_.SetOnline(ok);
    if (parsed) {
        model_.ApplySnapshot(std::move(snapshot), static_cast<int64_t>(std::time(nullptr)));
        sync_requested_ = false;
        failures_ = 0;
        next_sync_not_before_us_ = esp_timer_get_time() + kPollIntervalUs;
        ESP_LOGI(TAG, "Inbox synced: revision=%" PRIu32 " unread=%" PRIu32,
                 model_.inbox_revision(), model_.Summary().unread);
    } else {
        ++failures_;
        const int64_t backoff = std::min<int64_t>(kMaxBackoffUs,
            kMinBackoffUs << std::min<uint32_t>(failures_ - 1, 6));
        next_sync_not_before_us_ = esp_timer_get_time() + backoff;
        ESP_LOGW(TAG, "Inbox sync failed: status=%d attempt=%" PRIu32, status, failures_);
    }
}

void InboxService::SendAction(const PendingAction& action) {
    DeviceCloudConfig config;
    uint32_t epoch = 0;
    std::string body;
    {
        MutexLock lock(mutex_);
        if (action.binding_epoch != binding_epoch_) return;
        const InboxItem* item = model_.Find(action.notice_id);
        if (item == nullptr) return;
        body = BuildInboxActionBody(action.kind, *item,
                                    action.kind == InboxActionKind::kMarkRead ? NewOpId()
                                                                              : item->op_id,
                                    item->op_snooze_minutes);
    }
    const bool loaded = LoadCloud(config, epoch) && epoch == action.binding_epoch;
    cJSON* raw = nullptr;
    int status = 0;
    const bool ok = loaded && !body.empty() && Request(config, kActionsPath, body, raw, status);
    const JsonTree root(raw);
    InboxActionResult result;
    std::string error;
    bool parsed = false;
    if (ok) {
        parsed = ParseInboxActionResult(Data(root), result, error);
        if (!parsed) ESP_LOGW(TAG, "Inbox action result rejected: %s", error.c_str());
    }
    MutexLock lock(mutex_);
    if (action.binding_epoch != binding_epoch_) return;
    model_.SetOnline(ok || status != 0);
    if (action.kind == InboxActionKind::kMarkRead) {
        // A lost read receipt is harmless: the next snapshot shows the server state.
        return;
    }
    if (!parsed) {
        ESP_LOGW(TAG, "Inbox action unconfirmed: kind=%d status=%d", static_cast<int>(action.kind),
                 status);
        model_.FailAction(action.notice_id);
        return;
    }
    model_.FinishAction(action.notice_id, result);
    ESP_LOGI(TAG, "Inbox action done: kind=%d outcome=%d revision=%" PRIu32,
             static_cast<int>(action.kind), static_cast<int>(result.outcome),
             result.inbox_revision);
    if (result.outcome != InboxActionOutcome::kApplied) {
        sync_requested_ = true;
        next_sync_not_before_us_ = 0;
    }
}

void InboxService::Run() {
    while (true) {
        PendingAction action;
        bool has_action = false;
        bool sync_due = false;
        bool busy = false;
        int64_t wait_us = kPollIntervalUs;
        std::function<bool()> gate;
        {
            MutexLock lock(mutex_);
            gate = busy_gate_;
        }
        // The gate reads other services' locks; holding mutex_ here would let a Home or Lock
        // Screen read under the LVGL lock wait behind them.
        busy = gate && gate();
        {
            MutexLock lock(mutex_);
            const int64_t now = esp_timer_get_time();
            if (!busy && !actions_.empty()) {
                action = std::move(actions_.front());
                actions_.pop_front();
                has_action = true;
            } else if (!busy) {
                sync_due = (sync_requested_ || model_.Summary().bound) &&
                           now >= next_sync_not_before_us_;
                if (!sync_due && next_sync_not_before_us_ > now) {
                    wait_us = next_sync_not_before_us_ - now;
                }
            } else {
                wait_us = kBusyRetryUs;
            }
        }
        if (has_action) {
            SendAction(action);
            continue;
        }
        if (sync_due) {
            SyncOnce();
            continue;
        }
        const int64_t wait_ms = std::clamp<int64_t>(wait_us / 1000, 100, 60 * 60 * 1000);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    }
}

}  // namespace rodakos

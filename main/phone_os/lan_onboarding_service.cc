#include "phone_os/lan_onboarding_service.h"

#include "phone_os/trust_setup_protocol.h"
#include "phone_os/trust_setup_transport.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_cpu.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_memory_utils.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <memory>

namespace rodakos {
namespace {

constexpr const char* TAG = "LanOnboarding";
constexpr const char* kRequestPath = "/api/v1/aiot/devices/binding/request";
constexpr const char* kRequestsPrefix = "/api/v1/aiot/devices/binding/requests/";
// 与 Rodak 一致：首次接入申请 5 分钟过期
constexpr int64_t kCodeLifetimeMs = 5 * 60 * 1000;
constexpr int kPollIntervalMs = 1000;
constexpr int kMaxConsecutiveFailures = 3;
constexpr int64_t kLockoutMs = 60 * 1000;
// esp_srp 3072 位运算、TLS 与 cJSON 都在这条栈上；栈放 PSRAM，不占内部堆
constexpr uint32_t kTaskStackBytes = 12 * 1024;

int64_t NowMs() { return esp_timer_get_time() / 1000; }

std::string MacDeviceKey() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buffer[18];
    std::snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
                  mac[2], mac[3], mac[4], mac[5]);
    return buffer;
}

struct Envelope {
    cJSON* root = nullptr;
    const cJSON* data = nullptr;
    int code = 0;
    std::string message;
    ~Envelope() { cJSON_Delete(root); }
};

// Rodak 业务错误以 HTTP 200 + code 字段返回
bool ParseEnvelope(const TrustSetupHttpResult& response, Envelope& envelope) {
    envelope.root = cJSON_ParseWithLength(response.body.c_str(), response.body.size());
    if (!cJSON_IsObject(envelope.root)) return false;
    const cJSON* code = cJSON_GetObjectItemCaseSensitive(envelope.root, "code");
    const cJSON* message = cJSON_GetObjectItemCaseSensitive(envelope.root, "message");
    envelope.code = cJSON_IsNumber(code) ? code->valueint : response.status_code;
    if (cJSON_IsString(message)) envelope.message = message->valuestring;
    envelope.data = cJSON_GetObjectItemCaseSensitive(envelope.root, "data");
    return true;
}

std::string StringField(const cJSON* object, const char* key) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : std::string();
}

std::string BaseUrl(const std::string& address, uint16_t port) {
    return "https://" + address + (port == 443 ? "" : ":" + std::to_string(port));
}

}  // namespace

LanOnboardingService::LanOnboardingService(DeviceCloudConfigService& cloud) : cloud_(cloud) {}

LanOnboardingService::~LanOnboardingService() {
    cancel_generation_.fetch_add(1);
    retirement_owner_.Close();
    retirement_owner_.Drain();
}

bool LanOnboardingService::Available() {
    DeviceCloudConfig config;
    cloud_.Load(config);
    return !config.server_trust_error && config.server_trust.empty() &&
        !(config.aiot_registered && config.aiot_activated);
}

void LanOnboardingService::SetCompletedCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    completed_callback_ = std::move(callback);
}

LanOnboardingSnapshot LanOnboardingService::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

bool LanOnboardingService::StartDiscovery() {
    return StartJob(Job::kDiscover, {});
}

bool LanOnboardingService::Connect(const DiscoveredRodakServer& server) {
    return StartJob(Job::kConnect, server);
}

bool LanOnboardingService::ConnectManual(const std::string& address, uint16_t port) {
    const auto normalized = NormalizeServerRouteAddress(address);
    if (normalized.empty() || normalized.find(':') != std::string::npos || port == 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.phase = LanOnboardingPhase::kFailed;
        snapshot_.message = "请输入有效的 IPv4 地址";
        ++snapshot_.revision;
        return false;
    }
    return StartJob(Job::kConnect, {{}, normalized, normalized, port});
}

void LanOnboardingService::Cancel() {
    cancel_generation_.fetch_add(1);
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.phase = LanOnboardingPhase::kIdle;
    snapshot_.code.clear();
    snapshot_.message.clear();
    ++snapshot_.revision;
}

bool LanOnboardingService::StartJob(Job job, const DiscoveredRodakServer& server) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (snapshot_.locked_until_ms > NowMs()) return false;
    }
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) return false;
    retirement_.Join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job_ = job;
        job_server_ = server;
        job_generation_ = cancel_generation_.load();
        snapshot_.phase = job == Job::kDiscover ? LanOnboardingPhase::kDiscovering
                                                : LanOnboardingPhase::kRequesting;
        snapshot_.code.clear();
        snapshot_.message.clear();
        if (job == Job::kConnect) snapshot_.server_name = server.display_name;
        ++snapshot_.revision;
    }
    retirement_ = ReserveTaskRetirement(retirement_owner_, TaskBody, this);
    TaskHandle_t handle = nullptr;
    const BaseType_t created = !retirement_ ? pdFAIL
        : xTaskCreateWithCaps(TaskRetirementEntry, "lan_onboard", kTaskStackBytes,
                              TaskRetirementContext(retirement_), 4, &handle,
                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        if (retirement_) CancelTaskRetirement(retirement_);
        retirement_.Reset();
        busy_.store(false);
        Publish(LanOnboardingPhase::kFailed, "设备内存不足，请稍后重试");
        return false;
    }
    PublishTaskRetirement(retirement_, handle);
    return true;
}

void LanOnboardingService::TaskBody(void* arg) {
    auto* service = static_cast<LanOnboardingService*>(arg);
    Job job;
    DiscoveredRodakServer server;
    {
        std::lock_guard<std::mutex> lock(service->mutex_);
        job = service->job_;
        server = service->job_server_;
    }
    if (job == Job::kDiscover) service->RunDiscover();
    else service->RunConnect(server);
    service->busy_.store(false);
}

void LanOnboardingService::RunWithInternalStack(const std::function<void()>& work) {
    if (esp_ptr_internal(esp_cpu_get_sp()) || internal_work_task_ == nullptr) {
        work();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(internal_work_mutex_);
        // 只有单个首次接入任务会交出工作，槽位此时必然空闲
        internal_work_ = &work;
        internal_work_waiter_ = xTaskGetCurrentTaskHandle();
    }
    xTaskNotifyGive(internal_work_task_);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

void LanOnboardingService::PumpInternalStackWork() {
    const std::function<void()>* work = nullptr;
    TaskHandle_t waiter = nullptr;
    {
        std::lock_guard<std::mutex> lock(internal_work_mutex_);
        work = internal_work_;
        waiter = internal_work_waiter_;
        internal_work_ = nullptr;
        internal_work_waiter_ = nullptr;
    }
    if (work == nullptr) return;
    (*work)();
    xTaskNotifyGive(waiter);
}

bool LanOnboardingService::Cancelled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cancel_generation_.load() != job_generation_;
}

void LanOnboardingService::Publish(LanOnboardingPhase phase, std::string message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancel_generation_.load() != job_generation_) return;
    snapshot_.phase = phase;
    snapshot_.message = std::move(message);
    if (phase != LanOnboardingPhase::kShowCode && phase != LanOnboardingPhase::kVerifying) {
        snapshot_.code.clear();
    }
    ++snapshot_.revision;
}

void LanOnboardingService::RecordFailure(const std::string& message) {
    ESP_LOGW(TAG, "First setup failed: %s", message.c_str());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (++consecutive_failures_ >= kMaxConsecutiveFailures) {
            consecutive_failures_ = 0;
            snapshot_.locked_until_ms = NowMs() + kLockoutMs;
        }
    }
    Publish(LanOnboardingPhase::kFailed, message);
}

void LanOnboardingService::RunDiscover() {
    auto servers = DiscoverRodakServers();
    if (Cancelled()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.servers = std::move(servers);
    snapshot_.phase = LanOnboardingPhase::kChooseServer;
    snapshot_.message = snapshot_.servers.empty()
        ? "没有找到 Rodak。请确认电脑与设备在同一网络，未开启访客网络或 AP 隔离。"
        : std::string();
    ++snapshot_.revision;
}

void LanOnboardingService::RunConnect(const DiscoveredRodakServer& server) {
    std::string error;
    std::string device_secret;
    std::string client_id;
    bool identity_ready = false;
    std::string identity_error;
    RunWithInternalStack([&]() {
        identity_ready = cloud_.PrepareTrustSetupIdentity(device_secret);
        if (!identity_ready) identity_error = cloud_.last_error();
        else client_id = cloud_.GetClientId();
    });
    if (!identity_ready) {
        Publish(LanOnboardingPhase::kFailed, identity_error);
        return;
    }
    const auto base_url = BaseUrl(server.address, server.port);
    const auto device_key = MacDeviceKey();

    // 第一次握手只为取得服务器证书指纹；之后每次请求都核对握手证书没有变化
    TrustSetupHttpResult probe;
    if (!TrustSetupHttpRequest(base_url, "/api/v1/app/health", "GET", {}, {}, probe, error)) {
        RecordFailure("无法连接到 Rodak，请检查地址与网络");
        return;
    }
    const TrustSetupBytes seen_spki(probe.peer_spki.begin(), probe.peer_spki.end());
    if (!server.server_id.empty()) {
        // mDNS 只是提示；这里只用于提早发现明显不符，认证仍靠证书绑定 HMAC
        static constexpr char kDigits[] = "0123456789abcdef";
        std::string seen_hex;
        for (const auto byte : seen_spki) {
            seen_hex.push_back(kDigits[byte >> 4]);
            seen_hex.push_back(kDigits[byte & 0x0f]);
        }
        if (seen_hex != server.server_id) {
            RecordFailure("服务器证书与广播不一致，已停止连接");
            return;
        }
    }
    const auto same_certificate = [&seen_spki](const TrustSetupHttpResult& response) {
        return response.peer_spki_valid &&
            TrustSetupBytes(response.peer_spki.begin(), response.peer_spki.end()) == seen_spki;
    };

    auto session = TrustSetupSession::Create(seen_spki);
    if (session == nullptr) {
        RecordFailure("设备内存不足，请稍后重试");
        return;
    }
    if (Cancelled()) return;

    cJSON* request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "protocol", kRodakAiotProtocol);
    cJSON_AddNumberToObject(request, "protocolVersion", kRodakAiotProtocolVersion);
    cJSON_AddStringToObject(request, "productKey", kRodakBigSmartProductKey);
    cJSON_AddStringToObject(request, "deviceKey", device_key.c_str());
    cJSON_AddStringToObject(request, "deviceName", "RodakOS RYMCU BigSmart");
    cJSON_AddStringToObject(request, "clientId", client_id.c_str());
    // Rodak 据 application/board 认定这是 RodakOS BigSmart，与现有绑定申请一致
    const esp_app_desc_t* app_desc = esp_app_get_description();
    const char* firmware_version = app_desc != nullptr ? app_desc->version : "unknown";
    cJSON_AddStringToObject(request, "firmwareVersion", firmware_version);
    cJSON_AddStringToObject(request, "hardwareVersion", "rymcu_bigsmart");
    cJSON* application = cJSON_AddObjectToObject(request, "application");
    cJSON_AddStringToObject(application, "name", "rodakos");
    cJSON_AddStringToObject(application, "version", firmware_version);
    cJSON* board = cJSON_AddObjectToObject(request, "board");
    cJSON_AddStringToObject(board, "type", "rymcu_bigsmart");
    cJSON_AddStringToObject(board, "productKey", kRodakBigSmartProductKey);
    cJSON* trust_setup = cJSON_Parse(session->RequestObjectJson().c_str());
    cJSON_AddItemToObject(request, "trustSetup", trust_setup);
    char* printed = cJSON_PrintUnformatted(request);
    const std::string request_body = printed != nullptr ? printed : "";
    cJSON_free(printed);
    cJSON_Delete(request);

    TrustSetupHttpResult created;
    Envelope created_envelope;
    if (trust_setup == nullptr || request_body.empty() ||
        !TrustSetupHttpRequest(base_url, kRequestPath, "POST", request_body, {}, created, error) ||
        !same_certificate(created) || !ParseEnvelope(created, created_envelope)) {
        RecordFailure("发起连接失败，请重试");
        return;
    }
    if (created_envelope.code != 200) {
        RecordFailure(created_envelope.message.empty() ? "Rodak 拒绝了连接请求"
                                                       : created_envelope.message);
        return;
    }
    const auto request_id = StringField(created_envelope.data, "requestId");
    const auto request_token = StringField(created_envelope.data, "requestToken");
    if (request_id.empty() || request_token.empty()) {
        RecordFailure("Rodak 返回的连接请求不完整");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancel_generation_.load() != job_generation_) return;
        snapshot_.code = session->code();
        snapshot_.code_expires_at_ms = NowMs() + kCodeLifetimeMs;
        snapshot_.phase = LanOnboardingPhase::kShowCode;
        snapshot_.message.clear();
        ++snapshot_.revision;
    }
    ESP_LOGI(TAG, "First setup request created; waiting for the code in Rodak");

    const std::string status_path = std::string(kRequestsPrefix) + request_id + "/status";
    const std::string trust_path = std::string(kRequestsPrefix) + request_id + "/trust";
    const int64_t deadline = NowMs() + kCodeLifetimeMs;
    TrustSetupChallenge challenge;
    bool have_challenge = false;
    while (!have_challenge) {
        if (Cancelled()) {
            TrustSetupHttpResult ignored;
            (void)TrustSetupHttpRequest(base_url, trust_path, "POST",
                                        "{\"abort\":\"cancelled\"}", request_token, ignored, error);
            return;
        }
        if (NowMs() >= deadline) {
            RecordFailure("配对码已过期，请重新生成");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
        TrustSetupHttpResult status;
        Envelope envelope;
        if (!TrustSetupHttpRequest(base_url, status_path, "GET", {}, request_token, status, error) ||
            !ParseEnvelope(status, envelope)) {
            continue;
        }
        if (!same_certificate(status)) {
            RecordFailure("服务器证书在连接过程中发生变化，已停止");
            return;
        }
        if (envelope.code != 200) {
            RecordFailure("连接请求已失效，请重新生成配对码");
            return;
        }
        const auto state = StringField(envelope.data, "status");
        if (state == "revoked" || state == "expired") {
            RecordFailure("连接请求已被取消，请重新生成配对码");
            return;
        }
        if (state != "awaiting-device") continue;
        const cJSON* data = cJSON_GetObjectItemCaseSensitive(envelope.data, "trustSetup");
        have_challenge =
            DecodeTrustSetupBase64(StringField(data, "srpA"), 384, 384, challenge.srp_A) &&
            DecodeTrustSetupBase64(StringField(data, "srpM1"), 64, 64, challenge.srp_M1) &&
            DecodeTrustSetupBase64(StringField(data, "serverBind"), 32, 32, challenge.server_bind);
        if (!have_challenge) {
            RecordFailure("Rodak 返回的校验数据无效");
            return;
        }
    }

    Publish(LanOnboardingPhase::kVerifying);
    std::string trust_body;
    const auto answer = session->Answer(request_id, device_key, challenge, device_secret, trust_body);
    if (answer != TrustSetupAnswerResult::kAccepted) {
        const std::string abort = std::string("{\"abort\":\"") + TrustSetupAbortReason(answer) + "\"}";
        TrustSetupHttpResult ignored;
        (void)TrustSetupHttpRequest(base_url, trust_path, "POST", abort, request_token, ignored, error);
        RecordFailure(answer == TrustSetupAnswerResult::kProofMismatch
                          ? "配对码不正确，请重新生成"
                          : answer == TrustSetupAnswerResult::kBindingMismatch
                          ? "服务器身份校验失败，可能存在中间人，已停止"
                          : "校验失败，请重试");
        return;
    }

    TrustSetupHttpResult trusted;
    Envelope trusted_envelope;
    if (!TrustSetupHttpRequest(base_url, trust_path, "POST", trust_body, request_token, trusted,
                               error) ||
        !same_certificate(trusted) || !ParseEnvelope(trusted, trusted_envelope) ||
        trusted_envelope.code != 200) {
        RecordFailure("Rodak 未确认本次连接，请重新生成配对码");
        return;
    }
    const cJSON* sealed_object = cJSON_GetObjectItemCaseSensitive(trusted_envelope.data, "sealed");
    const TrustSetupSealed sealed{StringField(sealed_object, "nonce"),
                                  StringField(sealed_object, "ciphertext")};
    ServerTrust trust;
    std::string rotated_token;
    if (!session->OpenResponse(request_id, sealed, trust, rotated_token, error)) {
        RecordFailure("服务器身份校验失败，已停止");
        return;
    }
    ProvisioningUrlSaveResult saved = ProvisioningUrlSaveResult::kFailedRolledBack;
    std::string save_error;
    RunWithInternalStack([&]() {
        saved = cloud_.SaveTrustSetup(trust, server.port, server.address, request_id,
                                      rotated_token);
        if (saved != ProvisioningUrlSaveResult::kSaved) save_error = cloud_.last_error();
    });
    if (saved != ProvisioningUrlSaveResult::kSaved) {
        RecordFailure(saved == ProvisioningUrlSaveResult::kStateUncertain
                          ? "保存服务器信息失败，请重启设备后重试"
                          : save_error);
        return;
    }
    ESP_LOGI(TAG, "First setup pinned Rodak server %s", trust.tls_name.c_str());
    std::function<void()> callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        consecutive_failures_ = 0;
        callback = completed_callback_;
    }
    Publish(LanOnboardingPhase::kCompleted);
    if (callback) callback();
}

}  // namespace rodakos

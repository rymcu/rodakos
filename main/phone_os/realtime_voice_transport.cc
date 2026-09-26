#include "phone_os/realtime_voice_transport.h"
#include "phone_os/realtime_voice_contract.h"

#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/idf_additions.h>

#include <algorithm>
#include <inttypes.h>
#include <cstring>
#include <utility>
#include <vector>

namespace rodakos {
namespace {
constexpr const char* TAG = "RodakRealtimeVoice";
constexpr EventBits_t kConnectedBit = BIT0;
constexpr EventBits_t kSessionReadyBit = BIT1;
constexpr EventBits_t kErrorBit = BIT2;
constexpr EventBits_t kCancelBit = BIT3;
constexpr int kConnectTimeoutMs = 10000;
constexpr int kSessionReadyTimeoutMs = 10000;
constexpr int kSendTimeoutMs = 3000;
constexpr size_t kMaxInboundAudioMessageSize = 64 * 1024;
constexpr size_t kMaxInboundTextMessageSize = 64 * 1024;
constexpr uint32_t kCleanupTaskStackSize = 4096;
constexpr UBaseType_t kCleanupTaskPriority = 3;

void LogTransportMemory(const char* phase) {
    ESP_LOGI(TAG,
             "Transport memory %s: internal_free=%u internal_min=%u internal_largest=%u "
             "psram_free=%u psram_largest=%u",
             phase,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

class RecursiveSemaphoreLock {
public:
    explicit RecursiveSemaphoreLock(SemaphoreHandle_t semaphore) : semaphore_(semaphore) {
        locked_ = semaphore_ != nullptr &&
                  xSemaphoreTakeRecursive(semaphore_, portMAX_DELAY) == pdTRUE;
    }

    ~RecursiveSemaphoreLock() {
        if (locked_) {
            xSemaphoreGiveRecursive(semaphore_);
        }
    }

    bool locked() const { return locked_; }

private:
    SemaphoreHandle_t semaphore_ = nullptr;
    bool locked_ = false;
};

class SemaphoreLock {
public:
    explicit SemaphoreLock(SemaphoreHandle_t semaphore) : semaphore_(semaphore) {
        locked_ = semaphore_ != nullptr &&
                  xSemaphoreTake(semaphore_, portMAX_DELAY) == pdTRUE;
    }

    ~SemaphoreLock() {
        if (locked_) {
            xSemaphoreGive(semaphore_);
        }
    }

    bool locked() const { return locked_; }

private:
    SemaphoreHandle_t semaphore_ = nullptr;
    bool locked_ = false;
};

std::string MacAddress() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buffer[18];
    std::snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buffer;
}

const char* ListeningModeName(VoiceListeningMode mode) {
    switch (mode) {
        case VoiceListeningMode::kRealtime:
            return "realtime";
        case VoiceListeningMode::kManualStop:
            return "manual-stop";
        case VoiceListeningMode::kAutoStop:
        default:
            return "auto-stop";
    }
}

}  // namespace

RodakRealtimeVoiceTransport::RodakRealtimeVoiceTransport(DeviceCloudConfigService& config_service)
    : config_service_(config_service) {
    mutex_ = xSemaphoreCreateMutex();
    client_mutex_ = xSemaphoreCreateRecursiveMutex();
    open_mutex_ = xSemaphoreCreateMutex();
    frame_mutex_ = xSemaphoreCreateMutex();
    events_ = xEventGroupCreate();
}

RodakRealtimeVoiceTransport::~RodakRealtimeVoiceTransport() {
    CloseAudioChannel();
    if (open_mutex_ != nullptr) {
        xSemaphoreTake(open_mutex_, portMAX_DELAY);
        xSemaphoreGive(open_mutex_);
        vSemaphoreDelete(open_mutex_);
        open_mutex_ = nullptr;
    }
    WaitForAudioChannelClosed();
    if (events_ != nullptr) {
        vEventGroupDelete(events_);
        events_ = nullptr;
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
    if (client_mutex_ != nullptr) {
        vSemaphoreDelete(client_mutex_);
        client_mutex_ = nullptr;
    }
    if (frame_mutex_ != nullptr) {
        vSemaphoreDelete(frame_mutex_);
        frame_mutex_ = nullptr;
    }
}

bool RodakRealtimeVoiceTransport::Start() {
    if (started_) {
        return true;
    }
    if (mutex_ == nullptr || client_mutex_ == nullptr || open_mutex_ == nullptr ||
        frame_mutex_ == nullptr || events_ == nullptr) {
        SetFailure(VoiceTransportFailureKind::kResource,
                   "transport_sync_unavailable",
                   "Voice transport synchronization unavailable", false);
        return false;
    }
    started_ = true;
    return true;
}

bool RodakRealtimeVoiceTransport::PrepareInteraction(VoiceOpenGuard can_continue) {
    if (!Start()) {
        return false;
    }
    SemaphoreLock open_lock(open_mutex_);
    if (!open_lock.locked() || (can_continue && !can_continue())) {
        SetFailure(VoiceTransportFailureKind::kCancelled,
                   "prepare_cancelled", "Voice preparation cancelled", false);
        return false;
    }
    config_prepared_ = false;
    CloseAudioChannel();
    WaitForAudioChannelClosed();
    if (can_continue && !can_continue()) {
        SetFailure(VoiceTransportFailureKind::kCancelled,
                   "prepare_cancelled", "Voice preparation cancelled", false);
        return false;
    }
    // New wakes run on wake_notify's internal stack; reconnect runs in PSRAM.
    // Keep HTTP/NVS work here and use only this RAM snapshot for reconnect.
    if (!config_service_.PrepareVoiceConfig(config_, can_continue)) {
        SetFailure(VoiceTransportFailureKind::kAuthentication,
                   "credential_refresh_failed", config_service_.last_error(), false);
        return false;
    }
    client_id_header_ = config_service_.GetClientId();
    if (can_continue && !can_continue()) {
        SetFailure(VoiceTransportFailureKind::kCancelled,
                   "prepare_cancelled", "Voice preparation cancelled", false);
        return false;
    }
    config_prepared_ = true;
    return true;
}

bool RodakRealtimeVoiceTransport::OpenAudioChannel(VoiceOpenGuard can_continue) {
    if (mutex_ == nullptr || client_mutex_ == nullptr || open_mutex_ == nullptr ||
        frame_mutex_ == nullptr || events_ == nullptr) {
        SetFailure(VoiceTransportFailureKind::kResource,
                   "transport_sync_unavailable",
                   "Voice transport synchronization unavailable", false);
        return false;
    }
    SemaphoreLock open_lock(open_mutex_);
    if (!open_lock.locked()) {
        SetFailure(VoiceTransportFailureKind::kResource,
                   "transport_lock_unavailable",
                   "Transport lock unavailable", false);
        return false;
    }
    if (can_continue && !can_continue()) {
        SetFailure(VoiceTransportFailureKind::kCancelled,
                   "open_cancelled", "Voice websocket open cancelled", false);
        return false;
    }

    CloseAudioChannel();
    WaitForAudioChannelClosed();
    if (can_continue && !can_continue()) {
        SetFailure(VoiceTransportFailureKind::kCancelled,
                   "open_cancelled", "Voice websocket open cancelled", false);
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool cleanup_pending = closing_ || cleanup_in_progress_ ||
                                 cleanup_inline_required_ || cleanup_task_finished_ ||
                                 cleanup_task_ != nullptr || cleanup_client_ != nullptr;
    xSemaphoreGive(mutex_);
    if (cleanup_pending) {
        SetFailure(VoiceTransportFailureKind::kResource,
                   "previous_channel_closing",
                   "Previous realtime voice channel is still closing", true);
        return false;
    }

    uint32_t generation = 0;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    generation = NextRealtimeVoiceGeneration(connection_generation_);
    connection_generation_ = generation;
    closing_ = false;
    connected_ = false;
    channel_open_ = false;
    session_id_.clear();
    session_gate_.Clear();
    vad_strategy_ = kRealtimeVoiceVadServerAuthoritative;
    inbound_playback_epoch_ = 0;
    inbound_output_active_ = false;
    audio_sequence_ = 0;
    inbound_audio_sequence_ = 0;
    inbound_failure_reported_ = false;
    xSemaphoreGive(mutex_);
    xEventGroupClearBits(events_, kConnectedBit | kSessionReadyBit | kErrorBit | kCancelBit);

    if (!IsConnectionCurrent(generation) || (can_continue && !can_continue())) {
        SetFailure(VoiceTransportFailureKind::kCancelled,
                   "open_cancelled", "Realtime voice stream open cancelled", false,
                   generation);
        return false;
    }

    if (!config_prepared_ || !config_service_.IsVoiceConfigCurrent(config_) ||
        config_.aiot_token_expires_at_ms <= esp_timer_get_time() / 1000) {
        SetFailure(VoiceTransportFailureKind::kAuthentication,
                   "access_token_expired", "Wake again to refresh Rodak credentials", false,
                   generation);
        return false;
    }
    if (!config_.has_realtime_voice_config) {
        const std::string error = config_service_.last_error();
        SetFailure(VoiceTransportFailureKind::kConfiguration,
                   "stream_not_configured",
                   error.empty() ? "Realtime voice stream is not configured" : error,
                   false, generation);
        return false;
    }
    if (config_.aiot_access_token.empty()) {
        SetFailure(VoiceTransportFailureKind::kAuthentication,
                   "access_token_missing",
                   "Realtime voice stream requires an AIoT access token", false,
                   generation);
        return false;
    }
    if (!IsConnectionCurrent(generation) || (can_continue && !can_continue())) {
        SetFailure(VoiceTransportFailureKind::kCancelled,
                   "open_cancelled", "Realtime voice stream open cancelled", false,
                   generation);
        return false;
    }

    authorization_header_.clear();
    if (!config_.aiot_access_token.empty()) {
        authorization_header_ = config_.aiot_access_token.find(' ') == std::string::npos
            ? "Bearer " + config_.aiot_access_token
            : config_.aiot_access_token;
    }
    protocol_version_header_ = std::to_string(config_.realtime_voice_protocol_version);
    device_id_header_ = MacAddress();

    headers_.clear();
    if (!authorization_header_.empty()) {
        headers_ += "Authorization: " + authorization_header_ + "\r\n";
    }
    headers_ += "Protocol-Version: " + protocol_version_header_ + "\r\n";
    headers_ += "Device-Id: " + device_id_header_ + "\r\n";
    headers_ += "Client-Id: " + client_id_header_ + "\r\n";

    esp_websocket_client_config_t ws_config = {};
    ws_config.uri = config_.realtime_voice_url.c_str();
    ws_config.headers = headers_.c_str();
    ws_config.disable_auto_reconnect = true;
    ws_config.buffer_size = 4096;
    ws_config.network_timeout_ms = 10000;
    ws_config.reconnect_timeout_ms = 10000;
    ws_config.pingpong_timeout_sec = 30;
    ws_config.task_name = "rodak_voice";
    ws_config.task_stack = 6144;
    ws_config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_websocket_client_handle_t client = esp_websocket_client_init(&ws_config);
    if (client == nullptr) {
        LogTransportMemory("client_init_failed");
        SetFailure(VoiceTransportFailureKind::kResource,
                   "websocket_client_create_failed",
                   "Failed to create websocket client", false, generation);
        return false;
    }
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, EventHandler, this);

    {
        RecursiveSemaphoreLock client_lock(client_mutex_);
        if (!client_lock.locked()) {
            esp_websocket_client_destroy(client);
            SetFailure(VoiceTransportFailureKind::kResource,
                       "transport_lock_unavailable", "Transport lock unavailable", false,
                       generation);
            return false;
        }
        if (!IsConnectionCurrent(generation) || (can_continue && !can_continue())) {
            esp_websocket_client_destroy(client);
            SetFailure(VoiceTransportFailureKind::kCancelled,
                       "open_cancelled", "Realtime voice stream open cancelled", false,
                       generation);
            return false;
        }
        xSemaphoreTake(mutex_, portMAX_DELAY);
        client_ = client;
        xSemaphoreGive(mutex_);
    }

    ESP_LOGI(TAG, "Connecting to Rodak realtime voice stream: %s", config_.realtime_voice_url.c_str());
    esp_err_t err = ESP_FAIL;
    {
        RecursiveSemaphoreLock client_lock(client_mutex_);
        if (!client_lock.locked()) {
            SetFailure(VoiceTransportFailureKind::kResource,
                       "transport_lock_unavailable", "Transport lock unavailable", false,
                       generation);
            CloseAudioChannel();
            return false;
        }
        if (!IsConnectionCurrent(generation) || (can_continue && !can_continue())) {
            SetFailure(VoiceTransportFailureKind::kCancelled,
                       "open_cancelled", "Realtime voice stream open cancelled", false,
                       generation);
            CloseAudioChannel();
            return false;
        }
        LogTransportMemory("before_task_start");
        err = esp_websocket_client_start(client);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Websocket start failed: %s, required_internal_stack=%d",
                 esp_err_to_name(err), ws_config.task_stack);
        LogTransportMemory("task_start_failed");
        SetFailure(VoiceTransportFailureKind::kNetwork,
                   "websocket_start_failed",
                   std::string("Websocket start failed: ") + esp_err_to_name(err),
                   true, generation);
        CloseAudioChannel();
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(
        events_, kConnectedBit | kErrorBit | kCancelBit, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(kConnectTimeoutMs));
    if ((bits & kCancelBit) != 0 || !IsConnectionCurrent(generation) ||
        (can_continue && !can_continue())) {
        SetFailure(VoiceTransportFailureKind::kCancelled,
                   "open_cancelled", "Realtime voice stream open cancelled", false,
                   generation);
        CloseAudioChannel();
        return false;
    }
    if ((bits & kErrorBit) != 0 || (bits & kConnectedBit) == 0) {
        if ((bits & kErrorBit) == 0) {
            SetFailure(VoiceTransportFailureKind::kTimeout,
                       "connect_timeout", "Realtime voice stream connect timeout", true,
                       generation);
        }
        CloseAudioChannel();
        return false;
    }

    if (!IsConnectionCurrent(generation) || (can_continue && !can_continue())) {
        SetFailure(VoiceTransportFailureKind::kCancelled,
                   "open_cancelled", "Realtime voice stream open cancelled", false,
                   generation);
        CloseAudioChannel();
        return false;
    }
    if (!SendSessionOpen(generation)) {
        CloseAudioChannel();
        return false;
    }

    if (!IsConnectionCurrent(generation) || (can_continue && !can_continue())) {
        SetFailure(VoiceTransportFailureKind::kCancelled,
                   "session_open_cancelled", "Realtime voice session open cancelled", false,
                   generation);
        CloseAudioChannel();
        return false;
    }
    bits = xEventGroupWaitBits(events_, kSessionReadyBit | kErrorBit | kCancelBit, pdTRUE, pdFALSE,
                               pdMS_TO_TICKS(kSessionReadyTimeoutMs));
    if ((bits & kCancelBit) != 0 || !IsConnectionCurrent(generation) ||
        (can_continue && !can_continue())) {
        SetFailure(VoiceTransportFailureKind::kCancelled,
                   "session_open_cancelled",
                   "Realtime voice session open cancelled", false, generation);
        CloseAudioChannel();
        return false;
    }
    if ((bits & kErrorBit) != 0 || (bits & kSessionReadyBit) == 0) {
        if ((bits & kErrorBit) == 0) {
            SetFailure(VoiceTransportFailureKind::kTimeout,
                       "session_ready_timeout",
                       "Realtime voice session ready timeout", true, generation);
        }
        CloseAudioChannel();
        return false;
    }

    const bool allowed = !can_continue || can_continue();
    bool current = false;
    {
        RecursiveSemaphoreLock client_lock(client_mutex_);
        if (client_lock.locked() && mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            current = allowed && connection_generation_ == generation && !closing_ &&
                      client_ == client && connected_ &&
                      esp_websocket_client_is_connected(client);
            if (current) {
                channel_open_ = true;
                last_error_.clear();
                last_failure_ = {};
            }
            xSemaphoreGive(mutex_);
        }
    }
    if (!current) {
        SetFailure(allowed ? VoiceTransportFailureKind::kNetwork
                           : VoiceTransportFailureKind::kCancelled,
                   allowed ? "websocket_disconnected" : "session_open_cancelled",
                   allowed ? "Realtime voice stream disconnected during session open"
                           : "Realtime voice session open cancelled",
                   allowed, generation);
        CloseAudioChannel();
        return false;
    }
    return true;
}

void RodakRealtimeVoiceTransport::CloseAudioChannel() {
    if (mutex_ == nullptr || client_mutex_ == nullptr) {
        const auto restore_closed_state = [this]() {
            connection_generation_ = NextRealtimeVoiceGeneration(connection_generation_);
            connected_ = false;
            channel_open_ = false;
            closing_ = false;
            cleanup_in_progress_ = false;
            cleanup_inline_required_ = false;
            cleanup_task_finished_ = false;
            cleanup_task_ = nullptr;
            client_ = nullptr;
            cleanup_client_ = nullptr;
            session_id_.clear();
            session_gate_.Clear();
            inbound_playback_epoch_ = 0;
            inbound_output_active_ = false;
        };
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            restore_closed_state();
            xSemaphoreGive(mutex_);
        } else {
            restore_closed_state();
        }
        if (events_ != nullptr) {
            xEventGroupSetBits(events_, kCancelBit | kErrorBit);
        }
        return;
    }

    esp_websocket_client_handle_t client_to_cleanup = nullptr;
    bool cleanup_started = false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool already_closed = !closing_ && !cleanup_in_progress_ &&
                                !cleanup_inline_required_ && !cleanup_task_finished_ &&
                                cleanup_task_ == nullptr && client_ == nullptr &&
                                cleanup_client_ == nullptr && !connected_ && !channel_open_;
    const bool cleanup_pending = closing_ || cleanup_in_progress_ ||
                                 cleanup_inline_required_ || cleanup_task_finished_ ||
                                 cleanup_task_ != nullptr || cleanup_client_ != nullptr;
    if (already_closed || cleanup_pending) {
        xSemaphoreGive(mutex_);
        if (cleanup_pending && events_ != nullptr) {
            xEventGroupSetBits(events_, kCancelBit | kErrorBit);
        }
        return;
    }
    closing_ = true;
    connection_generation_ = NextRealtimeVoiceGeneration(connection_generation_);
    xSemaphoreGive(mutex_);
    if (events_ != nullptr) {
        xEventGroupSetBits(events_, kCancelBit | kErrorBit);
    }

    {
        // OpenAudioChannel no longer holds this lock while waiting for network events, so
        // cancellation can detach the client even while connect/hello is pending.
        RecursiveSemaphoreLock client_lock(client_mutex_);
        if (!client_lock.locked()) {
            return;
        }

        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            if (!cleanup_in_progress_) {
                cleanup_client_ = client_;
                client_ = nullptr;
            }
            connected_ = false;
            channel_open_ = false;
            session_id_.clear();
            session_gate_.Clear();
            inbound_playback_epoch_ = 0;
            inbound_output_active_ = false;
            if (cleanup_client_ != nullptr && !cleanup_in_progress_) {
                cleanup_in_progress_ = true;
                cleanup_inline_required_ = false;
                cleanup_task_finished_ = false;
                cleanup_started = true;
                client_to_cleanup = cleanup_client_;
            }
            xSemaphoreGive(mutex_);
        } else {
            cleanup_client_ = client_;
            client_ = nullptr;
            cleanup_started = cleanup_client_ != nullptr;
            client_to_cleanup = cleanup_client_;
        }
    }

    if (cleanup_started) {
        esp_websocket_unregister_events(
            client_to_cleanup, WEBSOCKET_EVENT_ANY, EventHandler);
    }

    {
        SemaphoreLock frame_lock(frame_mutex_);
        if (frame_lock.locked()) {
            inbound_frame_.clear();
            inbound_opcode_ = 0;
            inbound_frame_offset_ = 0;
            inbound_message_active_ = false;
        }
    }

    if (cleanup_started) {
        TaskHandle_t cleanup_task = nullptr;
        BaseType_t created = pdFAIL;
        {
            RecursiveSemaphoreLock client_lock(client_mutex_);
            if (client_lock.locked()) {
                // Stop/destroy only release network resources; no flash/NVS calls require an internal stack.
                created = xTaskCreateWithCaps(
                    CleanupTaskEntry, "voice_ws_gc", kCleanupTaskStackSize, this,
                    kCleanupTaskPriority, &cleanup_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (created == pdPASS) {
                    xSemaphoreTake(mutex_, portMAX_DELAY);
                    cleanup_task_ = cleanup_task;
                    xSemaphoreGive(mutex_);
                }
            }
        }
        if (created != pdPASS) {
            LogTransportMemory("cleanup_task_failed");
            // Preserve the connection failure that caused this best-effort cleanup.
            ESP_LOGE(TAG, "Voice websocket cleanup task unavailable");
            xSemaphoreTake(mutex_, portMAX_DELAY);
            cleanup_task_ = nullptr;
            cleanup_task_finished_ = false;
            cleanup_inline_required_ = true;
            xSemaphoreGive(mutex_);
        }
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!cleanup_in_progress_) {
        closing_ = false;
    }
    xSemaphoreGive(mutex_);
}

void RodakRealtimeVoiceTransport::CleanupTaskEntry(void* arg) {
    auto* owner = static_cast<RodakRealtimeVoiceTransport*>(arg);
    if (owner != nullptr) {
        owner->CleanupDetachedClient();
        ESP_LOGI(TAG, "Websocket cleanup stack minimum free: %u bytes",
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        if (owner->mutex_ != nullptr) {
            xSemaphoreTake(owner->mutex_, portMAX_DELAY);
            owner->cleanup_task_finished_ = true;
            xSemaphoreGive(owner->mutex_);
        } else {
            owner->cleanup_task_finished_ = true;
        }
    }
    while (true) {
        vTaskSuspend(nullptr);
    }
}

void RodakRealtimeVoiceTransport::CleanupDetachedClient() {
    esp_websocket_client_handle_t client = nullptr;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        client = cleanup_client_;
        xSemaphoreGive(mutex_);
    } else {
        client = cleanup_client_;
    }

    if (client != nullptr) {
        const int64_t cleanup_started_us = esp_timer_get_time();
        ESP_LOGI(TAG, "Voice websocket cleanup started");
        const esp_err_t stop_result = esp_websocket_client_stop(client);
        const int64_t stop_completed_us = esp_timer_get_time();
        ESP_LOGI(TAG,
                 "Voice websocket stop completed: result=%s duration_ms=%lld",
                 esp_err_to_name(stop_result),
                 static_cast<long long>((stop_completed_us - cleanup_started_us) / 1000));
        const esp_err_t destroy_result = esp_websocket_client_destroy(client);
        const int64_t destroy_completed_us = esp_timer_get_time();
        ESP_LOGI(TAG,
                 "Voice websocket destroy completed: result=%s duration_ms=%lld total_ms=%lld",
                 esp_err_to_name(destroy_result),
                 static_cast<long long>((destroy_completed_us - stop_completed_us) / 1000),
                 static_cast<long long>((destroy_completed_us - cleanup_started_us) / 1000));
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (cleanup_client_ == client) {
            cleanup_client_ = nullptr;
        }
        cleanup_in_progress_ = false;
        cleanup_inline_required_ = false;
        closing_ = false;
        xSemaphoreGive(mutex_);
    } else {
        cleanup_client_ = nullptr;
        cleanup_in_progress_ = false;
        cleanup_inline_required_ = false;
        closing_ = false;
    }
}

void RodakRealtimeVoiceTransport::WaitForAudioChannelClosed() {
    if (mutex_ == nullptr || client_mutex_ == nullptr) {
        return;
    }

    const int64_t wait_started_us = esp_timer_get_time();
    int64_t next_warning_us = wait_started_us + 5 * 1000 * 1000;
    while (true) {
        bool cleanup_inline = false;
        bool complete = false;
        TaskHandle_t cleanup_task = nullptr;
        bool closing = false;
        bool cleanup_in_progress = false;
        bool cleanup_inline_required = false;
        bool cleanup_task_finished = false;
        bool cleanup_client_pending = false;
        {
            RecursiveSemaphoreLock client_lock(client_mutex_);
            if (!client_lock.locked()) {
                return;
            }

            xSemaphoreTake(mutex_, portMAX_DELAY);
            if (cleanup_inline_required_) {
                cleanup_inline_required_ = false;
                cleanup_inline = true;
            } else if (cleanup_task_ != nullptr && cleanup_task_finished_) {
                cleanup_task = cleanup_task_;
            }
            complete = !closing_ && !cleanup_in_progress_ &&
                       !cleanup_inline_required_ && !cleanup_task_finished_ &&
                       cleanup_task_ == nullptr && cleanup_client_ == nullptr;
            closing = closing_;
            cleanup_in_progress = cleanup_in_progress_;
            cleanup_inline_required = cleanup_inline_required_;
            cleanup_task_finished = cleanup_task_finished_;
            cleanup_client_pending = cleanup_client_ != nullptr;
            xSemaphoreGive(mutex_);

            if (cleanup_task != nullptr) {
                if (eTaskGetState(cleanup_task) == eSuspended) {
                    vTaskDeleteWithCaps(cleanup_task);
                    xSemaphoreTake(mutex_, portMAX_DELAY);
                    if (cleanup_task_ == cleanup_task) {
                        cleanup_task_ = nullptr;
                        cleanup_task_finished_ = false;
                    }
                    xSemaphoreGive(mutex_);
                    continue;
                }
            }
            if (cleanup_inline) {
                CleanupDetachedClient();
                continue;
            }
            if (complete) {
                return;
            }
        }
        const int64_t now_us = esp_timer_get_time();
        if (now_us >= next_warning_us) {
            ESP_LOGW(TAG,
                     "Waiting for voice websocket cleanup: elapsed_ms=%lld closing=%d "
                     "in_progress=%d inline=%d task_finished=%d client_pending=%d",
                     static_cast<long long>((now_us - wait_started_us) / 1000),
                     closing, cleanup_in_progress, cleanup_inline_required,
                     cleanup_task_finished, cleanup_client_pending);
            next_warning_us = now_us + 5 * 1000 * 1000;
        }
        vTaskDelay(1);
    }
}

bool RodakRealtimeVoiceTransport::IsAudioChannelOpen() const {
    RecursiveSemaphoreLock client_lock(client_mutex_);
    if (!client_lock.locked() || mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool open = !closing_ && channel_open_ && client_ != nullptr &&
                      esp_websocket_client_is_connected(client_);
    xSemaphoreGive(mutex_);
    return open;
}

uint32_t RodakRealtimeVoiceTransport::connection_generation() const {
    if (mutex_ == nullptr) {
        return 0;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const uint32_t generation = connection_generation_;
    xSemaphoreGive(mutex_);
    return generation;
}

bool RodakRealtimeVoiceTransport::SendAudio(const VoiceAudioPacket& packet,
                                             uint32_t expected_generation) {
    RecursiveSemaphoreLock client_lock(client_mutex_);
    if (!client_lock.locked()) {
        SetFailure(VoiceTransportFailureKind::kResource,
                   "transport_lock_unavailable", "Transport lock unavailable", false,
                   expected_generation);
        return false;
    }
    esp_websocket_client_handle_t client = nullptr;
    bool open = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        client = client_;
        open = !closing_ && channel_open_ && connected_ && client != nullptr &&
               connection_generation_ == expected_generation;
        xSemaphoreGive(mutex_);
    }
    if (!open || !esp_websocket_client_is_connected(client)) {
        SetFailure(VoiceTransportFailureKind::kNetwork,
                   "audio_channel_closed", "Audio channel is not open", true,
                   expected_generation);
        return false;
    }
    if (packet.payload.empty()) {
        return true;
    }
    if (packet.payload.size() > config_.realtime_voice_max_audio_frame_bytes) {
        SetFailure(VoiceTransportFailureKind::kProtocol,
                   "audio_frame_too_large",
                   "Realtime voice audio frame is too large", false,
                   expected_generation);
        return false;
    }

    std::vector<uint8_t> frame(kRealtimeVoiceAudioHeaderBytes + packet.payload.size());
    std::memcpy(frame.data(), kRealtimeVoiceAudioMagic, 4);
    uint32_t sequence = 0;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        // Sequence zero is reserved by the canonical envelope.  Refuse to
        // wrap instead of emitting a frame that the peer must reject.
        if (audio_sequence_ == UINT32_MAX) {
            xSemaphoreGive(mutex_);
            SetFailure(VoiceTransportFailureKind::kProtocol,
                       "audio_sequence_exhausted",
                       "Realtime voice audio sequence exhausted", false,
                       expected_generation);
            return false;
        }
        if (connection_generation_ != expected_generation || closing_ || !channel_open_ ||
            !connected_) {
            xSemaphoreGive(mutex_);
            SetFailure(VoiceTransportFailureKind::kNetwork,
                       "audio_channel_changed",
                       "Realtime voice audio channel changed before send", true,
                       expected_generation);
            return false;
        }
        sequence = ++audio_sequence_;
        xSemaphoreGive(mutex_);
    } else {
        if (audio_sequence_ == UINT32_MAX) {
            SetFailure(VoiceTransportFailureKind::kProtocol,
                       "audio_sequence_exhausted",
                       "Realtime voice audio sequence exhausted", false,
                       expected_generation);
            return false;
        }
        sequence = ++audio_sequence_;
    }
    frame[4] = static_cast<uint8_t>(sequence >> 24);
    frame[5] = static_cast<uint8_t>(sequence >> 16);
    frame[6] = static_cast<uint8_t>(sequence >> 8);
    frame[7] = static_cast<uint8_t>(sequence);
    const uint32_t payload_size = static_cast<uint32_t>(packet.payload.size());
    frame[8] = static_cast<uint8_t>(payload_size >> 24);
    frame[9] = static_cast<uint8_t>(payload_size >> 16);
    frame[10] = static_cast<uint8_t>(payload_size >> 8);
    frame[11] = static_cast<uint8_t>(payload_size);
    std::memcpy(frame.data() + kRealtimeVoiceAudioHeaderBytes,
                packet.payload.data(), packet.payload.size());

    int sent = esp_websocket_client_send_bin(
        client, reinterpret_cast<const char*>(frame.data()), frame.size(),
        pdMS_TO_TICKS(kSendTimeoutMs));
    if (sent != static_cast<int>(frame.size())) {
        SetFailure(VoiceTransportFailureKind::kSend,
                   "audio_send_failed", "Failed to send complete audio packet", true,
                   expected_generation);
        return false;
    }
    return true;
}

bool RodakRealtimeVoiceTransport::SendStartListening(VoiceListeningMode mode,
                                                      uint32_t expected_generation) {
    std::string session_id;
    if (!SnapshotSession(expected_generation, session_id)) {
        SetFailure(VoiceTransportFailureKind::kNetwork,
                   "audio_channel_closed", "Audio channel is not open", true,
                   expected_generation);
        return false;
    }
    const std::string message = BuildRealtimeVoiceInputMessage(
        kRealtimeVoiceEventInputStart, session_id, ListeningModeName(mode));
    const bool sent = SendText(message, expected_generation, session_id, true);
    if (sent) {
        ESP_LOGI(TAG, "Sent speech input start: session=%s mode=%s",
                 session_id.c_str(), ListeningModeName(mode));
    }
    return sent;
}

bool RodakRealtimeVoiceTransport::SendStopListening(uint32_t expected_generation) {
    std::string session_id;
    if (!SnapshotSession(expected_generation, session_id)) {
        SetFailure(VoiceTransportFailureKind::kNetwork,
                   "audio_channel_closed", "Audio channel is not open", true,
                   expected_generation);
        return false;
    }
    const std::string message = BuildRealtimeVoiceInputMessage(
        kRealtimeVoiceEventInputStop, session_id);
    const bool sent = SendText(message, expected_generation, session_id, true);
    if (sent) {
        ESP_LOGI(TAG, "Sent speech input stop: session=%s", session_id.c_str());
    }
    return sent;
}

bool RodakRealtimeVoiceTransport::SendWakeWordDetected(const std::string& wake_word,
                                                        uint32_t expected_generation) {
    std::string session_id;
    if (!SnapshotSession(expected_generation, session_id)) {
        SetFailure(VoiceTransportFailureKind::kNetwork,
                   "audio_channel_closed", "Audio channel is not open", true,
                   expected_generation);
        return false;
    }
    const std::string message = BuildRealtimeVoiceWakeMessage(session_id, wake_word);
    const bool sent = SendText(message, expected_generation, session_id, true);
    if (sent) {
        ESP_LOGI(TAG, "Sent wake event: session=%s text=%s",
                 session_id.c_str(), wake_word.c_str());
    }
    return sent;
}

bool RodakRealtimeVoiceTransport::SendAbortSpeaking(VoiceAbortReason reason,
                                                     uint32_t expected_generation,
                                                     uint32_t playback_epoch) {
    std::string session_id;
    if (!SnapshotSession(expected_generation, session_id)) {
        SetFailure(VoiceTransportFailureKind::kNetwork,
                   "audio_channel_closed", "Audio channel is not open", true,
                   expected_generation);
        return false;
    }
    const std::string message = BuildRealtimeVoicePlaybackAbortMessage(
        session_id, reason, playback_epoch);
    if (message.empty()) {
        SetFailure(VoiceTransportFailureKind::kProtocol,
                   "playback_abort_invalid",
                   "Invalid realtime voice playback abort", false,
                   expected_generation);
        return false;
    }
    return SendText(message, expected_generation, session_id, true);
}

bool RodakRealtimeVoiceTransport::SendVadStart(const char* source, uint32_t sequence,
                                                uint32_t trigger_ms,
                                                uint32_t expected_generation,
                                                uint32_t playback_epoch) {
    return SendVadState("start", source, sequence, trigger_ms, expected_generation, playback_epoch);
}

bool RodakRealtimeVoiceTransport::SendVadEnd(const char* source, uint32_t sequence,
                                              uint32_t trigger_ms, uint32_t expected_generation,
                                              uint32_t playback_epoch) {
    return SendVadState("end", source, sequence, trigger_ms, expected_generation, playback_epoch);
}

bool RodakRealtimeVoiceTransport::SendVadState(const char* state, const char* source,
                                                uint32_t sequence, uint32_t trigger_ms,
                                                uint32_t expected_generation,
                                                uint32_t playback_epoch) {
    std::string session_id;
    if (!SnapshotSession(expected_generation, session_id)) {
        SetFailure(VoiceTransportFailureKind::kNetwork,
                   "audio_channel_closed", "Audio channel is not open", true,
                   expected_generation);
        return false;
    }
    // The server-selected strategy is authoritative for this session. A
    // server-authoritative session still streams audio, but device VAD events
    // are deliberately suppressed so they cannot create a second boundary.
    if (vad_strategy_ == kRealtimeVoiceVadServerAuthoritative) {
        return true;
    }
    const std::string message = BuildRealtimeVoiceVadMessage(
        session_id, state, source, sequence, trigger_ms, playback_epoch);
    return SendText(message, expected_generation, session_id, true);
}

bool RodakRealtimeVoiceTransport::SendMcpMessage(const std::string& payload,
                                                  uint32_t expected_generation) {
    std::string session_id;
    if (!SnapshotSession(expected_generation, session_id)) {
        SetFailure(VoiceTransportFailureKind::kNetwork,
                   "audio_channel_closed", "Audio channel is not open", true,
                   expected_generation);
        return false;
    }

    const std::string message = BuildRealtimeVoiceMcpMessage(session_id, payload);
    if (message.empty()) {
        SetFailure(VoiceTransportFailureKind::kProtocol,
                   "mcp_payload_invalid",
                   "Realtime voice MCP payload must be a JSON object", false,
                   expected_generation);
        return false;
    }
    return SendText(message, expected_generation, session_id, true);
}

void RodakRealtimeVoiceTransport::SetInboundHandler(VoiceInboundHandler handler) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        inbound_handler_ = std::move(handler);
        xSemaphoreGive(mutex_);
    } else {
        inbound_handler_ = std::move(handler);
    }
}

void RodakRealtimeVoiceTransport::EventHandler(void* arg,
                                             esp_event_base_t,
                                             int32_t event_id,
                                             void* event_data) {
    auto* self = static_cast<RodakRealtimeVoiceTransport*>(arg);
    auto* data = static_cast<esp_websocket_event_data_t*>(event_data);
    if (self == nullptr) {
        return;
    }
    uint32_t generation = 0;
    if (self->mutex_ != nullptr) {
        xSemaphoreTake(self->mutex_, portMAX_DELAY);
        const bool current = !self->closing_ && self->client_ != nullptr &&
                             (data == nullptr || data->client == nullptr ||
                              self->client_ == data->client);
        generation = self->connection_generation_;
        xSemaphoreGive(self->mutex_);
        if (!current) {
            return;
        }
    }

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            if (self->mutex_ != nullptr) {
                xSemaphoreTake(self->mutex_, portMAX_DELAY);
                if (self->closing_ || self->connection_generation_ != generation ||
                    self->client_ == nullptr ||
                    (data != nullptr && data->client != nullptr &&
                     self->client_ != data->client)) {
                    xSemaphoreGive(self->mutex_);
                    return;
                }
                self->connected_ = true;
                xSemaphoreGive(self->mutex_);
            }
            xEventGroupSetBits(self->events_, kConnectedBit);
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_CLOSED: {
            bool unexpected_close = false;
            if (self->mutex_ != nullptr) {
                xSemaphoreTake(self->mutex_, portMAX_DELAY);
                if (self->connection_generation_ != generation) {
                    xSemaphoreGive(self->mutex_);
                    return;
                }
                unexpected_close = !self->closing_ && !self->inbound_failure_reported_;
                if (unexpected_close) {
                    self->inbound_failure_reported_ = true;
                }
                self->connected_ = false;
                self->channel_open_ = false;
                self->session_id_.clear();
                self->session_gate_.Clear();
                self->inbound_playback_epoch_ = 0;
                self->inbound_output_active_ = false;
                xSemaphoreGive(self->mutex_);
            }
            if (unexpected_close) {
                const int close_code = data != nullptr ? data->close_status_code : 0;
                VoiceTransportFailure failure =
                    ClassifyVoiceWebsocketCloseFailure(close_code, generation);
                failure = self->SetFailure(
                    failure.kind, failure.code, failure.message, failure.retryable, generation);
                xEventGroupSetBits(self->events_, kErrorBit);
                self->EmitInbound(VoiceInboundEvent{
                    .type = VoiceInboundEventType::kError,
                    .audio = {},
                    .payload = "Voice websocket disconnected",
                    .failure = failure,
                }, generation);
            }
            break;
        }
        case WEBSOCKET_EVENT_ERROR: {
            bool report_failure = false;
            if (self->mutex_ != nullptr) {
                xSemaphoreTake(self->mutex_, portMAX_DELAY);
                if (self->closing_ || self->connection_generation_ != generation) {
                    xSemaphoreGive(self->mutex_);
                    return;
                }
                report_failure = !self->inbound_failure_reported_;
                self->inbound_failure_reported_ = true;
                self->connected_ = false;
                self->channel_open_ = false;
                self->session_id_.clear();
                self->session_gate_.Clear();
                self->inbound_playback_epoch_ = 0;
                self->inbound_output_active_ = false;
                xSemaphoreGive(self->mutex_);
            }
            if (!report_failure) {
                break;
            }
            const int status_code = data != nullptr
                ? data->error_handle.esp_ws_handshake_status_code
                : 0;
            const esp_websocket_error_type_t error_type = data != nullptr
                ? data->error_handle.error_type
                : WEBSOCKET_ERROR_TYPE_NONE;
            VoiceTransportFailure failure = ClassifyVoiceWebsocketErrorFailure(
                status_code, error_type == WEBSOCKET_ERROR_TYPE_PONG_TIMEOUT, generation);
            failure = self->SetFailure(
                failure.kind, failure.code, failure.message, failure.retryable, generation);
            xEventGroupSetBits(self->events_, kErrorBit);
            self->EmitInbound(VoiceInboundEvent{
                .type = VoiceInboundEventType::kError,
                .audio = {},
                .payload = "Websocket error",
                .failure = failure,
            }, generation);
            break;
        }
        case WEBSOCKET_EVENT_DATA:
            if (data != nullptr) {
                self->HandleDataFrame(*data, generation);
            }
            break;
        default:
            break;
    }
}

void RodakRealtimeVoiceTransport::HandleDataFrame(
    const esp_websocket_event_data_t& data,
    uint32_t generation) {
    if (data.data_ptr == nullptr || data.data_len <= 0 || data.payload_len <= 0 ||
        data.payload_offset < 0 || data.payload_offset + data.data_len > data.payload_len) {
        return;
    }

    if (data.op_code >= 0x08) {
        return;
    }

    std::vector<uint8_t> complete_frame;
    uint8_t complete_opcode = 0;
    {
        SemaphoreLock frame_lock(frame_mutex_);
        if (!frame_lock.locked()) {
            return;
        }

        if (data.payload_offset == 0) {
            inbound_frame_offset_ = 0;
            if (data.op_code == WS_TRANSPORT_OPCODES_TEXT ||
                data.op_code == WS_TRANSPORT_OPCODES_BINARY) {
                inbound_frame_.clear();
                inbound_opcode_ = data.op_code;
                inbound_message_active_ = true;
            } else if (data.op_code != WS_TRANSPORT_OPCODES_CONT || !inbound_message_active_) {
                inbound_frame_.clear();
                inbound_opcode_ = 0;
                inbound_message_active_ = false;
                return;
            }
        }

        const size_t message_limit = inbound_opcode_ == WS_TRANSPORT_OPCODES_BINARY
                                         ? kMaxInboundAudioMessageSize
                                         : kMaxInboundTextMessageSize;
        if (!inbound_message_active_ ||
            static_cast<size_t>(data.payload_offset) != inbound_frame_offset_ ||
            inbound_frame_.size() + static_cast<size_t>(data.data_len) > message_limit) {
            inbound_frame_.clear();
            inbound_opcode_ = 0;
            inbound_frame_offset_ = 0;
            inbound_message_active_ = false;
            return;
        }

        const auto* chunk_begin = reinterpret_cast<const uint8_t*>(data.data_ptr);
        inbound_frame_.insert(inbound_frame_.end(),
                              chunk_begin,
                              chunk_begin + static_cast<size_t>(data.data_len));
        inbound_frame_offset_ += static_cast<size_t>(data.data_len);
        if (data.payload_offset + data.data_len < data.payload_len) {
            return;
        }
        inbound_frame_offset_ = 0;
        if (!data.fin) {
            return;
        }

        complete_opcode = inbound_opcode_;
        complete_frame = std::move(inbound_frame_);
        inbound_opcode_ = 0;
        inbound_message_active_ = false;
    }

    if (complete_opcode == WS_TRANSPORT_OPCODES_TEXT) {
        HandleTextFrame(reinterpret_cast<const char*>(complete_frame.data()),
                        static_cast<int>(complete_frame.size()), generation);
    } else if (complete_opcode == WS_TRANSPORT_OPCODES_BINARY) {
        HandleBinaryFrame(complete_frame.data(), complete_frame.size(), generation);
    }
}

bool RodakRealtimeVoiceTransport::SendText(const std::string& text,
                                            uint32_t expected_generation,
                                            const std::string& expected_session,
                                            bool require_open) {
    RecursiveSemaphoreLock client_lock(client_mutex_);
    if (!client_lock.locked()) {
        SetFailure(VoiceTransportFailureKind::kResource,
                   "transport_lock_unavailable", "Transport lock unavailable", false,
                   expected_generation);
        return false;
    }
    esp_websocket_client_handle_t client = nullptr;
    bool connected = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        client = client_;
        connected = !closing_ && connected_ && client != nullptr &&
                    connection_generation_ == expected_generation &&
                    (!require_open ||
                     (channel_open_ && session_id_ == expected_session));
        xSemaphoreGive(mutex_);
    }
    if (!connected || !esp_websocket_client_is_connected(client)) {
        SetFailure(VoiceTransportFailureKind::kNetwork,
                   "websocket_disconnected", "Websocket is not connected", true,
                   expected_generation);
        return false;
    }
    const int sent = esp_websocket_client_send_text(client, text.c_str(), text.size(),
                                                    pdMS_TO_TICKS(kSendTimeoutMs));
    if (sent != static_cast<int>(text.size())) {
        SetFailure(VoiceTransportFailureKind::kSend,
                   "control_send_failed",
                   "Failed to send complete websocket text", true,
                   expected_generation);
        return false;
    }
    ESP_LOGD(TAG, "Sent text: %s", text.c_str());
    return true;
}

bool RodakRealtimeVoiceTransport::SendSessionOpen(uint32_t generation) {
    return SendText(BuildSessionOpenMessage(generation), generation, {}, false);
}

bool RodakRealtimeVoiceTransport::SnapshotSession(uint32_t expected_generation,
                                                   std::string& session_id) const {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool valid = !closing_ && connected_ && channel_open_ && client_ != nullptr &&
                       connection_generation_ == expected_generation &&
                       !session_id_.empty();
    if (valid) {
        session_id = session_id_;
    }
    xSemaphoreGive(mutex_);
    return valid;
}

std::string RodakRealtimeVoiceTransport::BuildSessionOpenMessage(uint32_t generation) const {
    RealtimeVoiceDescriptor descriptor;
    descriptor.endpoint = config_.realtime_voice_url;
    descriptor.protocol_version = config_.realtime_voice_protocol_version;
    descriptor.downlink_sample_rate_hz = config_.realtime_voice_downlink_sample_rate_hz;
    descriptor.downlink_frame_duration_ms = config_.realtime_voice_downlink_frame_duration_ms;
    descriptor.max_audio_frame_bytes = config_.realtime_voice_max_audio_frame_bytes;
    descriptor.max_control_bytes = config_.realtime_voice_max_control_bytes;
    descriptor.vad_strategies = config_.realtime_voice_vad_strategies;
    if (descriptor.vad_strategies.empty()) {
        descriptor.vad_strategies = {kRealtimeVoiceVadServerAuthoritative};
    }
    descriptor.preferred_vad_strategy = config_.realtime_voice_preferred_vad_strategy;
    if (!IsRealtimeVoiceVadStrategy(descriptor.preferred_vad_strategy) ||
        std::find(descriptor.vad_strategies.begin(), descriptor.vad_strategies.end(),
                  descriptor.preferred_vad_strategy) == descriptor.vad_strategies.end()) {
        descriptor.preferred_vad_strategy = kRealtimeVoiceVadServerAuthoritative;
    }
    const bool supports_device_vad_epoch =
#if CONFIG_USE_DEVICE_AEC
        true;
#else
        false;
#endif
    return BuildRealtimeVoiceSessionOpenMessage(descriptor, false,
                                                supports_device_vad_epoch,
                                                generation);
}

void RodakRealtimeVoiceTransport::HandleTextFrame(const char* data,
                                                   int len,
                                                   uint32_t generation) {
    if (data == nullptr || len <= 0 ||
        static_cast<size_t>(len) > config_.realtime_voice_max_control_bytes) {
        if (data != nullptr && len > 0) {
            SetError("Realtime voice control frame is too large", generation);
        }
        return;
    }
    std::string payload(data, len);
    cJSON* root = cJSON_Parse(payload.c_str());
    if (root == nullptr) {
        ESP_LOGW(TAG, "Invalid JSON from voice cloud: %.*s", len, data);
        SetError("Invalid JSON from voice cloud", generation);
        return;
    }

    cJSON* event = cJSON_GetObjectItem(root, "event");
    if (!cJSON_IsString(event) || event->valuestring == nullptr ||
        event->valuestring[0] == '\0') {
        cJSON_Delete(root);
        SetError("Realtime voice control frame is missing a canonical event", generation);
        return;
    }

    const char* event_name = event->valuestring;
    const bool is_session_ready =
        std::strcmp(event_name, kRealtimeVoiceEventSessionReady) == 0;
    const bool is_output_start =
        std::strcmp(event_name, kRealtimeVoiceEventOutputStart) == 0;
    const bool is_output_stop =
        std::strcmp(event_name, kRealtimeVoiceEventOutputStop) == 0;
    const bool is_session_end =
        std::strcmp(event_name, kRealtimeVoiceEventSessionEnd) == 0;
    const bool is_mcp = std::strcmp(event_name, kRealtimeVoiceEventMcp) == 0;
    const bool is_error = std::strcmp(event_name, kRealtimeVoiceEventError) == 0;
    if (!is_session_ready && !is_output_start && !is_output_stop && !is_session_end &&
        !is_mcp && !is_error) {
        cJSON_Delete(root);
        SetError("Unsupported realtime voice control event", generation);
        return;
    }

    std::string payload_error;
    if (!ValidateRealtimeVoiceServerControlPayload(root, payload_error)) {
        cJSON_Delete(root);
        SetError(payload_error, generation);
        xEventGroupSetBits(events_, kErrorBit);
        return;
    }

    if (!is_session_ready) {
        const cJSON* session_id = cJSON_GetObjectItemCaseSensitive(root, "sessionId");
        bool session_matches = false;
        if (is_error && session_id == nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            session_matches = !closing_ && connection_generation_ == generation;
            xSemaphoreGive(mutex_);
        } else if (cJSON_IsString(session_id) && session_id->valuestring != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            session_matches = !closing_ && session_id_ == session_id->valuestring &&
                              connection_generation_ == generation;
            xSemaphoreGive(mutex_);
        }
        if (!session_matches) {
            cJSON_Delete(root);
            SetError("Realtime voice control frame sessionId does not match", generation);
            return;
        }
    }

    if (is_session_ready) {
        ParseSessionReady(payload, generation);
    } else if (is_output_start || is_output_stop) {
        const cJSON* epoch_value = cJSON_GetObjectItemCaseSensitive(root, "playbackEpoch");
        uint32_t epoch = 0;
        if (epoch_value != nullptr) {
            if (!cJSON_IsNumber(epoch_value) || !(epoch_value->valuedouble >= 1 &&
                epoch_value->valuedouble <= UINT32_MAX)) {
                cJSON_Delete(root);
                ESP_LOGW(TAG, "Invalid realtime voice playback epoch");
                return;
            }
            epoch = static_cast<uint32_t>(epoch_value->valuedouble);
            if (epoch_value->valuedouble != static_cast<double>(epoch)) {
                cJSON_Delete(root);
                return;
            }
        }
        if (is_output_start) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            if (closing_ || connection_generation_ != generation ||
                !session_gate_.AcceptOutputStart(generation, epoch)) {
                xSemaphoreGive(mutex_);
                cJSON_Delete(root);
                return;
            }
            inbound_playback_epoch_ = session_gate_.playback_epoch();
            inbound_output_active_ = true;
            xSemaphoreGive(mutex_);
            ESP_LOGI(TAG, "Received realtime voice output start: playback_epoch=%u",
                     static_cast<unsigned>(epoch));
            EmitInbound(VoiceInboundEvent{
                .type = VoiceInboundEventType::kSpeakingStarted,
                .playback_epoch = epoch,
                .audio = {},
                .payload = {},
                .failure = {},
            }, generation);
        } else {
            // A delayed stop from an older playback must not terminate a
            // newer utterance.  Zero remains the wire-compatible value for
            // servers that omit playbackEpoch on output.stop.
            xSemaphoreTake(mutex_, portMAX_DELAY);
            const bool stale = closing_ || connection_generation_ != generation ||
                               !inbound_output_active_ ||
                               !session_gate_.AcceptOutputStop(generation, epoch);
            if (!stale) inbound_output_active_ = false;
            xSemaphoreGive(mutex_);
            if (stale) {
                cJSON_Delete(root);
                return;
            }
            EmitInbound(VoiceInboundEvent{
                .type = VoiceInboundEventType::kSpeakingStopped,
                .playback_epoch = epoch,
                .audio = {},
                .payload = {},
                .failure = {},
            }, generation);
        }
    } else if (is_session_end) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (closing_ || connection_generation_ != generation) {
            xSemaphoreGive(mutex_);
            cJSON_Delete(root);
            return;
        }
        channel_open_ = false;
        session_id_.clear();
        inbound_playback_epoch_ = 0;
        inbound_output_active_ = false;
        session_gate_.Clear();
        xSemaphoreGive(mutex_);
        EmitInbound(VoiceInboundEvent{
            .type = VoiceInboundEventType::kSessionFinished,
            .audio = {},
            .payload = {},
            .failure = {},
        }, generation);
    } else if (is_mcp) {
        if (!IsRealtimeVoiceMcpPayloadObject(root)) {
            cJSON_Delete(root);
            SetError("Invalid realtime voice MCP payload", generation);
            return;
        }
        EmitInbound(VoiceInboundEvent{
            .type = VoiceInboundEventType::kMcp,
            .audio = {},
            .payload = payload,
            .failure = {},
        }, generation);
    } else if (is_error) {
        RealtimeVoiceServerError server_error;
        std::string server_error_parse_failure;
        if (!ParseRealtimeVoiceServerError(root, server_error,
                                           server_error_parse_failure)) {
            cJSON_Delete(root);
            const VoiceTransportFailure failure = SetFailure(
                VoiceTransportFailureKind::kProtocol, "server_error_invalid",
                server_error_parse_failure.empty()
                    ? "Invalid realtime voice server error"
                    : server_error_parse_failure,
                false, generation);
            xEventGroupSetBits(events_, kErrorBit);
            EmitInbound(VoiceInboundEvent{
                .type = VoiceInboundEventType::kError,
                .audio = {},
                .payload = {},
                .failure = failure,
            }, generation);
            return;
        }
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (closing_ || connection_generation_ != generation) {
            xSemaphoreGive(mutex_);
            cJSON_Delete(root);
            return;
        }
        if (inbound_failure_reported_) {
            xSemaphoreGive(mutex_);
            cJSON_Delete(root);
            return;
        }
        inbound_failure_reported_ = true;
        channel_open_ = false;
        session_id_.clear();
        inbound_playback_epoch_ = 0;
        inbound_output_active_ = false;
        session_gate_.Clear();
        xSemaphoreGive(mutex_);
        const VoiceTransportFailure failure = SetFailure(
            VoiceTransportFailureKind::kServer, server_error.code, server_error.message,
            server_error.retryable, generation);
        xEventGroupSetBits(events_, kErrorBit);
        EmitInbound(VoiceInboundEvent{
            .type = VoiceInboundEventType::kError,
            .audio = {},
            .payload = payload,
            .failure = failure,
        }, generation);
    }
    cJSON_Delete(root);
}

void RodakRealtimeVoiceTransport::HandleBinaryFrame(const uint8_t* data,
                                                     size_t size,
                                                     uint32_t generation) {
    VoiceAudioPacket packet;
    uint32_t playback_epoch = 0;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (closing_ || connection_generation_ != generation || session_id_.empty() ||
            !inbound_output_active_ || !session_gate_.Matches(generation, session_id_)) {
            xSemaphoreGive(mutex_);
            return;
        }
        playback_epoch = inbound_playback_epoch_;
        packet.sample_rate = server_sample_rate_;
        packet.frame_duration_ms = server_frame_duration_ms_;
        xSemaphoreGive(mutex_);
    } else {
        packet.sample_rate = server_sample_rate_;
        packet.frame_duration_ms = server_frame_duration_ms_;
    }
    const size_t max_audio_frame_bytes =
        std::min(config_.realtime_voice_max_audio_frame_bytes, kMaxInboundAudioMessageSize);
    RealtimeVoiceAudioFrame frame;
    std::string frame_error;
    if (!ParseRealtimeVoiceAudioFrame(data, size, max_audio_frame_bytes, frame, frame_error)) {
        SetError(frame_error.empty() ? "Invalid realtime voice audio frame" : frame_error,
                 generation);
        return;
    }
    const uint32_t sequence = frame.sequence;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (closing_ || connection_generation_ != generation || session_id_.empty() ||
            !session_gate_.AcceptAudio(generation, sequence)) {
            xSemaphoreGive(mutex_);
            SetError("Invalid realtime voice audio sequence", generation);
            return;
        }
        inbound_audio_sequence_ = session_gate_.audio_sequence();
        xSemaphoreGive(mutex_);
    } else {
        if (sequence == 0 || sequence <= inbound_audio_sequence_) {
            SetError("Invalid realtime voice audio sequence", generation);
            return;
        }
        inbound_audio_sequence_ = sequence;
    }
    packet.timestamp_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    packet.payload.assign(frame.payload, frame.payload + frame.payload_size);
    EmitInbound(VoiceInboundEvent{
        .type = VoiceInboundEventType::kAudio,
        .playback_epoch = playback_epoch,
        .audio = std::move(packet),
        .payload = {},
        .failure = {},
    }, generation);
}

void RodakRealtimeVoiceTransport::ParseSessionReady(const std::string& payload,
                                                     uint32_t generation) {
    cJSON* root = cJSON_Parse(payload.c_str());
    if (root == nullptr) {
        SetError("Invalid realtime voice session.ready event", generation);
        xEventGroupSetBits(events_, kErrorBit);
        return;
    }

    cJSON* event = cJSON_GetObjectItemCaseSensitive(root, "event");
    cJSON* protocol = cJSON_GetObjectItemCaseSensitive(root, "protocol");
    cJSON* protocol_version = cJSON_GetObjectItemCaseSensitive(root, "protocolVersion");
    cJSON* generation_value = cJSON_GetObjectItemCaseSensitive(root, "generation");
    cJSON* transport = cJSON_GetObjectItemCaseSensitive(root, "transport");
    cJSON* vad_strategy = cJSON_GetObjectItemCaseSensitive(root, "vadStrategy");
    if (!cJSON_IsString(event) ||
        std::strcmp(event->valuestring, kRealtimeVoiceEventSessionReady) != 0 ||
        !cJSON_IsString(protocol) ||
        std::strcmp(protocol->valuestring, kRodakRealtimeVoiceProtocol) != 0 ||
        !cJSON_IsNumber(protocol_version) ||
        protocol_version->valuedouble != kRodakRealtimeVoiceProtocolVersion ||
        !cJSON_IsNumber(generation_value) || generation == 0 ||
        generation_value->valuedouble != static_cast<double>(generation) ||
        !cJSON_IsString(transport) ||
        std::strcmp(transport->valuestring, kRealtimeVoiceTransportWebSocket) != 0) {
        cJSON_Delete(root);
        SetError("Unsupported realtime voice session.ready event", generation);
        xEventGroupSetBits(events_, kErrorBit);
        return;
    }

    const bool valid_vad_strategy =
        vad_strategy == nullptr ||
        (cJSON_IsString(vad_strategy) && vad_strategy->valuestring != nullptr &&
         IsRealtimeVoiceVadStrategy(vad_strategy->valuestring));
    if (!valid_vad_strategy) {
        cJSON_Delete(root);
        SetError("Invalid realtime voice VAD strategy", generation);
        xEventGroupSetBits(events_, kErrorBit);
        return;
    }

    cJSON* session_id = cJSON_GetObjectItem(root, "sessionId");
    cJSON* downlink = cJSON_GetObjectItem(root, "downlink");
    cJSON* format = cJSON_IsObject(downlink)
                        ? cJSON_GetObjectItem(downlink, "codec")
                        : nullptr;
    cJSON* channels = cJSON_IsObject(downlink)
                          ? cJSON_GetObjectItem(downlink, "channels")
                          : nullptr;
    cJSON* sample_rate = cJSON_IsObject(downlink)
                             ? cJSON_GetObjectItem(downlink, "sampleRateHz")
                             : nullptr;
    cJSON* frame_duration = cJSON_IsObject(downlink)
                                ? cJSON_GetObjectItem(downlink, "frameDurationMs")
                                : nullptr;

    const bool valid_session = cJSON_IsString(session_id) &&
                               session_id->valuestring != nullptr &&
                               session_id->valuestring[0] != '\0';
    const bool valid_format = cJSON_IsString(format) &&
                              std::strcmp(format->valuestring, "opus") == 0;
    const bool valid_channels = cJSON_IsNumber(channels) && channels->valuedouble == 1;
    const int negotiated_sample_rate = cJSON_IsNumber(sample_rate) &&
                                               sample_rate->valuedouble ==
                                                   static_cast<double>(sample_rate->valueint)
                                           ? sample_rate->valueint
                                           : 0;
    const int negotiated_frame_duration = cJSON_IsNumber(frame_duration) &&
                                                  frame_duration->valuedouble ==
                                                      static_cast<double>(frame_duration->valueint)
                                              ? frame_duration->valueint
                                              : 0;
    if (!valid_session || !valid_format || !valid_channels ||
        (negotiated_sample_rate != 8000 && negotiated_sample_rate != 12000 &&
         negotiated_sample_rate != 16000 && negotiated_sample_rate != 24000 &&
         negotiated_sample_rate != 48000) ||
        !IsSupportedRealtimeVoiceFrameDuration(negotiated_frame_duration)) {
        cJSON_Delete(root);
        SetError("Invalid realtime voice session.ready parameters", generation);
        xEventGroupSetBits(events_, kErrorBit);
        return;
    }

    const std::string negotiated_session_id = session_id->valuestring;
    const std::string negotiated_vad_strategy =
        vad_strategy != nullptr ? vad_strategy->valuestring : kRealtimeVoiceVadServerAuthoritative;
    if (!config_.realtime_voice_vad_strategies.empty() &&
        std::find(config_.realtime_voice_vad_strategies.begin(),
                  config_.realtime_voice_vad_strategies.end(), negotiated_vad_strategy) ==
            config_.realtime_voice_vad_strategies.end()) {
        cJSON_Delete(root);
        SetError("Server selected an unsupported realtime voice VAD strategy", generation);
        xEventGroupSetBits(events_, kErrorBit);
        return;
    }
    cJSON_Delete(root);

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (closing_ || client_ == nullptr || connection_generation_ != generation) {
            xSemaphoreGive(mutex_);
            return;
        }
        if (!session_gate_.Establish(generation, negotiated_session_id)) {
            xSemaphoreGive(mutex_);
            SetError("Duplicate realtime voice session.ready event", generation);
            xEventGroupSetBits(events_, kErrorBit);
            return;
        }
        session_id_ = negotiated_session_id;
        inbound_playback_epoch_ = 0;
        inbound_output_active_ = false;
        server_sample_rate_ = negotiated_sample_rate;
        server_frame_duration_ms_ = negotiated_frame_duration;
        vad_strategy_ = negotiated_vad_strategy;
        xSemaphoreGive(mutex_);
    } else {
        if (!session_gate_.Establish(generation, negotiated_session_id)) {
            SetError("Duplicate realtime voice session.ready event", generation);
            xEventGroupSetBits(events_, kErrorBit);
            return;
        }
        session_id_ = negotiated_session_id;
        server_sample_rate_ = negotiated_sample_rate;
        server_frame_duration_ms_ = negotiated_frame_duration;
        vad_strategy_ = negotiated_vad_strategy;
    }
    ESP_LOGI(TAG, "Realtime voice session ready: session=%s sample_rate=%d frame=%d",
             negotiated_session_id.c_str(), negotiated_sample_rate,
             negotiated_frame_duration);
    xEventGroupSetBits(events_, kSessionReadyBit);
}

VoiceTransportFailure RodakRealtimeVoiceTransport::SetFailure(
    VoiceTransportFailureKind kind, const std::string& code, const std::string& message,
    bool retryable, uint32_t generation) {
    const std::string normalized = message.empty() ? "Voice cloud transport error" : message;
    uint32_t resolved_generation = generation;
    VoiceTransportFailure failure;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (resolved_generation == 0) {
            resolved_generation = connection_generation_;
        }
        failure = {
            .kind = kind,
            .code = code,
            .message = normalized,
            .retryable = retryable,
            .transport_generation = resolved_generation,
        };
        if (generation == 0 || resolved_generation == connection_generation_) {
            last_error_ = normalized;
            last_failure_ = failure;
        }
        xSemaphoreGive(mutex_);
    } else {
        last_error_ = normalized;
        failure = {
            .kind = kind,
            .code = code,
            .message = normalized,
            .retryable = retryable,
            .transport_generation = generation,
        };
        last_failure_ = failure;
    }
    ESP_LOGW(TAG, "%s: code=%s retryable=%d generation=%" PRIu32,
             normalized.c_str(), code.c_str(), retryable ? 1 : 0,
             resolved_generation);
    return failure;
}

bool RodakRealtimeVoiceTransport::ClaimInboundFailure(uint32_t generation) {
    if (mutex_ == nullptr || generation == 0) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool claimed = !closing_ && connection_generation_ == generation &&
                         !inbound_failure_reported_;
    if (claimed) {
        inbound_failure_reported_ = true;
    }
    xSemaphoreGive(mutex_);
    return claimed;
}

void RodakRealtimeVoiceTransport::SetError(const std::string& message,
                                           uint32_t generation) {
    if (!ClaimInboundFailure(generation)) {
        return;
    }
    const VoiceTransportFailure failure = SetFailure(
        VoiceTransportFailureKind::kProtocol, "protocol_error", message, false, generation);
    if (events_ != nullptr) {
        xEventGroupSetBits(events_, kErrorBit);
    }
    EmitInbound(VoiceInboundEvent{
        .type = VoiceInboundEventType::kError,
        .audio = {},
        .payload = failure.message,
        .failure = failure,
    }, failure.transport_generation);
}

std::string RodakRealtimeVoiceTransport::last_error() const {
    if (mutex_ == nullptr) {
        return last_error_;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const std::string error = last_error_;
    xSemaphoreGive(mutex_);
    return error;
}

VoiceTransportFailure RodakRealtimeVoiceTransport::last_failure() const {
    if (mutex_ == nullptr) {
        return last_failure_;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const VoiceTransportFailure failure = last_failure_;
    xSemaphoreGive(mutex_);
    return failure;
}

bool RodakRealtimeVoiceTransport::IsConnectionCurrent(uint32_t generation) const {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool current = connection_generation_ == generation && !closing_;
    xSemaphoreGive(mutex_);
    return current;
}

void RodakRealtimeVoiceTransport::EmitInbound(VoiceInboundEvent&& event,
                                               uint32_t generation) {
    event.transport_generation = generation;
    VoiceInboundHandler handler;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (!closing_ && generation != 0 && connection_generation_ == generation) {
            handler = inbound_handler_;
        }
        xSemaphoreGive(mutex_);
    } else {
        handler = inbound_handler_;
    }
    if (handler) {
        handler(std::move(event));
    }
}

}  // namespace rodakos

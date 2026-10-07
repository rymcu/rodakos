#pragma once

#include "phone_os/device_cloud_config.h"
#include "phone_os/battery_monitor.h"
#include "phone_os/light_service.h"
#include "phone_os/mqtt_credential_refresh_policy.h"
#include "phone_os/mqtt_volume_effect.h"
#include "phone_os/mqtt_light_effect.h"
#include "phone_os/mqtt_command_ledger.h"
#include "phone_os/stream_lease.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <esp_event.h>
#include <mqtt_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/timers.h>

namespace rodakos {

class AudioOutputService;
class DeviceCloudConfigService;
class OtaUpdateService;
class VoiceWakeService;
struct VoiceWakeState;
class WebRtcCameraService;
class WebRtcDisplayService;
class AppearanceService;

class UnifiedMqttService {
public:
    using DisplayControlReply = std::function<void(bool accepted, const char* reason)>;
    using DisplayControlCallback = std::function<void(const StreamLeasePtr& lease,
        const std::string& payload, DisplayControlReply reply)>;
    UnifiedMqttService(DeviceCloudConfigService& config_service,
                       OtaUpdateService& ota_update,
                       AudioOutputService* audio_output,
                       BatteryStateProvider* battery_provider = nullptr,
                       LightService* light_service = nullptr);
    ~UnifiedMqttService();

    bool Start();
    void Stop();
    void ReconnectAfterCredentialChange();
    // Apply a provisioning change. An active session restarts so its outbox
    // and event workers cannot cross broker or routing boundaries.
    void RequestCredentialRefresh();
    void SetVoiceWakeService(VoiceWakeService* voice_wake) { voice_wake_ = voice_wake; }
    // The camera peer is injected after the camera hardware service has been
    // constructed. MQTT remains the control plane; JPEG payloads never pass
    // through MQTT.
    void SetWebRtcCameraService(WebRtcCameraService* camera_peer) {
        web_rtc_camera_service_ = camera_peer;
    }
    void StopWebRtcCameraStream();
    void SetWebRtcDisplayService(WebRtcDisplayService* display_peer) { web_rtc_display_service_ = display_peer; }
    void SetWebRtcDisplayControlCallback(DisplayControlCallback callback) {
        display_control_callback_ = std::move(callback);
    }
    void StopWebRtcDisplayStream();
    void SetAppearanceService(AppearanceService* appearance);
    bool IsConnected() const { return connected_.load(); }
    bool Publish(const std::string& topic, const std::string& payload);

private:
    struct ClientInstance {
        UnifiedMqttService* service = nullptr;
        esp_mqtt_client_handle_t handle = nullptr;
        uint32_t generation = 0;
        bool sdk_started = false;
        // ESP-MQTT borrows these pointers until the old task has fully exited.
        ServerTrust tls_trust;
        std::string broker_uri;
        std::string client_id;
    };

    struct ControlGateTiming {
        uint64_t callback_no = 0;
        uint64_t instance_nonce = 0;
        int64_t started_us = 0;
        int64_t acquired_us = 0;
        int64_t checked_us = 0;
        bool current = false;
    };
    struct PublishedEvent {
        uint32_t client_generation = 0;
        uint64_t sequence = 0;
        int message_id = -1;
    };

    struct ReliablePublishState {
        uint32_t client_generation = 0;
        int message_id = -1;
        bool acknowledged = false;
        std::string topic;
        std::string payload;
    };

    struct CommandPublishContext {
        uint32_t client_generation = 0;
        uint64_t connection_epoch = 0;
        std::string ack_topic;
    };

    struct PendingCommandPublication {
        CommandPublishContext context;
        std::string payload;
        StreamLeasePtr stream_lease;
        bool terminal_stream_event = false;
        bool display_stream = false;
    };

    static void NetworkEventHandler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);
    static void MqttEventHandler(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data);
    static void WorkerTask(void* arg);
    void WorkerLoop();
    void RunConnection();
    void RefreshCredentials();
    void OnConnected(uint32_t generation);
    static void TelemetryTimerCallback(TimerHandle_t timer);

    void StartConnectionAsync();
    void Connect();
    std::unique_ptr<DeviceCloudConfig> LoadMqttSnapshot();
    bool StartClient(DeviceCloudConfig& config);
    bool DestroyClient(std::unique_ptr<ClientInstance> client);
    bool RetireClientForRefresh();
    std::unique_ptr<ClientInstance> DetachClientLocked();
    void RetryClientReplacement();
    void BindOtaProgressPublisher();
    void ScheduleCredentialRefresh(uint32_t generation);
    void FinishCredentialRefresh();
    void MaybeScheduleTransportRecovery();
    bool ShouldDeferCredentialRefresh();
    bool HasClient() const;
    bool IsCurrentClientGeneration(uint32_t generation, uint64_t connection_epoch = 0) const;
    void AdvanceConnectionEpochLocked();
    void ResetEffectAuthorityLocked();
    bool SchedulePublicationEventLocked();
    void QueueEffectReceipt(const std::string& payload, uint32_t generation,
                            uint64_t connection_epoch);
    bool QueueCommandPublication(const CommandPublishContext& context,
                                 const std::string& payload,
                                 const StreamLeasePtr& stream_lease = {},
                                 bool terminal_stream_event = false,
                                 bool display_stream = false);
    void DrainCommandPublications(esp_mqtt_client_handle_t event_client);
    bool IsCommandContextCurrentLocked(const CommandPublishContext& context) const;
    bool IsStreamPublicationCurrentLocked(const StreamLeasePtr& lease,
                                         bool terminal, bool display) const;
    void RevokeStreamLease(const StreamLeasePtr& lease);
    void CleanupRevokedStreams();
    void CleanupRevokedStreamsLocked();
    std::string CopyTopic(const std::string DeviceCloudConfig::*member) const;
    void HandleMqttEvent(esp_mqtt_event_handle_t event, uint32_t generation);
    void HandleMessage(const std::string& topic, const std::string& payload,
                       uint32_t generation, uint64_t connection_epoch);
    void SubscribeTopics();
    bool PublishWithAck(const std::string& topic, const std::string& payload);
    void PublishTelemetry();
    void PublishShadowReport(const VoiceWakeState* voice_state = nullptr);
    void PublishChangedVoiceIdentity();
    void ApplyDesiredShadow(const std::string& payload, uint32_t generation,
                            uint64_t connection_epoch);
    void HandlePcStatus(const std::string& payload);
    void HandleCommand(const std::string& command_no, const std::string& payload,
                       const CommandPublishContext& context);

    DeviceCloudConfigService& config_service_;
    OtaUpdateService& ota_update_;
    AudioOutputService* audio_output_ = nullptr;
    MqttVolumeEffect volume_effect_;
    MqttLightEffect light_effect_;
    MqttCommandLedger command_ledger_;
    BatteryStateProvider* battery_provider_ = nullptr;
    LightService* light_service_ = nullptr;
    VoiceWakeService* voice_wake_ = nullptr;
    std::string last_voice_identity_report_;
    WebRtcCameraService* web_rtc_camera_service_ = nullptr;
    WebRtcDisplayService* web_rtc_display_service_ = nullptr;
    AppearanceService* appearance_ = nullptr;
    std::atomic<bool> appearance_report_pending_{false};
    std::string deferred_ota_payload_;
    DisplayControlCallback display_control_callback_;
    BatteryMonitor fallback_battery_monitor_;
    DeviceCloudConfig config_;
    uint32_t mqtt_sdk_stack_min_free_ = UINT32_MAX;
    esp_mqtt_client_handle_t client_ = nullptr;
    std::unique_ptr<ClientInstance> client_instance_;
    // A failed stop is not proof of task exit. Keep its callback/TLS storage
    // alive even when the host test's esp_restart() stub returns.
    std::unique_ptr<ClientInstance> failed_client_;
    std::atomic<bool> client_lifecycle_failed_{false};
    esp_event_handler_instance_t ip_event_instance_ = nullptr;
    TimerHandle_t telemetry_timer_ = nullptr;
    StaticSemaphore_t publish_ack_semaphore_storage_ = {};
    SemaphoreHandle_t publish_ack_semaphore_ = nullptr;
    std::mutex reliable_publish_mutex_;
    std::recursive_mutex lifecycle_mutex_;
    std::mutex client_api_mutex_;
    // Only command/worker callers enter this lock. SDK and peer callbacks
    // revoke leases under mqtt_mutex_ without waiting for resource teardown.
    std::mutex stream_operation_mutex_;
    StreamLeasePtr camera_lease_;
    StreamLeasePtr display_lease_;
    uint64_t next_stream_instance_nonce_ = 0;
    uint64_t camera_latest_nonce_ = 0;
    uint64_t display_latest_nonce_ = 0;
    bool stream_cleanup_pending_ = false;
    mutable std::mutex mqtt_mutex_;
    ControlGateTiming control_gate_timing_{};
    bool control_gate_timing_pending_ = false;
    int64_t control_gate_next_log_us_ = 0;
    uint32_t client_generation_ = 0;
    uint64_t connection_epoch_ = 0;
    bool effect_authority_active_ = false;
    struct PendingEffectReceipt {
        uint32_t client_generation = 0;
        uint64_t connection_epoch = 0;
        std::string topic;
        std::string payload;
    };
    std::deque<PendingEffectReceipt> effect_receipts_;
    std::deque<PendingCommandPublication> command_publications_;
    size_t command_publication_bytes_ = 0;
    // Shared wakeup belongs to the SDK client's lifetime, not an epoch or
    // credential generation. Its payload queues carry their own scopes.
    bool publication_event_pending_ = false;
    uint64_t published_event_sequence_ = 0;
    std::array<PublishedEvent, 4> recent_published_events_ = {};
    size_t next_published_event_index_ = 0;
    ReliablePublishState reliable_publish_;
    std::atomic<bool> started_{false};
    std::atomic<bool> connecting_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> reset_scheduled_{false};
    std::atomic<bool> force_refresh_{false};
    MqttTransportRecoveryPolicy transport_recovery_;
    bool transport_refresh_scheduled_ = false;
    uint32_t auth_refresh_generation_ = 0;
    std::unique_ptr<DeviceCloudConfig> pending_credential_config_;
    int64_t client_replacement_retry_at_ms_ = 0;
    int client_replacement_retry_ms_ = 2000;
    bool credential_restart_pending_ = false;
    bool credential_refresh_deferred_for_voice_ = false;
    struct PendingMessage {
        uint32_t client_generation;
        uint64_t connection_epoch;
        std::string topic;
        std::string payload;
    };
    struct MessageAssembly {
        bool active = false;
        uint32_t client_generation = 0;
        uint64_t connection_epoch = 0;
        size_t total_length = 0;
        std::string topic;
        std::string payload;
    };
    MessageAssembly message_assembly_;
    QueueHandle_t message_queue_ = nullptr;
    TaskHandle_t worker_ = nullptr;
    std::atomic<bool> worker_running_{false};
    std::atomic<bool> telemetry_pending_{false};
    bool connected_pending_ = false;
    uint32_t connected_pending_generation_ = 0;
};

}  // namespace rodakos

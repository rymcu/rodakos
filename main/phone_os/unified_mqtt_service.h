#pragma once

#include "phone_os/device_cloud_config.h"
#include "phone_os/battery_monitor.h"
#include "phone_os/light_service.h"

#include <array>
#include <atomic>
#include <cstdint>
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

class UnifiedMqttService {
public:
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
    bool IsConnected() const { return connected_.load(); }
    bool Publish(const std::string& topic, const std::string& payload);

private:
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
    void BindOtaProgressPublisher();
    void ScheduleCredentialRefresh();
    bool HasClient() const;
    bool IsCurrentClientGeneration(uint32_t generation) const;
    std::string CopyTopic(const std::string DeviceCloudConfig::*member) const;
    void HandleMqttEvent(esp_mqtt_event_handle_t event);
    void HandleMessage(const std::string& topic, const std::string& payload);
    void SubscribeTopics();
    bool PublishWithAck(const std::string& topic, const std::string& payload);
    void PublishTelemetry();
    void PublishShadowReport();
    void ApplyDesiredShadow(const std::string& payload);
    void HandlePcStatus(const std::string& payload);
    void HandleCommand(const std::string& command_no, const std::string& payload);

    DeviceCloudConfigService& config_service_;
    OtaUpdateService& ota_update_;
    AudioOutputService* audio_output_ = nullptr;
    BatteryStateProvider* battery_provider_ = nullptr;
    LightService* light_service_ = nullptr;
    VoiceWakeService* voice_wake_ = nullptr;
    BatteryMonitor fallback_battery_monitor_;
    DeviceCloudConfig config_;
    esp_mqtt_client_handle_t client_ = nullptr;
    esp_event_handler_instance_t ip_event_instance_ = nullptr;
    TimerHandle_t telemetry_timer_ = nullptr;
    std::string broker_uri_;
    std::string client_id_;
    StaticSemaphore_t publish_ack_semaphore_storage_ = {};
    SemaphoreHandle_t publish_ack_semaphore_ = nullptr;
    std::mutex reliable_publish_mutex_;
    std::mutex client_api_mutex_;
    mutable std::mutex mqtt_mutex_;
    uint32_t client_generation_ = 0;
    uint64_t published_event_sequence_ = 0;
    std::array<PublishedEvent, 4> recent_published_events_ = {};
    size_t next_published_event_index_ = 0;
    ReliablePublishState reliable_publish_;
    std::atomic<bool> started_{false};
    std::atomic<bool> connecting_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> reset_scheduled_{false};
    std::atomic<bool> force_refresh_{false};
    struct PendingMessage {
        uint32_t client_generation;
        std::string topic;
        std::string payload;
    };
    struct MessageAssembly {
        bool active = false;
        uint32_t client_generation = 0;
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

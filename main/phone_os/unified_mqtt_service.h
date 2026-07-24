#pragma once

#include "phone_os/device_cloud_config.h"

#include <atomic>
#include <mutex>
#include <string>

#include <esp_event.h>
#include <mqtt_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

namespace rodakos {

class AudioOutputService;
class DeviceCloudConfigService;
class OtaUpdateService;

class UnifiedMqttService {
public:
    UnifiedMqttService(DeviceCloudConfigService& config_service,
                       OtaUpdateService& ota_update,
                       AudioOutputService* audio_output);
    ~UnifiedMqttService();

    bool Start();
    void Stop();
    bool IsConnected() const { return connected_.load(); }
    bool Publish(const std::string& topic, const std::string& payload);

private:
    static void NetworkEventHandler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);
    static void MqttEventHandler(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data);
    static void ConnectionTask(void* arg);
    static void ClientResetTask(void* arg);
    static void TelemetryTimerCallback(TimerHandle_t timer);

    void StartConnectionAsync();
    void Connect();
    void ScheduleCredentialRefresh();
    bool HasClient() const;
    std::string CopyTopic(const std::string DeviceCloudConfig::*member) const;
    void HandleMqttEvent(esp_mqtt_event_handle_t event);
    void HandleMessage(const std::string& topic, const std::string& payload);
    void SubscribeTopics();
    void PublishTelemetry();
    void PublishShadowReport();
    void ApplyDesiredShadow(const std::string& payload);

    DeviceCloudConfigService& config_service_;
    OtaUpdateService& ota_update_;
    AudioOutputService* audio_output_ = nullptr;
    DeviceCloudConfig config_;
    esp_mqtt_client_handle_t client_ = nullptr;
    esp_event_handler_instance_t ip_event_instance_ = nullptr;
    TimerHandle_t telemetry_timer_ = nullptr;
    std::string broker_uri_;
    std::string client_id_;
    mutable std::mutex mqtt_mutex_;
    std::atomic<bool> started_{false};
    std::atomic<bool> connecting_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> reset_scheduled_{false};
    std::atomic<bool> force_refresh_{false};
};

}  // namespace rodakos

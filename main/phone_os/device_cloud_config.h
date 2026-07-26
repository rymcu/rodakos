#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace rodakos {

struct DeviceCloudConfig {
    std::string provisioning_url;
    std::string websocket_url;
    std::string websocket_token;
    int websocket_version = 1;
    int mqtt_protocol_version = 1;
    std::string mqtt_broker_address;
    int mqtt_broker_port = 1883;
    std::string mqtt_username;
    std::string mqtt_password;  // Also used as the OTA HTTP Bearer credential.
    int mqtt_keepalive = 240;
    std::string mqtt_device_key;
    bool mqtt_home_enabled = false;
    std::string mqtt_http_base_url;
    std::string mqtt_topic_telemetry;
    std::string mqtt_topic_shadow_report;
    std::string mqtt_topic_shadow_desired;
    std::string mqtt_topic_ota_notify;
    std::string mqtt_topic_ota_progress;
    std::string mqtt_topic_commands;
    std::string mqtt_topic_pc_status;
    std::string mqtt_topic_home_prefix;
    std::string activation_code;
    std::string activation_message;
    bool has_websocket_config = false;
    bool has_mqtt_config = false;
    bool has_activation_code = false;
};

class DeviceCloudConfigService {
public:
    bool Load(DeviceCloudConfig& config);
    bool Refresh(DeviceCloudConfig& config);
    bool SaveProvisioningUrl(const std::string& url);
    std::string GetClientId();
    std::string last_error() const;

    static const char* DefaultProvisioningUrl();

private:
    bool SaveWebsocketConfig(const DeviceCloudConfig& config);
    bool SaveMqttConfig(const DeviceCloudConfig& config);
    void ClearWebsocketConfig();
    void ClearMqttConfig();
    bool ParseProvisioningResponse(const std::string& response, DeviceCloudConfig& config);
    std::string BuildSystemInfoJson();
    std::string BuildBoardJson();
    void SetError(const std::string& message);

    mutable std::recursive_mutex config_mutex_;
    std::mutex refresh_mutex_;
    uint32_t config_generation_ = 0;
    std::string last_error_;
};

}  // namespace rodakos

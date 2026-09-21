#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace rodakos {

// These are the external Rodak device identity values. Board Manager keeps
// its internal board name as `rymcu_bigsmart`; that build-time identifier is
// intentionally not reused as the cloud product key.
inline constexpr char kRodakBigSmartProductKey[] = "rymcu-bigsmart";
inline constexpr char kRodakAiotProtocol[] = "rodak-aiot";
inline constexpr int kRodakAiotProtocolVersion = 1;

struct DeviceCloudConfig {
    std::string provisioning_url;
    // Autonomous Rodak AIoT identity. These values are persisted separately
    // from the legacy XiaoZhi websocket cache and are the source of truth for
    // MQTT/HTTP device authentication.
    std::string aiot_device_secret;
    std::string aiot_access_token;
    bool aiot_registered = false;
    bool aiot_activated = false;
    bool aiot_pending = false;
    bool unbind_pending = false;
    bool unbind_server_acknowledged = false;
    std::string pairing_request_id;
    std::string pairing_request_token;
    std::string pairing_code;
    std::string pairing_expires_at;
    std::string pairing_status;
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
    bool has_aiot_config = false;
    bool has_activation_code = false;
    bool has_pairing_request = false;
};

enum class ProvisioningUrlSaveResult {
    kSaved,
    kFailedRolledBack,
    kStateUncertain,
};

constexpr ProvisioningUrlSaveResult ClassifyProvisioningUrlSaveFailure(
    bool url_restored, bool websocket_restored, bool mqtt_restored) {
    return url_restored && websocket_restored && mqtt_restored
               ? ProvisioningUrlSaveResult::kFailedRolledBack
               : ProvisioningUrlSaveResult::kStateUncertain;
}

class DeviceCloudConfigService {
public:
    bool Load(DeviceCloudConfig& config);
    bool Refresh(DeviceCloudConfig& config);
    bool Unbind(DeviceCloudConfig& config);
    ProvisioningUrlSaveResult SaveProvisioningUrl(const std::string& url);
    std::string GetClientId();
    std::string last_error() const;

    static const char* DefaultProvisioningUrl();

private:
    bool ParseProvisioningResponse(const std::string& response, DeviceCloudConfig& config);
    bool RefreshAiot(DeviceCloudConfig& config);
    std::string BuildSystemInfoJson();
    std::string BuildBoardJson();
    void SetError(const std::string& message);

    mutable std::recursive_mutex config_mutex_;
    std::mutex refresh_mutex_;
    uint32_t config_generation_ = 0;
    std::string last_error_;
};

}  // namespace rodakos

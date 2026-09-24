#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace rodakos {

// These are the external Rodak device identity values. Board Manager keeps
// its internal board name as `rymcu_bigsmart`; that build-time identifier is
// intentionally not reused as the cloud product key.
inline constexpr char kRodakBigSmartProductKey[] = "rymcu-bigsmart";
inline constexpr char kRodakAiotProtocol[] = "rodak-aiot";
inline constexpr int kRodakAiotProtocolVersion = 1;

struct DeviceCloudConfig {
    std::string provisioning_url;
    // Autonomous Rodak AIoT identity. These values are the source of truth for
    // MQTT/HTTP authentication and the canonical realtime voice stream.
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
    std::string realtime_voice_url;
    int realtime_voice_protocol_version = 1;
    int realtime_voice_downlink_sample_rate_hz = 24000;
    int realtime_voice_downlink_frame_duration_ms = 60;
    size_t realtime_voice_max_audio_frame_bytes = 8192;
    size_t realtime_voice_max_control_bytes = 64 * 1024;
    std::vector<std::string> realtime_voice_vad_strategies;
    std::string realtime_voice_preferred_vad_strategy = "server-authoritative";
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
    bool has_realtime_voice_config = false;
    bool has_mqtt_config = false;
    bool has_aiot_config = false;
    bool has_activation_code = false;
    bool has_pairing_request = false;
};

enum class ProvisioningUrlSaveResult {
    kSaved,
    // The normalized endpoint was already active and no state was changed.
    kUnchanged,
    kFailedRolledBack,
    kStateUncertain,
};

enum class ProvisioningUrlSaveMode {
    // Preserve a working cloud identity when the effective endpoint is the same.
    kPreserveCredentials,
    // Explicit provisioning is a credential rotation boundary, even for the
    // same endpoint (for example a new serial provisioning transaction).
    kForceRefresh,
};

constexpr ProvisioningUrlSaveResult ClassifyProvisioningUrlSaveFailure(
    bool url_restored, bool realtime_voice_restored, bool mqtt_restored) {
    return url_restored && realtime_voice_restored && mqtt_restored
               ? ProvisioningUrlSaveResult::kFailedRolledBack
               : ProvisioningUrlSaveResult::kStateUncertain;
}

class DeviceCloudConfigService {
public:
    bool Load(DeviceCloudConfig& config);
    bool Refresh(DeviceCloudConfig& config);
    bool Unbind(DeviceCloudConfig& config);
    ProvisioningUrlSaveResult SaveProvisioningUrl(
        const std::string& url,
        ProvisioningUrlSaveMode mode = ProvisioningUrlSaveMode::kPreserveCredentials);
    std::string GetClientId();
    std::string last_error() const;

    static const char* DefaultProvisioningUrl();

private:
    bool RefreshAiot(DeviceCloudConfig& config);
    void SetError(const std::string& message);

    mutable std::recursive_mutex config_mutex_;
    std::mutex refresh_mutex_;
    uint32_t config_generation_ = 0;
    std::string last_error_;
};

}  // namespace rodakos

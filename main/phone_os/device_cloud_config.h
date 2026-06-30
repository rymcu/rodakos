#pragma once

#include <cstdint>
#include <string>

namespace rodakos {

struct DeviceCloudConfig {
    std::string provisioning_url;
    std::string websocket_url;
    std::string websocket_token;
    int websocket_version = 1;
    std::string activation_code;
    std::string activation_message;
    bool has_websocket_config = false;
    bool has_activation_code = false;
};

class DeviceCloudConfigService {
public:
    bool Load(DeviceCloudConfig& config);
    bool Refresh(DeviceCloudConfig& config);
    bool SaveProvisioningUrl(const std::string& url);
    std::string GetClientId();
    const char* last_error() const { return last_error_.c_str(); }

    static const char* DefaultProvisioningUrl();

private:
    bool SaveWebsocketConfig(const DeviceCloudConfig& config);
    void ClearWebsocketConfig();
    bool ParseProvisioningResponse(const std::string& response, DeviceCloudConfig& config);
    std::string BuildSystemInfoJson();
    std::string BuildBoardJson();
    void SetError(const std::string& message);

    std::string last_error_;
};

}  // namespace rodakos

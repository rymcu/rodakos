#pragma once

#include <cstdint>
#include <string>

namespace rodakos {

struct VoiceCloudConfig {
    std::string ota_url;
    std::string websocket_url;
    std::string websocket_token;
    int websocket_version = 1;
    std::string activation_code;
    std::string activation_message;
    bool has_websocket_config = false;
    bool has_activation_code = false;
};

class VoiceCloudConfigService {
public:
    bool Load(VoiceCloudConfig& config);
    bool RefreshFromOta(VoiceCloudConfig& config);
    std::string GetClientId();
    const char* last_error() const { return last_error_.c_str(); }

    static const char* DefaultOtaUrl();

private:
    bool SaveWebsocketConfig(const VoiceCloudConfig& config);
    bool ParseOtaResponse(const std::string& response, VoiceCloudConfig& config);
    std::string BuildSystemInfoJson();
    std::string BuildBoardJson();
    void SetError(const std::string& message);

    std::string last_error_;
};

}  // namespace rodakos

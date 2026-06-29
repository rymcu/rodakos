#pragma once

#include <cstdint>
#include <string>

namespace rodakos {

struct XiaozhiCloudConfig {
    std::string ota_url;
    std::string websocket_url;
    std::string websocket_token;
    int websocket_version = 1;
    std::string activation_code;
    std::string activation_message;
    bool has_websocket_config = false;
    bool has_activation_code = false;
};

class XiaozhiCloudConfigService {
public:
    bool Load(XiaozhiCloudConfig& config);
    bool RefreshFromOta(XiaozhiCloudConfig& config);
    std::string GetClientId();
    const char* last_error() const { return last_error_.c_str(); }

    static const char* DefaultOtaUrl();

private:
    bool SaveWebsocketConfig(const XiaozhiCloudConfig& config);
    bool ParseOtaResponse(const std::string& response, XiaozhiCloudConfig& config);
    std::string BuildSystemInfoJson();
    std::string BuildBoardJson();
    void SetError(const std::string& message);

    std::string last_error_;
};

}  // namespace rodakos

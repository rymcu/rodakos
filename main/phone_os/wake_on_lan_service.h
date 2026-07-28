#pragma once

#include <cstdint>
#include <string_view>

class WiFiAdapter;

namespace rodakos {

enum class WakeOnLanResult {
    kSent,
    kWifiDisconnected,
    kInvalidMac,
    kInvalidBroadcastAddress,
    kInvalidPort,
    kSocketOpenFailed,
    kSocketConfigureFailed,
    kSendFailed,
};

class WakeOnLanService {
public:
    explicit WakeOnLanService(WiFiAdapter* wifi) : wifi_(wifi) {}

    WakeOnLanResult Send(std::string_view mac_address,
                         std::string_view broadcast_address,
                         uint16_t port) const;

private:
    WiFiAdapter* wifi_ = nullptr;
};

}  // namespace rodakos

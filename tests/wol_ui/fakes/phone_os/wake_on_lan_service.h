#pragma once
// Host fake for the Wake UI tests: records every send and returns a scripted result.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

struct SentPacket {
    std::string mac;
    std::string broadcast;
    uint16_t port = 0;
};

class WakeOnLanService {
public:
    explicit WakeOnLanService(WiFiAdapter* wifi = nullptr) : wifi_(wifi) {}
    WakeOnLanResult Send(std::string_view mac, std::string_view broadcast, uint16_t port) const {
        sent.push_back({std::string(mac), std::string(broadcast), port});
        return result;
    }
    mutable std::vector<SentPacket> sent;
    WakeOnLanResult result = WakeOnLanResult::kSent;

private:
    WiFiAdapter* wifi_ = nullptr;
};

}  // namespace rodakos

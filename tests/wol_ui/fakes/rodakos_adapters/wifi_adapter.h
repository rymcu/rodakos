#pragma once
#include <string>
enum class WiFiStatus { kDisconnected, kConnecting, kConnected, kFailed };
class WiFiAdapter {
public:
    WiFiStatus GetStatus() const { return status; }
    std::string GetConnectedSSID() const { return ssid; }
    WiFiStatus status = WiFiStatus::kConnected;
    std::string ssid = "书房-5G";
};

#pragma once
#include <string>
enum class WiFiStatus { kDisconnected, kConnecting, kConnected, kFailed };
class WiFiAdapter {
public:
    WiFiStatus GetStatus() const { return status; }
    std::string GetConnectedSSID() const { return ssid; }
    std::string GetIPAddress() const { return ip; }
    std::string AuthRejectedSSID() const { return rejected; }
    WiFiStatus status = WiFiStatus::kConnected;
    std::string ssid;
    std::string ip;
    std::string rejected;
};

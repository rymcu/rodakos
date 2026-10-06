#pragma once
#include <string>
struct WiFiScanResult { std::string ssid; };
enum class WiFiStatus { kDisconnected, kConnecting, kConnected, kFailed };
class WiFiAdapter {
public:
    WiFiStatus GetStatus() const { return status; }
    WiFiStatus status = WiFiStatus::kConnected;
};

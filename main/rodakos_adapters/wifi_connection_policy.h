#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Compare SSIDs as raw octets; WiFi SSIDs are not signed text.
bool WiFiEventMatchesTargetSSID(const std::string& target_ssid,
                                const uint8_t* event_ssid,
                                size_t event_ssid_length);

// GOT_IP is associated only after STA_CONNECTED recorded the same generation.
bool ShouldAcceptWiFiGotIP(uint32_t active_generation,
                           uint32_t sta_connected_generation,
                           bool connection_active);

bool ShouldRetryWiFiConnection(int retry_count,
                               int max_retries,
                               bool connection_active,
                               bool has_callback);

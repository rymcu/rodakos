#include "rodakos_adapters/wifi_connection_policy.h"

#include <algorithm>

bool WiFiEventMatchesTargetSSID(const std::string& target_ssid,
                                const uint8_t* event_ssid,
                                size_t event_ssid_length) {
    return event_ssid != nullptr && target_ssid.size() == event_ssid_length &&
           std::equal(target_ssid.begin(), target_ssid.end(), event_ssid,
                      [](char target_byte, uint8_t event_byte) {
                          return static_cast<uint8_t>(
                                     static_cast<unsigned char>(target_byte)) == event_byte;
                      });
}

bool ShouldAcceptWiFiGotIP(uint32_t active_generation,
                           uint32_t sta_connected_generation,
                           bool connection_active) {
    return connection_active && active_generation != 0 &&
           sta_connected_generation == active_generation;
}

bool ShouldRetryWiFiConnection(int retry_count,
                               int max_retries,
                               bool connection_active,
                               bool has_callback) {
    return connection_active && has_callback && retry_count >= 0 &&
           max_retries > 0 && retry_count < max_retries;
}

WiFiFailureKind ClassifyWiFiDisconnectReason(uint8_t reason) {
    // Values from esp_wifi_types_generic.h (wifi_err_reason_t); kept numeric for host tests.
    switch (reason) {
        case 2:    // AUTH_EXPIRE
        case 14:   // MIC_FAILURE
        case 15:   // 4WAY_HANDSHAKE_TIMEOUT
        case 202:  // AUTH_FAIL
        case 204:  // HANDSHAKE_TIMEOUT
            return WiFiFailureKind::kAuthRejected;
        case 201:  // NO_AP_FOUND
        case 210:  // NO_AP_FOUND_W_COMPATIBLE_SECURITY
        case 211:  // NO_AP_FOUND_IN_AUTHMODE_THRESHOLD
            return WiFiFailureKind::kNetworkNotFound;
        default:
            return WiFiFailureKind::kOther;
    }
}

uint32_t WiFiRecoveryBackoffMs(uint32_t retry_count) {
    return retry_count >= 5 ? 30000 : (1000u << retry_count);
}

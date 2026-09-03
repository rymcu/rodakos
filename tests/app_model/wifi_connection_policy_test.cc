#include "test_framework.h"

#include "rodakos_adapters/wifi_connection_policy.h"

#include <string>

RODAK_TEST("WiFi event SSID must exactly match the current target") {
    const std::string target = "new-network";
    const std::string matching = "new-network";
    const std::string stale = "old-network";

    RODAK_CHECK(WiFiEventMatchesTargetSSID(
        target, reinterpret_cast<const uint8_t*>(matching.data()), matching.size()));
    RODAK_CHECK_FALSE(WiFiEventMatchesTargetSSID(
        target, reinterpret_cast<const uint8_t*>(stale.data()), stale.size()));
}

RODAK_TEST("WiFi event SSID comparison preserves embedded and full length bytes") {
    const std::string target(32, 'x');
    std::string shorter(31, 'x');

    RODAK_CHECK(WiFiEventMatchesTargetSSID(
        target, reinterpret_cast<const uint8_t*>(target.data()), target.size()));
    RODAK_CHECK_FALSE(WiFiEventMatchesTargetSSID(
        target, reinterpret_cast<const uint8_t*>(shorter.data()), shorter.size()));
    RODAK_CHECK_FALSE(WiFiEventMatchesTargetSSID(target, nullptr, target.size()));
}

RODAK_TEST("WiFi event SSID comparison treats high bytes as raw octets") {
    const std::string target({static_cast<char>(0x80), static_cast<char>(0xff), 'x'});
    const uint8_t event[] = {0x80, 0xff, 'x'};
    const uint8_t wrong[] = {0x80, 0xfe, 'x'};

    RODAK_CHECK(WiFiEventMatchesTargetSSID(target, event, sizeof(event)));
    RODAK_CHECK_FALSE(WiFiEventMatchesTargetSSID(target, wrong, sizeof(wrong)));
}

RODAK_TEST("WiFi GOT IP requires target STA connected in the active generation") {
    RODAK_CHECK(ShouldAcceptWiFiGotIP(7, 7, true));
    RODAK_CHECK_FALSE(ShouldAcceptWiFiGotIP(7, 6, true));
    RODAK_CHECK_FALSE(ShouldAcceptWiFiGotIP(7, 7, false));
    RODAK_CHECK_FALSE(ShouldAcceptWiFiGotIP(0, 0, true));
}

RODAK_TEST("WiFi retry policy stops at the retry budget and requires an active callback") {
    RODAK_CHECK(ShouldRetryWiFiConnection(0, 3, true, true));
    RODAK_CHECK(ShouldRetryWiFiConnection(2, 3, true, true));
    RODAK_CHECK_FALSE(ShouldRetryWiFiConnection(3, 3, true, true));
    RODAK_CHECK_FALSE(ShouldRetryWiFiConnection(0, 3, false, true));
    RODAK_CHECK_FALSE(ShouldRetryWiFiConnection(0, 3, true, false));
    RODAK_CHECK_FALSE(ShouldRetryWiFiConnection(-1, 3, true, true));
}

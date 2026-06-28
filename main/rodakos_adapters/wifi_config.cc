#include "rodakos_adapters/wifi_config.h"
#include "settings.h"
#include <esp_log.h>

static const char* TAG = "WiFiConfig";

WiFiConfig::WiFiConfig() {
}

WiFiConfig::~WiFiConfig() {
}

bool WiFiConfig::SaveCredentials(const std::string& ssid, const std::string& password) {
    Settings wifi_settings(kNamespace, true);

    wifi_settings.SetString(kSSIDKey, ssid);
    wifi_settings.SetString(kPasswordKey, password);

    ESP_LOGI(TAG, "WiFi credentials saved: %s", ssid.c_str());
    return true;
}

bool WiFiConfig::LoadCredentials(std::string& ssid, std::string& password) {
    Settings wifi_settings(kNamespace, false);

    ssid = wifi_settings.GetString(kSSIDKey, "");
    password = wifi_settings.GetString(kPasswordKey, "");

    if (ssid.empty()) {
        ESP_LOGI(TAG, "No saved WiFi credentials");
        return false;
    }

    ESP_LOGI(TAG, "Loaded WiFi credentials: %s", ssid.c_str());
    return true;
}

std::string WiFiConfig::GetSavedSSID() {
    Settings wifi_settings(kNamespace, false);
    return wifi_settings.GetString(kSSIDKey, "");
}

void WiFiConfig::ClearCredentials() {
    Settings wifi_settings(kNamespace, true);
    wifi_settings.SetString(kSSIDKey, "");
    wifi_settings.SetString(kPasswordKey, "");
    ESP_LOGI(TAG, "WiFi credentials cleared");
}

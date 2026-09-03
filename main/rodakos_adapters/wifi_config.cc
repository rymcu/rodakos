#include "rodakos_adapters/wifi_config.h"
#include "settings.h"
#include <esp_log.h>

static const char* TAG = "WiFiConfig";

WiFiConfig::WiFiConfig() {
}

WiFiConfig::~WiFiConfig() {
}

bool WiFiConfig::SaveCredentials(const std::string& ssid, const std::string& password) {
    std::string previous_ssid;
    std::string previous_password;
    const bool had_previous = LoadCredentials(previous_ssid, previous_password);
    Settings wifi_settings(kNamespace, true);

    const bool written = wifi_settings.SetString(kSSIDKey, ssid) &&
                         wifi_settings.SetString(kPasswordKey, password);
    const bool committed = written && wifi_settings.Commit();
    if (!committed) {
        // ESP-IDF 6's NVS simple handle applies set operations immediately;
        // restore the snapshot when a later field or commit fails.
        (void)wifi_settings.Commit();
        Settings restore(kNamespace, true);
        const bool restored = restore.SetString(kSSIDKey, had_previous ? previous_ssid : "") &&
                              restore.SetString(kPasswordKey, had_previous ? previous_password : "") &&
                              restore.Commit();
        if (!restored) {
            ESP_LOGE(TAG, "Failed to restore WiFi credentials after write failure");
        }
        ESP_LOGE(TAG, "Failed to persist WiFi credentials");
        return false;
    }

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

bool WiFiConfig::ClearCredentials() {
    Settings wifi_settings(kNamespace, true);
    if (!wifi_settings.SetString(kSSIDKey, "") ||
        !wifi_settings.SetString(kPasswordKey, "") ||
        !wifi_settings.Commit()) {
        ESP_LOGE(TAG, "Failed to clear WiFi credentials");
        return false;
    }
    ESP_LOGI(TAG, "WiFi credentials cleared");
    return true;
}

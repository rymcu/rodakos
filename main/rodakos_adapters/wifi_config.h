#pragma once

#include <string>

/**
 * WiFi 配置管理
 *
 * 保存和加载 WiFi 凭据（SSID 和密码）
 */
class WiFiConfig {
public:
    WiFiConfig();
    ~WiFiConfig();

    /**
     * 保存 WiFi 凭据
     */
    bool SaveCredentials(const std::string& ssid, const std::string& password);

    /**
     * 加载 WiFi 凭据
     */
    bool LoadCredentials(std::string& ssid, std::string& password);

    /**
     * 获取保存的 SSID
     */
    std::string GetSavedSSID();

    /**
     * 清除保存的凭据
     */
    void ClearCredentials();

private:
    static constexpr const char* kNamespace = "wifi";
    static constexpr const char* kSSIDKey = "ssid";
    static constexpr const char* kPasswordKey = "password";
};

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

/**
 * WiFi 扫描结果
 */
struct WiFiScanResult {
    std::string ssid;          // WiFi 名称
    int8_t rssi;               // 信号强度 (dBm)
    uint8_t auth_mode;         // 加密模式
    bool is_secured;           // 是否加密
};

/**
 * WiFi 连接状态
 */
enum class WiFiStatus {
    kDisconnected,    // 未连接
    kConnecting,      // 连接中
    kConnected,       // 已连接
    kFailed,          // 连接失败
};

/**
 * WiFi 适配器接口
 *
 * 提供 WiFi 扫描、连接、断开等功能
 */
class WiFiAdapter {
public:
    virtual ~WiFiAdapter() = default;

    /**
     * 初始化 WiFi
     * @return true 成功，false 失败
     */
    virtual bool Init() = 0;

    /**
     * 反初始化 WiFi
     */
    virtual void Deinit() = 0;

    /**
     * 开始扫描 WiFi
     * @param callback 扫描完成回调，传入扫描结果列表
     * @return true 扫描启动成功，false 失败
     */
    virtual bool StartScan(std::function<void(const std::vector<WiFiScanResult>&)> callback) = 0;

    /**
     * 连接到 WiFi
     * @param ssid WiFi 名称
     * @param password 密码（如果没有密码传空字符串）
     * @param callback 连接结果回调
     * @return true 连接启动成功，false 失败
     */
    virtual bool Connect(const std::string& ssid,
                        const std::string& password,
                        std::function<void(WiFiStatus)> callback) = 0;

    /**
     * 断开 WiFi 连接
     */
    virtual void Disconnect() = 0;

    /**
     * 获取当前连接状态
     */
    virtual WiFiStatus GetStatus() const = 0;

    /**
     * 获取当前连接的 SSID
     */
    virtual std::string GetConnectedSSID() const = 0;

    /**
     * 获取 IP 地址
     */
    virtual std::string GetIPAddress() const = 0;
};

/**
 * 工厂函数：创建 WiFi 适配器实例
 */
WiFiAdapter* CreateWiFiAdapter();

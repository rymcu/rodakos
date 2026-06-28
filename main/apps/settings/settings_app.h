#pragma once

#include "phone_os/phone_app.h"
#include "phone_ui/soft_keyboard.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "rodakos_adapters/wifi_config.h"
#include <lvgl.h>
#include <vector>
#include <memory>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

enum class SettingsPage {
    kMain,        // 主设置页面
    kWiFiList,    // WiFi 列表页面
    kWiFiDetail,  // WiFi 详情页面
};

class SettingsApp final : public PhoneApp {
public:
    ~SettingsApp() override;

    const char* id() const override { return "settings"; }
    bool OnCreate(PhoneAppContext& context) override;
    void OnShow() override {}
    void OnHide() override {}
    void OnDestroy() override;

private:
    // 页面切换
    void ShowPage(SettingsPage page);
    void CreateMainPage();
    void CreateWiFiListPage();
    void CreateWiFiDetailPage();

    // WiFi 相关
    void StartWiFiScan();
    void OnWiFiScanComplete(const std::vector<WiFiScanResult>& results);
    void OnNetworkSelected(const std::string& ssid, bool is_secured);
    void ShowPasswordDialog(const std::string& ssid);
    void ConnectToNetwork(const std::string& ssid, const std::string& password);
    void OnConnectResult(WiFiStatus status, const std::string& ssid);
    void UpdateWiFiDetailPage();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    lv_obj_t* root_ = nullptr;
    SettingsPage current_page_ = SettingsPage::kMain;

    // 主设置页面控件
    lv_obj_t* main_body_ = nullptr;
    lv_obj_t* brightness_label_ = nullptr;
    lv_obj_t* brightness_slider_ = nullptr;
    lv_obj_t* theme_dropdown_ = nullptr;
    lv_obj_t* language_switch_ = nullptr;

    // WiFi 页面控件
    lv_obj_t* wifi_body_ = nullptr;
    lv_obj_t* wifi_status_label_ = nullptr;
    lv_obj_t* wifi_list_container_ = nullptr;
    lv_obj_t* password_dialog_ = nullptr;
    lv_obj_t* password_textarea_ = nullptr;
    std::vector<WiFiScanResult> wifi_scan_results_;
    std::string connecting_ssid_;
    SoftKeyboard soft_keyboard_;
    WiFiConfig wifi_config_;

    // WiFi 详情页控件
    lv_obj_t* wifi_detail_body_ = nullptr;
    lv_obj_t* detail_ssid_label_ = nullptr;
    lv_obj_t* detail_status_label_ = nullptr;
    lv_obj_t* detail_ip_label_ = nullptr;
    lv_obj_t* detail_gateway_label_ = nullptr;
    lv_obj_t* detail_netmask_label_ = nullptr;
    lv_obj_t* detail_rssi_label_ = nullptr;
};

void RegisterSettingsApp(PhoneAppRegistry& registry);

#pragma once

#include "phone_os/phone_app.h"
#include "phone_ui/soft_keyboard.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "rodakos_adapters/wifi_config.h"
#include <lvgl.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;
class SettingsApp;

struct SettingsCloudRefreshGuard {
    std::atomic<SettingsApp*> app{nullptr};
    std::atomic<bool> refresh_in_progress{false};
    std::atomic<uint32_t> refresh_generation{0};
};

namespace rodakos {
struct ButtonAction;
struct ButtonBinding;
struct DeviceCloudConfig;
}

enum class SettingsPage {
    kMain,        // 主设置页面
    kWiFiList,    // WiFi 列表页面
    kWiFiDetail,  // WiFi 详情页面
    kDateTime,    // 日期与时间页面
    kButtons,     // 按键绑定页面
    kDeviceCloud, // 设备云配置页面
    kWebFiles,    // Web 文件管理页面
};

class SettingsApp final : public PhoneApp {
public:
    ~SettingsApp() override;

    const char* id() const override { return "settings"; }
    bool OnCreate(PhoneAppContext& context) override;
    void OnShow() override {}
    void OnHide() override {}
    void OnDestroy() override;
    void OnDeviceCloudRefreshComplete(bool ok,
                                      const rodakos::DeviceCloudConfig& config,
                                      const std::string& error,
                                      uint32_t generation);

private:
    // 页面切换
    void ShowPage(SettingsPage page);
    void CreateMainPage();
    void CreateWiFiListPage();
    void CreateWiFiDetailPage();
    void CreateDateTimePage();
    void CreateButtonBindingsPage();
    void UpdateButtonBindingsPage();
    void ShowButtonActionDialog(const rodakos::ButtonBinding& binding);
    void CloseButtonActionDialog();
    void SaveButtonBindingAction(const rodakos::ButtonBinding& binding,
                                 const rodakos::ButtonAction& action);
    void CreateDeviceCloudPage();
    void UpdateDeviceCloudPage();
    void RefreshDeviceCloud();
    void ShowCloudProvisioningUrlDialog();
    void CloseCloudProvisioningUrlDialog();
    void CloseCloudProvisioningUrlDialogAsync();
    void SaveCloudProvisioningUrl(const std::string& url);
    void CreateWebFilesPage();
    void UpdateWebFilesPage();
    void StartWebFiles();
    void StopWebFiles();
    void ShowUsbDiskDialog();
    void CloseUsbDiskDialog();
    void EnterUsbDiskMode();
    void ShowUsbDiskEnablePage();
    void ShowNtpServerDialog();
    void CloseNtpServerDialog();
    void CloseNtpServerDialogAsync();
    void NavigateBack();
    void NavigateHome();

    // WiFi 相关
    void StartWiFiScan();
    void OnWiFiScanComplete(const std::vector<WiFiScanResult>& results);
    void OnNetworkSelected(const std::string& ssid, bool is_secured);
    void ShowPasswordDialog(const std::string& ssid);
    void ConnectToNetwork(const std::string& ssid, const std::string& password);
    void OnConnectResult(WiFiStatus status, const std::string& ssid);
    void UpdateWiFiDetailPage();
    void SaveTimeZone(size_t index);
    void SaveNtpServer(const std::string& server);
    void StartTimeSync();
    void UpdateTimeSyncStatus();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    lv_obj_t* root_ = nullptr;
    SettingsPage current_page_ = SettingsPage::kMain;

    // 主设置页面控件
    lv_obj_t* main_body_ = nullptr;
    lv_obj_t* brightness_label_ = nullptr;
    lv_obj_t* brightness_slider_ = nullptr;
    lv_obj_t* theme_buttons_[4] = {};
    lv_obj_t* language_switch_ = nullptr;
    lv_obj_t* usb_disk_dialog_ = nullptr;
    lv_obj_t* usb_disk_hint_page_ = nullptr;
    lv_timer_t* usb_disk_restart_timer_ = nullptr;
    lv_obj_t* header_title_label_ = nullptr;

    // 日期与时间页面控件
    lv_obj_t* datetime_body_ = nullptr;
    lv_obj_t* timezone_dropdown_ = nullptr;
    lv_obj_t* ntp_dropdown_ = nullptr;
    lv_obj_t* ntp_dialog_ = nullptr;
    lv_obj_t* ntp_textarea_ = nullptr;
    lv_obj_t* time_sync_status_label_ = nullptr;
    lv_timer_t* time_sync_timer_ = nullptr;
    bool time_sync_in_progress_ = false;
    uint32_t time_sync_poll_count_ = 0;

    // 按键绑定页面控件
    lv_obj_t* buttons_body_ = nullptr;
    lv_obj_t* button_action_dialog_ = nullptr;

    // 设备云配置页面控件
    lv_obj_t* device_cloud_body_ = nullptr;
    lv_obj_t* cloud_status_label_ = nullptr;
    lv_obj_t* cloud_url_label_ = nullptr;
    lv_obj_t* cloud_client_id_label_ = nullptr;
    lv_obj_t* cloud_websocket_label_ = nullptr;
    lv_obj_t* cloud_activation_label_ = nullptr;
    lv_obj_t* cloud_url_dialog_ = nullptr;
    lv_obj_t* cloud_url_textarea_ = nullptr;
    std::shared_ptr<SettingsCloudRefreshGuard> cloud_refresh_guard_;

    // Web 上传页面控件
    lv_obj_t* web_upload_body_ = nullptr;
    lv_obj_t* web_upload_status_label_ = nullptr;
    lv_obj_t* web_upload_url_label_ = nullptr;
    lv_obj_t* web_upload_last_label_ = nullptr;
    lv_obj_t* web_upload_start_btn_ = nullptr;
    lv_obj_t* web_upload_stop_btn_ = nullptr;

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

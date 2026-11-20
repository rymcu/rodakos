#pragma once

#include "phone_os/phone_app.h"
#include "phone_os/server_trust.h"
#include "phone_ui/soft_keyboard.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "rodakos_adapters/wifi_config.h"
#include "apps/settings/settings_web_files_page.h"
#include "apps/settings/device_cloud_page_model.h"
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

enum class SettingsCloudRefreshFailure { kNone, kTaskStart, kCompletionDelivery };

// Status shown at the end of the 连接服务 row on the Settings home.
enum class SettingsCloudSummary { kUnknown, kUnbound, kPairing, kBound, kAttention };

struct SettingsCloudRefreshGuard {
    std::atomic<SettingsApp*> app{nullptr};
    std::atomic<bool> refresh_in_progress{false};
    std::atomic<SettingsCloudRefreshFailure> refresh_failure{SettingsCloudRefreshFailure::kNone};
    std::atomic<uint32_t> refresh_generation{0};
};

struct SettingsWiFiAsyncGuard {
    std::atomic<SettingsApp*> app{nullptr};
    std::atomic<uint32_t> generation{0};
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
    kSystemShell, // 系统壳页面
    kButtons,     // 按键绑定页面
    kDeviceCloud, // 设备云配置页面
    kWebFiles,    // Web 文件管理页面
};

class SettingsApp final : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override {}
    void OnPause() override {}
    void OnDestroy() override;
    bool OnBackRequested() override;
    bool OnThemeChanged(PhoneAppContext& context) override;
    void OnDeviceCloudRefreshComplete(bool ok,
                                      const rodakos::DeviceCloudConfig& config,
                                      const std::string& error,
                                      uint32_t generation);
    void OnDeviceCloudUnbindComplete(bool ok,
                                     const std::string& error,
                                     uint32_t generation);
    void ReloadUiForTheme();
    void OnCloudSummaryReady(SettingsCloudSummary summary);
    void OnUsbDiskSdProbed(bool ready);
    void OnWiFiScanAsyncComplete(const std::vector<WiFiScanResult>& results);
    void OnWiFiConnectAsyncComplete(WiFiStatus status,
                                    const std::string& ssid,
                                    const std::string& password);

private:
    // 页面切换
    bool CreateUi();
    void DestroyUi();
    void ResetUiPointers();
    void ShowPage(SettingsPage page);
    void CreateMainPage();
    // Refreshes the row-end values; NVS-backed values are read off the LVGL thread.
    void UpdateMainPageStatus();
    void CreateWiFiListPage();
    void CreateWiFiDetailPage();
    void CreateDateTimePage();
    void CreateSystemShellPage();
    void CreateButtonBindingsPage();
    void UpdateButtonBindingsPage();
    void ShowButtonActionDialog(const rodakos::ButtonBinding& binding);
    void CloseButtonActionDialog();
    void SaveButtonBindingAction(const rodakos::ButtonBinding& binding,
                                 const rodakos::ButtonAction& action);
    void CreateDeviceCloudPage();
    void UpdateDeviceCloudPage();
    void ShowDeviceCloudDetails(bool show);
    void RunDeviceCloudAction(rodakos_settings::DeviceCloudAction action);
    void RefreshDeviceCloud();
    void ShowDeviceCloudUnbindDialog();
    void CloseDeviceCloudUnbindDialog();
    void UnbindDeviceCloud();
    void ShowCloudProvisioningUrlDialog();
    void CloseCloudProvisioningUrlDialog();
    void CloseCloudProvisioningUrlDialogAsync();
    bool SaveCloudProvisioningUrl(const std::string& url);
    void ShowLanOnboarding();
    void CloseLanOnboarding();
    void UpdateLanOnboarding();
    void ShowSetupQr();
    void CloseSetupQr();
    // Cancels the scan and frees the overlay without touching other pages (used by DestroyUi).
    void TeardownSetupQr();
    void ShowSetupQrManualEntry();
    void UpdateSetupQrPreview();
    void DetachSetupQrPreview();
    void ReleaseSetupQrPreviewPixels();
    void UpdateSetupQr();
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
    // `notice` replaces the caption, e.g. after the router rejected the saved password.
    void ShowPasswordDialog(const std::string& ssid, const char* notice = nullptr);
    void ClosePasswordDialog();
    void SubmitPasswordDialog();
    void ShowForgetWiFiDialog();
    void CloseForgetWiFiDialog();
    void ConnectToNetwork(const std::string& ssid, const std::string& password);
    void OnConnectResult(WiFiStatus status, const std::string& ssid);
    void UpdateWiFiDetailPage();
    void SaveTimeZone(size_t index);
    bool SaveNtpServer(const std::string& server);
    void UpdateDateTimePage();
    void ShowDateTimePicker(bool servers);
    void CloseDateTimePicker();
    void SubmitNtpServerDialog();
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
    lv_obj_t* home_wifi_value_ = nullptr;
    lv_obj_t* home_cloud_value_ = nullptr;
    lv_obj_t* home_time_zone_value_ = nullptr;
    lv_obj_t* home_web_files_value_ = nullptr;
    SettingsCloudSummary home_cloud_summary_ = SettingsCloudSummary::kUnknown;
    bool home_cloud_summary_pending_ = false;
    std::string home_time_zone_label_;
    lv_obj_t* usb_disk_dialog_ = nullptr;
    lv_obj_t* usb_disk_hint_page_ = nullptr;
    lv_obj_t* usb_disk_sd_label_ = nullptr;
    lv_obj_t* usb_disk_enter_button_ = nullptr;
    bool usb_disk_sd_ready_ = false;
    lv_obj_t* header_title_label_ = nullptr;

    // 日期与时间页面控件
    lv_obj_t* datetime_body_ = nullptr;
    lv_obj_t* timezone_dropdown_ = nullptr;
    lv_obj_t* ntp_dropdown_ = nullptr;
    lv_obj_t* ntp_dialog_ = nullptr;
    lv_obj_t* ntp_textarea_ = nullptr;
    lv_obj_t* time_sync_status_label_ = nullptr;
    lv_obj_t* time_sync_button_ = nullptr;
    lv_obj_t* datetime_clock_label_ = nullptr;
    lv_obj_t* datetime_date_label_ = nullptr;
    lv_obj_t* datetime_picker_ = nullptr;
    lv_obj_t* ntp_error_label_ = nullptr;
    lv_timer_t* datetime_clock_timer_ = nullptr;
    uintptr_t datetime_pending_choice_ = 0;
    int64_t time_sync_start_epoch_ = 0;
    // Loaded once per Settings session; picker and dialog edits update it.
    std::string datetime_server_cache_;
    lv_timer_t* time_sync_timer_ = nullptr;
    bool time_sync_in_progress_ = false;
    uint32_t time_sync_poll_count_ = 0;

    // 系统壳页面控件
    lv_obj_t* system_shell_body_ = nullptr;

    // 按键绑定页面控件
    lv_obj_t* buttons_body_ = nullptr;
    lv_obj_t* button_action_dialog_ = nullptr;

    // 设备云配置页面控件
    lv_obj_t* device_cloud_body_ = nullptr;
    lv_obj_t* cloud_status_label_ = nullptr;
    lv_obj_t* cloud_guide_label_ = nullptr;
    lv_obj_t* cloud_url_label_ = nullptr;
    lv_obj_t* cloud_client_id_label_ = nullptr;
    lv_obj_t* cloud_realtime_voice_label_ = nullptr;
    lv_obj_t* cloud_activation_label_ = nullptr;
    lv_obj_t* cloud_pairing_button_ = nullptr;
    lv_obj_t* cloud_pairing_button_label_ = nullptr;
    lv_obj_t* cloud_unbind_button_ = nullptr;
    lv_obj_t* cloud_status_card_ = nullptr;
    lv_obj_t* cloud_status_dot_ = nullptr;
    lv_obj_t* cloud_summary_label_ = nullptr;
    lv_obj_t* cloud_details_link_ = nullptr;
    lv_obj_t* cloud_secondary_button_ = nullptr;
    lv_obj_t* cloud_secondary_button_label_ = nullptr;
    lv_obj_t* cloud_details_panel_ = nullptr;
    lv_obj_t* cloud_trust_label_ = nullptr;
    lv_obj_t* cloud_edit_url_button_ = nullptr;
    lv_obj_t* cloud_manual_pair_button_ = nullptr;
    bool cloud_details_open_ = false;
    rodakos_settings::DeviceCloudAction cloud_primary_action_ = rodakos_settings::DeviceCloudAction::kNone;
    rodakos_settings::DeviceCloudAction cloud_secondary_action_ = rodakos_settings::DeviceCloudAction::kNone;
    lv_obj_t* cloud_unbind_dialog_ = nullptr;
    lv_timer_t* cloud_pairing_timer_ = nullptr;
    lv_obj_t* cloud_url_dialog_ = nullptr;
    lv_obj_t* cloud_url_textarea_ = nullptr;
    std::string cloud_pairing_error_;
    std::string cloud_pairing_code_;
    std::shared_ptr<SettingsCloudRefreshGuard> cloud_refresh_guard_;

    // 首次接入（无 USB）全屏页
    lv_obj_t* lan_onboarding_overlay_ = nullptr;
    lv_obj_t* lan_onboarding_body_ = nullptr;
    lv_obj_t* lan_onboarding_countdown_ = nullptr;
    lv_timer_t* lan_onboarding_timer_ = nullptr;
    uint32_t lan_onboarding_revision_ = 0;
    std::vector<rodakos::DiscoveredRodakServer> lan_onboarding_servers_;

    // 扫码配网全屏页
    lv_obj_t* setup_qr_overlay_ = nullptr;
    lv_obj_t* setup_qr_body_ = nullptr;
    lv_obj_t* setup_qr_progress_ = nullptr;
    lv_timer_t* setup_qr_timer_ = nullptr;
    uint32_t setup_qr_revision_ = 0;
    lv_obj_t* setup_qr_preview_image_ = nullptr;
    lv_obj_t* setup_qr_preview_placeholder_ = nullptr;
    lv_image_dsc_t setup_qr_preview_dsc_ = {};
    std::vector<uint8_t> setup_qr_preview_pixels_;
    uint32_t setup_qr_preview_sequence_ = 0;

    // Web 上传页面控件
    SettingsWebFilesPage web_files_page_;

    // WiFi 页面控件
    lv_obj_t* wifi_body_ = nullptr;
    lv_obj_t* wifi_status_label_ = nullptr;
    lv_obj_t* wifi_list_container_ = nullptr;
    lv_obj_t* password_dialog_ = nullptr;
    lv_obj_t* wifi_forget_dialog_ = nullptr;
    lv_obj_t* wifi_detail_connect_button_ = nullptr;
    lv_obj_t* password_textarea_ = nullptr;
    std::shared_ptr<SettingsWiFiAsyncGuard> wifi_async_guard_;
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

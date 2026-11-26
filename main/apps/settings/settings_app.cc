#include "apps/settings/settings_app.h"
#include "phone_os/time_service.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "settings.h"

#include <esp_log.h>
#include <memory>

namespace {
constexpr const char* TAG = "SettingsApp";
}  // namespace

using namespace rodakos_settings;

bool SettingsApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    cloud_refresh_guard_ = std::make_shared<SettingsCloudRefreshGuard>();
    cloud_refresh_guard_->app.store(this);
    cloud_refresh_guard_->refresh_in_progress.store(false);
    cloud_refresh_guard_->refresh_generation.store(0);
    wifi_async_guard_ = std::make_shared<SettingsWiFiAsyncGuard>();
    wifi_async_guard_->app.store(this);
    wifi_async_guard_->generation.store(0);

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    if (!CreateUi()) {
        return false;
    }

    // Another app may open a page directly (Assistant -> Wi-Fi / 连接服务). Back from that page
    // returns to the caller rather than to the Settings main list.
    const std::string requested = context.navigation().TakeRequestedPage();
    if (requested == "wifi") {
        ShowPage(SettingsPage::kWiFiList);
        opened_for_page_ = true;
    } else if (requested == "cloud") {
        ShowPage(SettingsPage::kDeviceCloud);
        opened_for_page_ = true;
    } else if (requested == "datetime") {
        ShowPage(SettingsPage::kDateTime);
        opened_for_page_ = true;
    }

    ESP_LOGI(TAG, "Settings app created");
    return true;
}

bool SettingsApp::CreateUi() {
    // 创建根容器
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    CreateAppHeader(root_, "设置", [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->NavigateBack();
    }, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, this, &header_title_label_);

    main_body_ = lv_obj_create(root_);
    lv_obj_remove_style_all(main_body_);
    lv_obj_set_size(main_body_, LV_PCT(100), 200);
    lv_obj_set_pos(main_body_, 0, kRodakosAppHeaderHeight);
    lv_obj_set_style_bg_opa(main_body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(main_body_, 0, 0);

    // 创建主设置页面
    CreateMainPage();
    return true;
}

void SettingsApp::OnDestroy() {
    if (cloud_refresh_guard_) {
        cloud_refresh_guard_->refresh_generation.fetch_add(1);
        cloud_refresh_guard_->refresh_in_progress.store(false);
        cloud_refresh_guard_->app.store(nullptr);
    }
    if (wifi_async_guard_) {
        wifi_async_guard_->generation.fetch_add(1);
        wifi_async_guard_->app.store(nullptr);
    }
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            if (context_ != nullptr) lv_async_call_cancel(DeferReturnToCaller, context_);
            DestroyUi();
        }
    }
    context_ = nullptr;
    ui_ = nullptr;
    opened_for_page_ = false;
    wifi_async_guard_.reset();
}

void SettingsApp::ReloadUiForTheme() {
    if (ui_ == nullptr) {
        return;
    }
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return;
    }
    if (wifi_async_guard_) {
        wifi_async_guard_->generation.fetch_add(1);
    }
    DestroyUi();
    current_page_ = SettingsPage::kMain;
    CreateUi();
}

bool SettingsApp::OnThemeChanged(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    ReloadUiForTheme();
    return root_ != nullptr;
}

void SettingsApp::DestroyUi() {
    CloseUsbDiskDialog();
    ClosePasswordDialog();
    CloseForgetWiFiDialog();
    CloseButtonActionDialog();
    if (usb_disk_hint_page_ != nullptr && lv_obj_is_valid(usb_disk_hint_page_)) {
        lv_obj_delete(usb_disk_hint_page_);
    }
    usb_disk_hint_page_ = nullptr;
    CloseNtpServerDialog();
    CloseCloudProvisioningUrlDialog();
    CloseDeviceCloudUnbindDialog();
    if (lan_onboarding_timer_ != nullptr) {
        lv_timer_delete(lan_onboarding_timer_);
        lan_onboarding_timer_ = nullptr;
    }
    if (lan_onboarding_overlay_ != nullptr && lv_obj_is_valid(lan_onboarding_overlay_)) {
        lv_obj_delete(lan_onboarding_overlay_);
    }
    lan_onboarding_overlay_ = nullptr;
    // Leaving Settings mid-scan must release the camera and drop a parsed WiFi password.
    if (setup_qr_overlay_ != nullptr) TeardownSetupQr();
    if (time_sync_timer_ != nullptr) {
        lv_timer_delete(time_sync_timer_);
        time_sync_timer_ = nullptr;
    }
    if (datetime_clock_timer_ != nullptr) {
        lv_timer_delete(datetime_clock_timer_);
        datetime_clock_timer_ = nullptr;
    }
    CloseDateTimePicker();
    if (cloud_pairing_timer_ != nullptr) {
        lv_timer_delete(cloud_pairing_timer_);
        cloud_pairing_timer_ = nullptr;
    }
    if (brightness_sync_timer_ != nullptr) {
        lv_timer_delete(brightness_sync_timer_);
        brightness_sync_timer_ = nullptr;
    }
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
    ResetUiPointers();
}

void SettingsApp::ResetUiPointers() {
    root_ = nullptr;
    main_body_ = nullptr;
    wifi_body_ = nullptr;
    wifi_detail_body_ = nullptr;
    datetime_body_ = nullptr;
    system_shell_body_ = nullptr;
    buttons_body_ = nullptr;
    device_cloud_body_ = nullptr;
    brightness_label_ = nullptr;
    brightness_slider_ = nullptr;
    home_wifi_value_ = nullptr;
    home_cloud_value_ = nullptr;
    home_time_zone_value_ = nullptr;
    home_web_files_value_ = nullptr;
    usb_disk_dialog_ = nullptr;
    usb_disk_sd_label_ = nullptr;
    usb_disk_enter_button_ = nullptr;
    usb_disk_sd_ready_ = false;
    button_action_dialog_ = nullptr;
    usb_disk_hint_page_ = nullptr;
    header_title_label_ = nullptr;
    timezone_dropdown_ = nullptr;
    ntp_dropdown_ = nullptr;
    ntp_dialog_ = nullptr;
    ntp_textarea_ = nullptr;
    time_sync_status_label_ = nullptr;
    time_sync_button_ = nullptr;
    datetime_clock_label_ = nullptr;
    datetime_date_label_ = nullptr;
    datetime_picker_ = nullptr;
    ntp_error_label_ = nullptr;
    datetime_clock_timer_ = nullptr;
    time_sync_timer_ = nullptr;
    time_sync_in_progress_ = false;
    time_sync_poll_count_ = 0;
    cloud_status_label_ = nullptr;
    cloud_guide_label_ = nullptr;
    cloud_url_label_ = nullptr;
    cloud_client_id_label_ = nullptr;
    cloud_realtime_voice_label_ = nullptr;
    cloud_activation_label_ = nullptr;
    cloud_pairing_button_ = nullptr;
    cloud_pairing_button_label_ = nullptr;
    cloud_unbind_button_ = nullptr;
    cloud_unbind_dialog_ = nullptr;
    cloud_pairing_timer_ = nullptr;
    cloud_url_dialog_ = nullptr;
    cloud_url_textarea_ = nullptr;
    cloud_status_card_ = nullptr;
    cloud_status_dot_ = nullptr;
    cloud_summary_label_ = nullptr;
    cloud_details_link_ = nullptr;
    cloud_secondary_button_ = nullptr;
    cloud_secondary_button_label_ = nullptr;
    cloud_details_panel_ = nullptr;
    cloud_trust_label_ = nullptr;
    cloud_edit_url_button_ = nullptr;
    cloud_manual_pair_button_ = nullptr;
    cloud_details_open_ = false;
    lan_onboarding_overlay_ = nullptr;
    lan_onboarding_body_ = nullptr;
    lan_onboarding_countdown_ = nullptr;
    lan_onboarding_timer_ = nullptr;
    lan_onboarding_servers_.clear();
    setup_qr_overlay_ = nullptr;
    setup_qr_body_ = nullptr;
    setup_qr_progress_ = nullptr;
    setup_qr_wifi_label_ = nullptr;
    setup_qr_retry_button_ = nullptr;
    setup_qr_timer_ = nullptr;
    setup_qr_preview_image_ = nullptr;
    setup_qr_preview_placeholder_ = nullptr;
    cloud_pairing_error_.clear();
    cloud_pairing_code_.clear();
    web_files_page_.Reset();
    wifi_status_label_ = nullptr;
    wifi_list_container_ = nullptr;
    detail_ssid_label_ = nullptr;
    detail_status_label_ = nullptr;
    detail_ip_label_ = nullptr;
    detail_gateway_label_ = nullptr;
    detail_netmask_label_ = nullptr;
    detail_rssi_label_ = nullptr;
    wifi_detail_connect_button_ = nullptr;
}

void SettingsApp::ShowPage(SettingsPage page) {
    if (page == current_page_) {
        return;
    }

    CloseButtonActionDialog();
    current_page_ = page;
    if (cloud_pairing_timer_ != nullptr && page != SettingsPage::kDeviceCloud) {
        lv_timer_pause(cloud_pairing_timer_);
    }
    cloud_details_open_ = false;

    // 隐藏所有页面
    if (main_body_ != nullptr) {
        lv_obj_add_flag(main_body_, LV_OBJ_FLAG_HIDDEN);
    }
    if (wifi_body_ != nullptr) {
        lv_obj_add_flag(wifi_body_, LV_OBJ_FLAG_HIDDEN);
    }
    if (wifi_detail_body_ != nullptr) {
        lv_obj_add_flag(wifi_detail_body_, LV_OBJ_FLAG_HIDDEN);
    }
    if (datetime_body_ != nullptr) {
        lv_obj_add_flag(datetime_body_, LV_OBJ_FLAG_HIDDEN);
    }
    if (system_shell_body_ != nullptr) {
        lv_obj_add_flag(system_shell_body_, LV_OBJ_FLAG_HIDDEN);
    }
    if (buttons_body_ != nullptr) {
        lv_obj_add_flag(buttons_body_, LV_OBJ_FLAG_HIDDEN);
    }
    if (device_cloud_body_ != nullptr) {
        lv_obj_add_flag(device_cloud_body_, LV_OBJ_FLAG_HIDDEN);
    }
    web_files_page_.Hide();

    // 显示目标页面
    switch (page) {
        case SettingsPage::kMain:
            if (main_body_ != nullptr) {
                lv_obj_clear_flag(main_body_, LV_OBJ_FLAG_HIDDEN);
            }
            if (header_title_label_ != nullptr) {
                lv_label_set_text(header_title_label_, "设置");
            }
            UpdateMainPageStatus();
            break;

        case SettingsPage::kWiFiList:
            if (wifi_body_ == nullptr) {
                CreateWiFiListPage();
            }
            lv_obj_clear_flag(wifi_body_, LV_OBJ_FLAG_HIDDEN);
            if (header_title_label_ != nullptr) {
                lv_label_set_text(header_title_label_, "Wi-Fi");
            }
            StartWiFiScan();  // 自动扫描
            break;

        case SettingsPage::kWiFiDetail:
            if (wifi_detail_body_ == nullptr) {
                CreateWiFiDetailPage();
            }
            lv_obj_clear_flag(wifi_detail_body_, LV_OBJ_FLAG_HIDDEN);
            if (header_title_label_ != nullptr) {
                lv_label_set_text(header_title_label_, "Wi-Fi 详情");
            }
            UpdateWiFiDetailPage();  // 刷新数据
            break;

        case SettingsPage::kDateTime:
            if (datetime_body_ == nullptr) {
                datetime_server_cache_ = TimeServiceLoadNtpServer();
                CreateDateTimePage();
            }
            UpdateDateTimePage();
            lv_obj_clear_flag(datetime_body_, LV_OBJ_FLAG_HIDDEN);
            if (header_title_label_ != nullptr) {
                lv_label_set_text(header_title_label_, "日期与时间");
            }
            break;

        case SettingsPage::kSystemShell:
            if (system_shell_body_ == nullptr) {
                CreateSystemShellPage();
            }
            lv_obj_clear_flag(system_shell_body_, LV_OBJ_FLAG_HIDDEN);
            if (header_title_label_ != nullptr) {
                lv_label_set_text(header_title_label_, "锁屏与控制中心");
            }
            break;

        case SettingsPage::kButtons:
            if (buttons_body_ == nullptr) {
                CreateButtonBindingsPage();
            }
            lv_obj_clear_flag(buttons_body_, LV_OBJ_FLAG_HIDDEN);
            if (header_title_label_ != nullptr) {
                lv_label_set_text(header_title_label_, "按键");
            }
            UpdateButtonBindingsPage();
            break;

        case SettingsPage::kDeviceCloud:
            if (device_cloud_body_ == nullptr) {
                CreateDeviceCloudPage();
            }
            lv_obj_clear_flag(device_cloud_body_, LV_OBJ_FLAG_HIDDEN);
            if (header_title_label_ != nullptr) {
                lv_label_set_text(header_title_label_, "连接服务");
            }
            UpdateDeviceCloudPage();
            break;

        case SettingsPage::kWebFiles:
            web_files_page_.Create(main_body_, *context_, *ui_);
            web_files_page_.Show();
            if (header_title_label_ != nullptr) {
                lv_label_set_text(header_title_label_, "网页传文件");
            }
            web_files_page_.Update();
            break;
    }
}

void SettingsApp::NavigateBack() {
    if (current_page_ == SettingsPage::kDeviceCloud && cloud_details_open_) {
        ShowDeviceCloudDetails(false);
        return;
    }
    if (current_page_ == SettingsPage::kWiFiDetail) {
        ShowPage(SettingsPage::kWiFiList);
        return;
    }
    if (current_page_ == SettingsPage::kWiFiList ||
        current_page_ == SettingsPage::kDateTime ||
        current_page_ == SettingsPage::kSystemShell ||
        current_page_ == SettingsPage::kButtons ||
        current_page_ == SettingsPage::kDeviceCloud ||
        current_page_ == SettingsPage::kWebFiles) {
        if (opened_for_page_) {
            opened_for_page_ = false;
            lv_async_call(DeferReturnToCaller, context_);
            return;
        }
        ShowPage(SettingsPage::kMain);
        return;
    }
    NavigateHome();
}

bool SettingsApp::OnBackRequested() {
    if (setup_qr_overlay_ != nullptr) {
        CloseSetupQr();
        return true;
    }
    if (lan_onboarding_overlay_ != nullptr) {
        CloseLanOnboarding();
        return true;
    }
    // Full-screen layers close first, so back never leaves one on top of another page.
    if (datetime_picker_ != nullptr) {
        CloseDateTimePicker();
        return true;
    }
    if (ntp_dialog_ != nullptr) {
        CloseNtpServerDialog();
        return true;
    }
    if (password_dialog_ != nullptr) {
        ClosePasswordDialog();
        return true;
    }
    if (wifi_forget_dialog_ != nullptr) {
        CloseForgetWiFiDialog();
        return true;
    }
    NavigateBack();
    return true;
}

void SettingsApp::NavigateHome() {
    ESP_LOGI(TAG, "Header home button returning home");
    lv_async_call(DeferReturnHome, context_);
}

void RegisterSettingsApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "settings",
        .title = "Settings",
        .icon = ICON_SETTINGS,
        .category = PhoneAppCategory::kSystem,
        .capabilities = PhoneCapability::kNone,
        .show_on_home = true,
        .aliases = {"config", "preferences", "设置"},
        .create = []() { return std::make_unique<SettingsApp>(); },
    });
}

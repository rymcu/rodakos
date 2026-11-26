#include "apps/settings/settings_app.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_ui.h"
#include "fake_cloud.h"
#include "settings.h"
#include "test_framework.h"
#include <freertos/task.h>
#include <esp_lvgl_port.h>
#include <src/others/test/lv_test.h>
#include <string>

namespace {
void Pump() { lv_test_wait(50); lv_obj_update_layout(lv_screen_active()); }
struct Fixture {
    PhoneUi ui{320, 240};
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context{ui, navigation, registry, services, settings};
    rodakos::DeviceCloudConfigService cloud;
    WiFiAdapter wifi;
    SettingsApp app;
    bool destroyed = false;
    unsigned bound_notifications = 0;
    Fixture() {
        cloud_ui_test::Reset();
        cloud_ui_test::task_create_ok = true;
        cloud_ui_test::ui_lock_available = true;
        cloud_ui_test::config.aiot_registered = true;
        cloud_ui_test::config.aiot_activated = true;
        cloud_ui_test::state.code = rodakos::CloudDiagnosticCode::kCredentialsExpired;
        services.SetDeviceCloud(&cloud); services.SetWiFi(&wifi);
        services.SetDeviceCloudBoundCallback([&] { ++bound_notifications; });
        ui.SetThemeName("dark");
        RODAK_CHECK(app.OnCreate(context)); Pump();
        app.ShowPage(SettingsPage::kDeviceCloud); Pump();
    }
    ~Fixture() {
        if (!destroyed) app.OnDestroy();
        cloud_ui_test::RunTask();
        // PhoneUi lives for the whole device boot; drain its toast before this
        // stack fixture ends, while its normal application callback is still valid.
        lv_test_wait(1850); Pump(); lv_obj_clean(lv_screen_active());
    }
};
}

RODAK_TEST("A bound device with expired credentials has a clickable retry separate from unbind") {
    Fixture f;
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.cloud_status_label_)), "设备凭据需要更新");
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app.cloud_pairing_button_, LV_OBJ_FLAG_HIDDEN));
    // 重试 is the primary action; 解除连接 is reached through 详情, never next to 重试.
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app.cloud_secondary_button_, LV_OBJ_FLAG_HIDDEN));
    lv_area_t retry;
    lv_obj_scroll_to_view(f.app.cloud_pairing_button_, LV_ANIM_OFF); Pump();
    lv_obj_get_coords(f.app.cloud_pairing_button_, &retry);
    RODAK_CHECK(retry.y1 >= 40); RODAK_CHECK(retry.y2 < 240);
    lv_test_mouse_click_at((retry.x1 + retry.x2) / 2, (retry.y1 + retry.y2) / 2);
    RODAK_CHECK(f.app.cloud_refresh_guard_->refresh_in_progress.load());
    cloud_ui_test::config.has_aiot_config = true;
    cloud_ui_test::state.code = rodakos::CloudDiagnosticCode::kVoiceUnavailable;
    cloud_ui_test::RunTask(); Pump();
    RODAK_CHECK_EQ(cloud_ui_test::refreshes, 1U);
    RODAK_CHECK_EQ(cloud_ui_test::unbinds, 0U);
    RODAK_CHECK_EQ(f.bound_notifications, 1U);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.cloud_status_label_)), "语音服务不可用");
}

RODAK_TEST("A rejected credential refresh keeps the binding and never displays raw server errors") {
    Fixture f;
    cloud_ui_test::refresh_ok = false;
    cloud_ui_test::state.code = rodakos::CloudDiagnosticCode::kCredentialsRejected;
    f.app.RefreshDeviceCloud(); cloud_ui_test::RunTask(); Pump();
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.cloud_status_label_)), "设备凭据被拒绝");
    RODAK_CHECK_EQ(f.app.cloud_pairing_error_, "设备凭据被拒绝");
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app.cloud_unbind_button_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK_FALSE(f.app.cloud_refresh_guard_->refresh_in_progress.load());
    RODAK_CHECK_EQ(f.bound_notifications, 0U);
    cloud_ui_test::refresh_ok = true;
    cloud_ui_test::config.has_aiot_config = true;
    cloud_ui_test::state.code = rodakos::CloudDiagnosticCode::kReady;
    f.app.RefreshDeviceCloud(); cloud_ui_test::RunTask(); Pump();
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.cloud_status_label_)), "已连接");
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.cloud_pairing_button_label_)), "详情");
}

RODAK_TEST("Settings destruction ignores delayed cloud completion without changing the voice lifecycle") {
    Fixture f;
    f.app.RefreshDeviceCloud();
    f.app.OnDestroy(); f.destroyed = true;
    cloud_ui_test::config.has_aiot_config = true;
    cloud_ui_test::RunTask(); Pump();
    RODAK_CHECK_EQ(f.bound_notifications, 0U);
    RODAK_CHECK_EQ(cloud_ui_test::unbinds, 0U);
}

RODAK_TEST("A stale refresh generation cannot publish a callback or clear the current operation") {
    Fixture f;
    f.app.RefreshDeviceCloud();
    f.app.cloud_refresh_guard_->refresh_generation.fetch_add(1);
    cloud_ui_test::config.has_aiot_config = true;
    cloud_ui_test::RunTask(); Pump();
    RODAK_CHECK_EQ(f.bound_notifications, 0U);
    RODAK_CHECK(f.app.cloud_refresh_guard_->refresh_in_progress.load());
}

RODAK_TEST("Settings refresh task allocation failure immediately leaves retry available") {
    Fixture f;
    cloud_ui_test::state = {rodakos::CloudDiagnosticCode::kCredentialsRejected, 100, 17};
    cloud_ui_test::task_create_ok = false;
    f.app.RefreshDeviceCloud(); Pump();
    RODAK_CHECK_FALSE(f.app.cloud_refresh_guard_->refresh_in_progress.load());
    RODAK_CHECK_FALSE(lv_obj_has_state(f.app.cloud_pairing_button_, LV_STATE_DISABLED));
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app.cloud_pairing_button_, LV_OBJ_FLAG_HIDDEN));
    f.app.UpdateDeviceCloudPage();
    f.app.ShowPage(SettingsPage::kMain);
    lv_test_wait(4050);
    f.app.ShowPage(SettingsPage::kDeviceCloud); Pump();
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.cloud_status_label_)), "无法开始连接");
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.cloud_guide_label_)), "请稍后点「重试」。");
    RODAK_CHECK_EQ(cloud_ui_test::refreshes, 0U);
    RODAK_CHECK_EQ(cloud_ui_test::unbinds, 0U);
    RODAK_CHECK(cloud_ui_test::state.code == rodakos::CloudDiagnosticCode::kCredentialsRejected);
    RODAK_CHECK_EQ(cloud_ui_test::state.revision, 17U);
    RODAK_CHECK(cloud_ui_test::config.aiot_registered && cloud_ui_test::config.aiot_activated);
    RODAK_CHECK(f.app.OnThemeChanged(f.context));
    f.app.ShowPage(SettingsPage::kDeviceCloud); Pump();
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.cloud_status_label_)), "无法开始连接");
    cloud_ui_test::task_create_ok = true;
    f.app.RefreshDeviceCloud();
    f.app.RefreshDeviceCloud();
    RODAK_CHECK(f.app.cloud_refresh_guard_->refresh_failure.load() == SettingsCloudRefreshFailure::kNone);
    cloud_ui_test::config.has_aiot_config = true;
    cloud_ui_test::state.code = rodakos::CloudDiagnosticCode::kReady;
    cloud_ui_test::RunTask(); Pump();
    RODAK_CHECK_EQ(cloud_ui_test::refreshes, 1U);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.cloud_status_label_)), "已连接");
}

RODAK_TEST("Lost refresh callback is recovered by the UI timer and a later retry can complete") {
    Fixture f;
    f.app.RefreshDeviceCloud();
    cloud_ui_test::config.has_aiot_config = true;
    cloud_ui_test::state.code = rodakos::CloudDiagnosticCode::kReady;
    cloud_ui_test::ui_lock_available = false;
    cloud_ui_test::RunTask();
    cloud_ui_test::ui_lock_available = true;
    lv_test_wait(2050); Pump();
    RODAK_CHECK_FALSE(f.app.cloud_refresh_guard_->refresh_in_progress.load());
    RODAK_CHECK_FALSE(lv_obj_has_state(f.app.cloud_pairing_button_, LV_STATE_DISABLED));
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app.cloud_pairing_button_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.cloud_status_label_)), "未收到连接结果");
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.cloud_guide_label_)), "请稍后点「重试」。");
    RODAK_CHECK(cloud_ui_test::state.code == rodakos::CloudDiagnosticCode::kReady);
    RODAK_CHECK_EQ(f.bound_notifications, 0U);
    lv_test_wait(4050); Pump();
    RODAK_CHECK_EQ(cloud_ui_test::refreshes, 1U);
    f.app.RefreshDeviceCloud(); cloud_ui_test::RunTask(); Pump();
    RODAK_CHECK_EQ(f.bound_notifications, 1U);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.cloud_pairing_button_label_)), "详情");
}

RODAK_TEST("A rejected UI completion remains distinct from an older rejected credential diagnosis") {
    Fixture f;
    cloud_ui_test::state = {rodakos::CloudDiagnosticCode::kCredentialsRejected, 100, 29};
    cloud_ui_test::refresh_ok = false;
    f.app.RefreshDeviceCloud();
    cloud_ui_test::reject_next_async_call = true;
    cloud_ui_test::RunTask();
    lv_test_wait(2050); Pump();
    f.app.ShowPage(SettingsPage::kMain);
    f.app.ShowPage(SettingsPage::kDeviceCloud); Pump();
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.cloud_status_label_)), "未收到连接结果");
    RODAK_CHECK_FALSE(lv_obj_has_state(f.app.cloud_pairing_button_, LV_STATE_DISABLED));
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app.cloud_pairing_button_, LV_OBJ_FLAG_HIDDEN));
    lv_test_wait(4050); Pump();
    RODAK_CHECK_EQ(cloud_ui_test::refreshes, 1U);
    RODAK_CHECK_EQ(cloud_ui_test::unbinds, 0U);
    RODAK_CHECK(cloud_ui_test::state.code == rodakos::CloudDiagnosticCode::kCredentialsRejected);
    RODAK_CHECK_EQ(cloud_ui_test::state.revision, 29U);
    RODAK_CHECK(cloud_ui_test::config.aiot_registered && cloud_ui_test::config.aiot_activated);
}

RODAK_TEST("A stale worker delivery failure cannot replace the current UI failure or clear busy") {
    Fixture f;
    f.app.RefreshDeviceCloud();
    f.app.cloud_refresh_guard_->refresh_generation.fetch_add(1);
    f.app.cloud_refresh_guard_->refresh_failure.store(SettingsCloudRefreshFailure::kTaskStart);
    cloud_ui_test::ui_lock_available = false;
    cloud_ui_test::RunTask();
    cloud_ui_test::ui_lock_available = true;
    RODAK_CHECK(f.app.cloud_refresh_guard_->refresh_in_progress.load());
    RODAK_CHECK(f.app.cloud_refresh_guard_->refresh_failure.load() == SettingsCloudRefreshFailure::kTaskStart);
    RODAK_CHECK_EQ(f.bound_notifications, 0U);
}

RODAK_TEST("A page requested by another app opens first and back returns to that app") {
    cloud_ui_test::Reset();
    cloud_ui_test::task_create_ok = true;
    cloud_ui_test::ui_lock_available = true;
    // Bound and ready, so the 连接服务 page does not start its pairing poll.
    cloud_ui_test::config.aiot_registered = true;
    cloud_ui_test::config.aiot_activated = true;
    cloud_ui_test::state.code = rodakos::CloudDiagnosticCode::kReady;
    PhoneUi ui{320, 240};
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context{ui, navigation, registry, services, settings};
    rodakos::DeviceCloudConfigService cloud;
    WiFiAdapter wifi;
    services.SetDeviceCloud(&cloud); services.SetWiFi(&wifi);
    ui.SetThemeName("dark");
    {
        SettingsApp app;
        navigation.requested_page = "cloud";
        RODAK_CHECK(app.OnCreate(context)); Pump();
        RODAK_CHECK(app.current_page_ == SettingsPage::kDeviceCloud);
        // The page is built inside OnCreate, before any layout pass; it must still get the
        // body's 320x200 box, not 0x0 (which showed a blank page on the device).
        lv_area_t body;
        lv_obj_get_coords(app.device_cloud_body_, &body);
        RODAK_CHECK_EQ(lv_area_get_width(&body), 320);
        RODAK_CHECK_EQ(lv_area_get_height(&body), 200);
        RODAK_CHECK_EQ(body.y1, 40);
        RODAK_CHECK_FALSE(lv_obj_has_flag(app.device_cloud_body_, LV_OBJ_FLAG_HIDDEN));
        lv_area_t card;
        lv_obj_get_coords(app.cloud_status_card_, &card);
        RODAK_CHECK(lv_area_get_width(&card) > 200);
        RODAK_CHECK(card.y1 >= 40 && card.y2 < 240);
        RODAK_CHECK(navigation.requested_page.empty());
        RODAK_CHECK(app.OnBackRequested());
        Pump();
        RODAK_CHECK_EQ(navigation.caller_count, 1);
        RODAK_CHECK_EQ(navigation.launches.size(), 0U);
        app.OnDestroy();
    }
    {
        // Opened normally: back from a sub-page returns to the Settings list, not to a caller.
        SettingsApp app;
        RODAK_CHECK(app.OnCreate(context)); Pump();
        RODAK_CHECK(app.current_page_ == SettingsPage::kMain);
        app.ShowPage(SettingsPage::kDeviceCloud); Pump();
        RODAK_CHECK(app.OnBackRequested());
        Pump();
        RODAK_CHECK(app.current_page_ == SettingsPage::kMain);
        RODAK_CHECK_EQ(navigation.caller_count, 1);
        app.OnDestroy();
    }
    {
        // Destroyed before the deferred return runs: nothing navigates afterwards.
        SettingsApp app;
        navigation.requested_page = "cloud";
        RODAK_CHECK(app.OnCreate(context)); Pump();
        RODAK_CHECK(app.OnBackRequested());
        app.OnDestroy();
        Pump();
        RODAK_CHECK_EQ(navigation.caller_count, 1);
    }
    cloud_ui_test::RunTask();
    lv_test_wait(1850); Pump(); lv_obj_clean(lv_screen_active());
}

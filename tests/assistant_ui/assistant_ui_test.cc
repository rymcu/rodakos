#include "apps/assistant/assistant_app.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/device_cloud_config.h"
#include "phone_os/voice_assistant_service.h"
#include "phone_os/voice_wake_service.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "phone_ui/phone_ui.h"
#include "settings.h"
#include "test_framework.h"
#include "fake_cloud.h"
#include <src/others/test/lv_test.h>
#include <string>

namespace {
void Pump() { lv_test_wait(550); lv_obj_update_layout(lv_screen_active()); }
lv_obj_t* Label(lv_obj_t* parent, const char* text) {
    if (lv_obj_check_type(parent, &lv_label_class) && std::string(lv_label_get_text(parent)) == text) return parent;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        if (auto* found = Label(lv_obj_get_child(parent, i), text)) return found;
    }
    return nullptr;
}
struct Fixture {
    PhoneUi ui{320, 240};
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context{ui, navigation, registry, services, settings};
    rodakos::VoiceAssistantService assistant;
    rodakos::VoiceWakeService wake;
    rodakos::DeviceCloudConfigService cloud;
    WiFiAdapter wifi;
    AssistantApp app;
    bool destroyed = false;
    Fixture() {
        cloud_ui_test::Reset();
        services.SetVoiceAssistant(&assistant); services.SetVoiceWake(&wake);
        services.SetDeviceCloud(&cloud); services.SetWiFi(&wifi);
        ui.SetThemeName("dark");
        RODAK_CHECK(app.OnCreate(context)); Pump();
    }
    ~Fixture() { if (!destroyed) app.OnDestroy(); Pump(); lv_obj_clean(lv_screen_active()); }
};
}

RODAK_TEST("Assistant shows fixed cloud diagnosis and recovery guidance") {
    Fixture f;
    for (auto code : {rodakos::CloudDiagnosticCode::kUnconfigured,
                      rodakos::CloudDiagnosticCode::kCredentialsRejected,
                      rodakos::CloudDiagnosticCode::kCredentialsExpired,
                      rodakos::CloudDiagnosticCode::kRefreshFailed,
                      rodakos::CloudDiagnosticCode::kVoiceUnavailable,
                      rodakos::CloudDiagnosticCode::kTrustUnavailable}) {
        cloud_ui_test::state = {code, 100};
        f.assistant.state.message = "raw-secret-token-response";
        f.app.RefreshState();
        RODAK_CHECK(Label(lv_screen_active(), rodakos::CloudDiagnosticTitle(code)) != nullptr);
        auto* hint = Label(lv_screen_active(), rodakos::CloudDiagnosticHint(code));
        RODAK_CHECK(hint != nullptr);
        lv_obj_update_layout(hint);
        lv_area_t hint_area, card_area;
        lv_obj_get_coords(hint, &hint_area);
        lv_obj_get_coords(lv_obj_get_parent(hint), &card_area);
        RODAK_CHECK(hint_area.y1 >= card_area.y1 && hint_area.y2 <= card_area.y2);
        RODAK_CHECK(hint_area.x1 >= card_area.x1 && hint_area.x2 <= card_area.x2);
        RODAK_CHECK(Label(lv_screen_active(), "raw-secret-token-response") == nullptr);
    }
    RODAK_CHECK_EQ(f.assistant.initializations, 1U);
    RODAK_CHECK_EQ(f.wake.changes, 0U);
}

RODAK_TEST("Assistant recovery control launches existing Settings only after the click") {
    Fixture f;
    auto* label = Label(lv_screen_active(), "Settings > Device Cloud");
    RODAK_CHECK(label != nullptr);
    lv_obj_update_layout(label);
    lv_area_t area; lv_obj_get_coords(lv_obj_get_parent(label), &area);
    RODAK_CHECK(area.y2 < 240);
    RODAK_CHECK(f.navigation.launches.empty());
    lv_test_mouse_click_at((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
    Pump();
    RODAK_CHECK_EQ(f.navigation.launches.size(), 1U);
    RODAK_CHECK_EQ(f.navigation.launches.front(), "settings");
    RODAK_CHECK_EQ(f.wake.changes, 0U);
}

RODAK_TEST("Destroy cancels a queued Settings navigation and diagnostic polling") {
    Fixture f;
    auto* label = Label(lv_screen_active(), "Settings > Device Cloud");
    lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, nullptr);
    f.app.OnDestroy(); f.destroyed = true; Pump();
    RODAK_CHECK(f.navigation.launches.empty());
}

RODAK_TEST("A newer cloud recovery replaces stale assistant failure and disconnected WiFi remains visible") {
    Fixture f;
    f.assistant.state.phase = rodakos::VoiceAssistantPhase::kError;
    f.assistant.state.diagnostic = rodakos::CloudDiagnosticCode::kCredentialsRejected;
    f.assistant.state.diagnostic_at_ms = 100;
    f.assistant.state.diagnostic_revision = rodakos::NextCloudDiagnosticRevision();
    cloud_ui_test::state = {rodakos::CloudDiagnosticCode::kReady, 100, rodakos::NextCloudDiagnosticRevision()};
    f.app.RefreshState();
    RODAK_CHECK(Label(lv_screen_active(), "Ready for wake") != nullptr);
    RODAK_CHECK(Label(lv_screen_active(), "Assistant - Ready") != nullptr);
    f.wifi.status = WiFiStatus::kDisconnected; f.app.RefreshState();
    RODAK_CHECK(Label(lv_screen_active(), "Server unreachable") != nullptr);
}

RODAK_TEST("Cloud and voice revisions order errors and recovery within the same millisecond") {
    Fixture f;
    cloud_ui_test::state = {rodakos::CloudDiagnosticCode::kReady, 100, rodakos::NextCloudDiagnosticRevision()};
    f.assistant.state.phase = rodakos::VoiceAssistantPhase::kError;
    f.assistant.state.diagnostic = rodakos::CloudDiagnosticCode::kVoiceUnavailable;
    f.assistant.state.diagnostic_at_ms = 100;
    f.assistant.state.diagnostic_revision = rodakos::NextCloudDiagnosticRevision();
    f.app.RefreshState();
    RODAK_CHECK(Label(lv_screen_active(), "Voice service unavailable") != nullptr);
    for (auto code : {rodakos::CloudDiagnosticCode::kCredentialsRejected,
                      rodakos::CloudDiagnosticCode::kRefreshing,
                      rodakos::CloudDiagnosticCode::kCredentialsExpired,
                      rodakos::CloudDiagnosticCode::kReady}) {
        cloud_ui_test::state = {code, 100, rodakos::NextCloudDiagnosticRevision()};
        f.app.RefreshState();
        RODAK_CHECK(Label(lv_screen_active(), rodakos::CloudDiagnosticTitle(code)) != nullptr);
    }
    RODAK_CHECK(rodakos::IsNewerCloudDiagnostic(1, UINT32_MAX));
    RODAK_CHECK_FALSE(rodakos::IsNewerCloudDiagnostic(UINT32_MAX, 1));
}

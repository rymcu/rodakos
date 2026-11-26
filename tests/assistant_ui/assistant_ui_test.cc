#include "apps/assistant/assistant_app.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/device_cloud_config.h"
#include "phone_os/voice_assistant_service.h"
#include "phone_os/voice_status_light_service.h"
#include "phone_os/voice_wake_service.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "settings.h"
#include "test_framework.h"
#include "fake_cloud.h"
#include <src/others/test/lv_test.h>
#include <fstream>
#include <string>

namespace {
void Pump() { lv_test_wait(550); lv_obj_update_layout(lv_screen_active()); }
lv_obj_t* Label(lv_obj_t* parent, const char* text) {
    if (lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(parent, &lv_label_class) && std::string(lv_label_get_text(parent)) == text) return parent;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        if (auto* found = Label(lv_obj_get_child(parent, i), text)) return found;
    }
    return nullptr;
}
bool Shows(const char* text) { return Label(lv_screen_active(), text) != nullptr; }
void Screenshot(const char* name) {
    lv_test_wait(300);
    lv_obj_update_layout(lv_screen_active());
    const auto* frame = lv_display_get_buf_active(lv_display_get_default());
    std::ofstream out(name, std::ios::binary);
    out << "P6\n320 240\n255\n";
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
            const auto* p = frame->data + y * frame->header.stride + x * 4;
            const char rgb[] = {static_cast<char>(p[2]), static_cast<char>(p[1]), static_cast<char>(p[0])};
            out.write(rgb, 3);
        }
    }
}
// A label must fit its own box (LONG_DOT would otherwise cut it) and stay on screen.
void CheckFits(lv_obj_t* label) {
    RODAK_CHECK(label != nullptr);
    lv_obj_update_layout(label);
    lv_area_t area;
    lv_obj_get_coords(label, &area);
    RODAK_CHECK(area.x1 >= 0 && area.x2 < 320 && area.y2 < 240);
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
    rodakos::VoiceStatusLightService light;
    WiFiAdapter wifi;
    AssistantApp app;
    bool destroyed = false;
    Fixture() {
        cloud_ui_test::Reset();
        cloud_ui_test::state = {rodakos::CloudDiagnosticCode::kReady, 100, rodakos::NextCloudDiagnosticRevision()};
        wake.state.enabled = true;
        wake.state.status = rodakos::VoiceWakeStatus::kListening;
        services.SetVoiceAssistant(&assistant); services.SetVoiceWake(&wake);
        services.SetDeviceCloud(&cloud); services.SetWiFi(&wifi);
        services.SetVoiceStatusLight(&light);
        rodakos_theme_init_from_name("dark");
        ui.SetThemeName("dark");
        RODAK_CHECK(app.OnCreate(context)); Pump();
    }
    ~Fixture() {
        if (!destroyed) app.OnDestroy();
        // PhoneUi lives for the whole device boot; drain its toast timer before this stack
        // fixture ends, as the Settings fixture does.
        lv_test_wait(1850);
        Pump();
        lv_obj_clean(lv_screen_active());
    }
    void Diagnose(rodakos::CloudDiagnosticCode code) {
        cloud_ui_test::state = {code, 100, rodakos::NextCloudDiagnosticRevision()};
        app.RefreshState();
    }
};
}

RODAK_TEST("Ready state names the confirmed wake word and the three setting rows") {
    Fixture f;
    RODAK_CHECK(Shows("语音助手"));
    RODAK_CHECK(Shows("待命中"));
    RODAK_CHECK(Shows("说「你好达克」开始对话"));
    RODAK_CHECK(Shows("语音唤醒"));
    RODAK_CHECK(Shows("语音状态灯"));
    RODAK_CHECK(Shows("30%"));
    RODAK_CHECK(Shows("连接服务"));
    RODAK_CHECK(Shows("已连接"));
    RODAK_CHECK_FALSE(Shows("去处理"));
    Screenshot("assistant-ready.ppm");

    f.wake.state.voice_identity.wake_word = "小罗同学";
    f.app.RefreshState();
    RODAK_CHECK(Shows("说「小罗同学」开始对话"));
    RODAK_CHECK_EQ(f.assistant.initializations, 1U);
    RODAK_CHECK_EQ(f.wake.changes, 0U);
}

RODAK_TEST("A voice turn shows each phase and the turn progress like Home does") {
    Fixture f;
    f.assistant.state.phase = rodakos::VoiceAssistantPhase::kConnecting;
    f.app.RefreshState();
    RODAK_CHECK(Shows("正在连接"));
    RODAK_CHECK(Shows("对话中"));
    f.assistant.state.phase = rodakos::VoiceAssistantPhase::kListening;
    f.app.RefreshState();
    RODAK_CHECK(Shows("正在聆听"));
    f.assistant.state.awaiting_reply = true;
    f.app.RefreshState();
    RODAK_CHECK(Shows("已收到"));
    RODAK_CHECK(Shows("等待罗达克回复…"));
    f.assistant.state.turn_stage = rodakos::VoiceTurnStage::kThinking;
    f.app.RefreshState();
    RODAK_CHECK(Shows("正在思考"));
    Screenshot("assistant-thinking.ppm");
    f.assistant.state.turn_stage = rodakos::VoiceTurnStage::kTool;
    f.app.RefreshState();
    RODAK_CHECK(Shows("正在处理"));
    f.assistant.state.turn_stage = rodakos::VoiceTurnStage::kComposing;
    f.app.RefreshState();
    RODAK_CHECK(Shows("正在组织回答"));
    f.assistant.state.turn_label = "正在查询明天的天气";
    f.app.RefreshState();
    RODAK_CHECK(Shows("正在查询明天的天气"));
    RODAK_CHECK_FALSE(Shows("正在组织回答"));
    f.assistant.state.awaiting_reply = false;
    f.assistant.state.turn_label.clear();
    f.assistant.state.phase = rodakos::VoiceAssistantPhase::kSpeaking;
    f.app.RefreshState();
    RODAK_CHECK(Shows("正在回复"));
    f.assistant.state.announce = true;
    f.app.RefreshState();
    RODAK_CHECK(Shows("正在播报"));
    f.assistant.state = {};
    f.app.RefreshState();
    RODAK_CHECK(Shows("待命中"));
    RODAK_CHECK_FALSE(Shows("对话中"));
}

RODAK_TEST("A turn in progress keeps priority over a stale cloud failure") {
    Fixture f;
    f.Diagnose(rodakos::CloudDiagnosticCode::kCredentialsRejected);
    RODAK_CHECK(Shows("设备凭据被拒绝"));
    f.assistant.state.phase = rodakos::VoiceAssistantPhase::kListening;
    f.app.RefreshState();
    RODAK_CHECK(Shows("正在聆听"));
    RODAK_CHECK_FALSE(Shows("去处理"));
}

RODAK_TEST("Cloud diagnoses use the shared Chinese titles and route the fix button") {
    Fixture f;
    struct Case {
        rodakos::CloudDiagnosticCode code;
        const char* title;
        const char* fix;
    };
    const Case cases[] = {
        {rodakos::CloudDiagnosticCode::kUnconfigured, "尚未连接", "去处理"},
        {rodakos::CloudDiagnosticCode::kCredentialsRejected, "设备凭据被拒绝", "去处理"},
        {rodakos::CloudDiagnosticCode::kCredentialsExpired, "设备凭据需要更新", "去处理"},
        {rodakos::CloudDiagnosticCode::kRefreshFailed, "凭据更新失败", "去处理"},
        {rodakos::CloudDiagnosticCode::kNetworkUnavailable, "无法连接服务器", "去处理"},
        {rodakos::CloudDiagnosticCode::kVoiceUnavailable, "语音服务不可用", "去处理"},
        {rodakos::CloudDiagnosticCode::kTrustUnavailable, "服务器证书需要处理", "去处理"},
        {rodakos::CloudDiagnosticCode::kDeviceMemoryLow, "设备内存不足", nullptr},
        {rodakos::CloudDiagnosticCode::kRefreshing, "正在更新凭据", nullptr},
        {rodakos::CloudDiagnosticCode::kCancelled, "连接已取消", nullptr},
    };
    for (const auto& c : cases) {
        f.assistant.state.message = "raw-secret-token-response";
        f.Diagnose(c.code);
        RODAK_CHECK(Shows(c.title));
        RODAK_CHECK(c.fix == nullptr ? !Shows("去处理") : Shows(c.fix));
        RODAK_CHECK_FALSE(Shows("待命中"));
        RODAK_CHECK_FALSE(Shows("raw-secret-token-response"));
        CheckFits(Label(lv_screen_active(), c.title));
    }
    RODAK_CHECK(f.navigation.launches.empty());
    RODAK_CHECK_EQ(f.wake.changes, 0U);
}

RODAK_TEST("No Wi-Fi sends the fix to the Wi-Fi page; server errors go to 连接服务") {
    Fixture f;
    f.wifi.status = WiFiStatus::kDisconnected;
    f.Diagnose(rodakos::CloudDiagnosticCode::kReady);
    RODAK_CHECK(Shows("未连接 Wi-Fi"));
    RODAK_CHECK(Shows("去连接"));
    Screenshot("assistant-no-wifi.ppm");
    lv_obj_send_event(lv_obj_get_parent(Label(lv_screen_active(), "去连接")), LV_EVENT_CLICKED, nullptr);
    Pump();
    RODAK_CHECK_EQ(f.navigation.launches.size(), 1U);
    RODAK_CHECK_EQ(f.navigation.launches.back(), "settings");
    RODAK_CHECK_EQ(f.navigation.callers.back(), "assistant");
    RODAK_CHECK_EQ(f.navigation.pages.back(), "wifi");

    f.wifi.status = WiFiStatus::kConnected;
    f.Diagnose(rodakos::CloudDiagnosticCode::kNetworkUnavailable);
    RODAK_CHECK(Shows("无法连接服务器"));
    RODAK_CHECK(Shows("服务器无响应"));
    lv_obj_send_event(lv_obj_get_parent(Label(lv_screen_active(), "去处理")), LV_EVENT_CLICKED, nullptr);
    Pump();
    RODAK_CHECK_EQ(f.navigation.pages.back(), "cloud");
    Screenshot("assistant-server.ppm");

    // Setup and trust problems are not hidden behind the Wi-Fi state.
    f.wifi.status = WiFiStatus::kDisconnected;
    f.Diagnose(rodakos::CloudDiagnosticCode::kTrustUnavailable);
    RODAK_CHECK(Shows("服务器证书需要处理"));
}

RODAK_TEST("The 连接服务 row opens the Settings page for it") {
    Fixture f;
    lv_obj_send_event(lv_obj_get_parent(Label(lv_screen_active(), "连接服务")), LV_EVENT_CLICKED, nullptr);
    RODAK_CHECK(f.navigation.launches.empty());
    Pump();
    RODAK_CHECK_EQ(f.navigation.launches.size(), 1U);
    RODAK_CHECK_EQ(f.navigation.pages.back(), "cloud");
}

RODAK_TEST("Destroy cancels a queued Settings navigation") {
    Fixture f;
    lv_obj_send_event(lv_obj_get_parent(Label(lv_screen_active(), "连接服务")), LV_EVENT_CLICKED, nullptr);
    f.app.OnDestroy(); f.destroyed = true; Pump();
    RODAK_CHECK(f.navigation.launches.empty());
}

RODAK_TEST("Wake row toggles listening; off and music-paused read differently") {
    Fixture f;
    lv_obj_send_event(lv_obj_get_parent(Label(lv_screen_active(), "语音唤醒")), LV_EVENT_CLICKED, nullptr);
    RODAK_CHECK_EQ(f.wake.changes, 1U);
    RODAK_CHECK_FALSE(f.wake.state.enabled);
    f.wake.state.status = rodakos::VoiceWakeStatus::kDisabled;
    f.app.RefreshState();
    RODAK_CHECK(Shows("唤醒已关闭"));
    Screenshot("assistant-wake-off.ppm");

    f.wake.state.enabled = true;
    f.app.RefreshState();
    RODAK_CHECK(Shows("唤醒已暂停"));
    RODAK_CHECK(Shows("已暂停"));

    // A cloud problem still outranks the wake guidance.
    f.wake.state.enabled = false;
    f.Diagnose(rodakos::CloudDiagnosticCode::kCredentialsRejected);
    RODAK_CHECK(Shows("设备凭据被拒绝"));
    RODAK_CHECK_FALSE(Shows("唤醒已关闭"));
}

RODAK_TEST("A missing wake runtime is reported instead of a ready state") {
    Fixture f;
    f.wake.state.status = rodakos::VoiceWakeStatus::kUnavailable;
    f.wake.state.runtime_available = false;
    f.app.RefreshState();
    RODAK_CHECK(Shows("唤醒不可用"));
    RODAK_CHECK(Shows("唤醒模型未安装"));
}

RODAK_TEST("Status light row saves through the service and shows its level") {
    Fixture f;
    lv_obj_send_event(lv_obj_get_parent(Label(lv_screen_active(), "语音状态灯")), LV_EVENT_CLICKED, nullptr);
    Pump();
    RODAK_CHECK_EQ(f.light.saves, 1);
    RODAK_CHECK_FALSE(f.light.current.enabled);
    RODAK_CHECK(Shows("已关闭"));
    f.light.save_ok = false;
    lv_obj_send_event(lv_obj_get_parent(Label(lv_screen_active(), "语音状态灯")), LV_EVENT_CLICKED, nullptr);
    Pump();
    RODAK_CHECK_FALSE(f.light.current.enabled);
}

RODAK_TEST("Info opens the detail page with identity, engine and raw fields; back closes it") {
    Fixture f;
    f.app.OnBackRequested();
    Pump();
    RODAK_CHECK_EQ(f.navigation.home_count, 1);

    Fixture g;
    lv_obj_t* header = lv_obj_get_child(g.app.root_, 0);
    lv_obj_t* info = lv_obj_get_child(header, -1);
    lv_obj_send_event(info, LV_EVENT_CLICKED, nullptr);
    Pump();
    RODAK_CHECK(Shows("助手详情"));
    RODAK_CHECK(Shows("罗达克"));
    RODAK_CHECK(Shows("host-wake · 就绪"));
    RODAK_CHECK(Shows("phase=idle wake=listening id_rev=1"));
    Screenshot("assistant-detail.ppm");
    RODAK_CHECK(g.app.OnBackRequested());
    Pump();
    RODAK_CHECK(Shows("语音助手"));
    RODAK_CHECK_FALSE(Shows("助手详情"));
    RODAK_CHECK_EQ(g.navigation.home_count, 0);
}

RODAK_TEST("A newer cloud recovery replaces a stale voice failure; revisions order ties") {
    Fixture f;
    f.assistant.state.phase = rodakos::VoiceAssistantPhase::kError;
    f.assistant.state.diagnostic = rodakos::CloudDiagnosticCode::kCredentialsRejected;
    f.assistant.state.diagnostic_revision = rodakos::NextCloudDiagnosticRevision();
    f.Diagnose(rodakos::CloudDiagnosticCode::kReady);
    RODAK_CHECK(Shows("待命中"));
    f.assistant.state.diagnostic = rodakos::CloudDiagnosticCode::kVoiceUnavailable;
    f.assistant.state.diagnostic_revision = rodakos::NextCloudDiagnosticRevision();
    f.app.RefreshState();
    RODAK_CHECK(Shows("语音服务不可用"));
    RODAK_CHECK(rodakos::IsNewerCloudDiagnostic(1, UINT32_MAX));
    RODAK_CHECK_FALSE(rodakos::IsNewerCloudDiagnostic(UINT32_MAX, 1));
}

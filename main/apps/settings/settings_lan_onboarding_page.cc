#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/lan_onboarding_service.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <esp_timer.h>

#include <string>

// 首次接入（无 USB）全屏页：发现服务器 → 显示 6 位码 → 结果。状态全部来自
// LanOnboardingService 的快照，界面只在 revision 变化时重绘。
namespace {

// The display is 320x240 landscape and the overlay does not scroll: every control must fit.
constexpr lv_coord_t kPad = 8;
constexpr lv_coord_t kButtonHeight = 36;
constexpr lv_coord_t kHalfButton = 148;
constexpr lv_coord_t kContentTop = 26;
constexpr lv_coord_t kServerRowHeight = 32;

lv_obj_t* CreateActionButton(lv_obj_t* parent, const char* text, bool primary, lv_align_t align,
                             lv_coord_t width = kHalfButton, lv_coord_t y = 0) {
    auto* button = rodakos_settings::CreateSettingButton(
        parent, text,
        primary ? rodakos_settings::SettingButtonStyle::kPrimary : rodakos_settings::SettingButtonStyle::kSecondary,
        width, kButtonHeight);
    lv_obj_align(button, align, 0, y);
    return button;
}

std::string ServerLabel(const rodakos::DiscoveredRodakServer& server) {
    const std::string name = server.display_name.empty() ? "Rodak" : server.display_name;
    return name + "  (" + server.address + ")";
}

}  // namespace

using namespace rodakos_settings;

void SettingsApp::ShowLanOnboarding() {
    auto* onboarding = context_ != nullptr ? context_->services().lan_onboarding() : nullptr;
    if (onboarding == nullptr || lan_onboarding_overlay_ != nullptr) return;
    lan_onboarding_overlay_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(lan_onboarding_overlay_);
    lv_obj_set_size(lan_onboarding_overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(lan_onboarding_overlay_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(lan_onboarding_overlay_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(lan_onboarding_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lan_onboarding_revision_ = UINT32_MAX;
    lan_onboarding_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* self = static_cast<SettingsApp*>(lv_timer_get_user_data(timer));
        if (self != nullptr) self->UpdateLanOnboarding();
    }, 500, this);
    onboarding->StartDiscovery();
    UpdateLanOnboarding();
}

void SettingsApp::CloseLanOnboarding() {
    auto* onboarding = context_ != nullptr ? context_->services().lan_onboarding() : nullptr;
    if (onboarding != nullptr) {
        const auto phase = onboarding->Snapshot().phase;
        if (phase != rodakos::LanOnboardingPhase::kCompleted) onboarding->Cancel();
    }
    if (lan_onboarding_timer_ != nullptr) {
        lv_timer_delete(lan_onboarding_timer_);
        lan_onboarding_timer_ = nullptr;
    }
    if (lan_onboarding_overlay_ != nullptr && lv_obj_is_valid(lan_onboarding_overlay_)) {
        lv_obj_delete(lan_onboarding_overlay_);
    }
    lan_onboarding_overlay_ = nullptr;
    lan_onboarding_body_ = nullptr;
    lan_onboarding_servers_.clear();
    // Completion already triggers the cloud refresh in main.cc; this only re-renders from NVS.
    if (current_page_ == SettingsPage::kDeviceCloud) UpdateDeviceCloudPage();
}

void SettingsApp::UpdateLanOnboarding() {
    auto* onboarding = context_ != nullptr ? context_->services().lan_onboarding() : nullptr;
    if (onboarding == nullptr || lan_onboarding_overlay_ == nullptr) return;
    const auto snapshot = onboarding->Snapshot();
    const int64_t now_ms = esp_timer_get_time() / 1000;
    // A lockout ends without a new snapshot; redraw once so 「重新开始」 appears on time.
    const bool lock_expired = lan_onboarding_locked_ && snapshot.locked_until_ms <= now_ms;
    if (snapshot.revision == lan_onboarding_revision_ && !lock_expired &&
        snapshot.phase != rodakos::LanOnboardingPhase::kShowCode) {
        return;
    }
    // 码页每 500 ms 刷新倒计时；其他状态只在快照变化时重建
    if (snapshot.revision == lan_onboarding_revision_ && lan_onboarding_countdown_ != nullptr) {
        const int64_t remaining = (snapshot.code_expires_at_ms - now_ms) / 1000;
        lv_label_set_text_fmt(lan_onboarding_countdown_, "%d:%02d 后失效",
                              static_cast<int>(remaining > 0 ? remaining / 60 : 0),
                              static_cast<int>(remaining > 0 ? remaining % 60 : 0));
        return;
    }
    lan_onboarding_revision_ = snapshot.revision;
    lan_onboarding_locked_ = snapshot.phase == rodakos::LanOnboardingPhase::kFailed &&
                             snapshot.locked_until_ms > now_ms;
    lan_onboarding_countdown_ = nullptr;
    if (lan_onboarding_body_ != nullptr) lv_obj_delete(lan_onboarding_body_);
    lan_onboarding_body_ = lv_obj_create(lan_onboarding_overlay_);
    lv_obj_remove_style_all(lan_onboarding_body_);
    lv_obj_set_size(lan_onboarding_body_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(lan_onboarding_body_, kPad, 0);
    lv_obj_clear_flag(lan_onboarding_body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(lan_onboarding_body_);
    const lv_coord_t content_width = lv_obj_get_content_width(lan_onboarding_body_);

    auto* title = CreateSettingLabel(lan_onboarding_body_, "局域网连接");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    auto* message = CreateSettingLabel(lan_onboarding_body_, "", true);
    lv_obj_set_width(message, content_width);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_align(message, LV_ALIGN_TOP_LEFT, 0, kContentTop);

    using Phase = rodakos::LanOnboardingPhase;
    lv_obj_t* primary = nullptr;
    const char* close_text = "取消";
    switch (snapshot.phase) {
        case Phase::kIdle:
        case Phase::kDiscovering:
            lv_label_set_text(message, "正在局域网中查找 Rodak…");
            break;
        case Phase::kChooseServer: {
            lan_onboarding_servers_ = snapshot.servers;
            if (lan_onboarding_servers_.empty()) {
                lv_label_set_text(message, snapshot.message.c_str());
                close_text = "返回";
            } else {
                lv_label_set_text(message, "选择要连接的电脑：");
            }
            // Every discovered server is reachable: the list scrolls between the prompt and the
            // fixed bottom buttons instead of dropping rows past the fourth.
            const lv_coord_t list_top = kContentTop + 22;
            const lv_coord_t list_height =
                lv_obj_get_content_height(lan_onboarding_body_) - list_top - kButtonHeight - 6;
            auto* list = lv_obj_create(lan_onboarding_body_);
            lv_obj_remove_style_all(list);
            lv_obj_set_size(list, content_width, list_height);
            lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, list_top);
            lv_obj_set_scroll_dir(list, LV_DIR_VER);
            lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
            lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
            lv_coord_t y = 0;
            for (size_t index = 0; index < lan_onboarding_servers_.size(); ++index) {
                auto* button = CreateActionButton(list,
                                                  ServerLabel(lan_onboarding_servers_[index]).c_str(),
                                                  index == 0, LV_ALIGN_TOP_LEFT, content_width, y);
                lv_obj_set_height(button, kServerRowHeight);
                lv_obj_set_user_data(button, reinterpret_cast<void*>(index));
                lv_obj_add_event_cb(button, [](lv_event_t* e) {
                    auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
                    const auto index = reinterpret_cast<size_t>(
                        lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
                    auto* service = self->context_->services().lan_onboarding();
                    if (service != nullptr && index < self->lan_onboarding_servers_.size()) {
                        service->Connect(self->lan_onboarding_servers_[index]);
                    }
                }, LV_EVENT_CLICKED, this);
                y += kServerRowHeight + 4;
            }
            primary = CreateActionButton(lan_onboarding_body_, "重新查找", lan_onboarding_servers_.empty(),
                                         LV_ALIGN_BOTTOM_LEFT);
            lv_obj_add_event_cb(primary, [](lv_event_t* e) {
                auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
                auto* service = self->context_->services().lan_onboarding();
                if (service != nullptr) service->StartDiscovery();
            }, LV_EVENT_CLICKED, this);
            break;
        }
        case Phase::kRequesting:
            lv_label_set_text(message, "正在连接 Rodak…");
            break;
        case Phase::kShowCode:
        case Phase::kVerifying: {
            lv_label_set_text(message, snapshot.phase == Phase::kShowCode
                                           ? "在电脑上的 Rodak 中输入下面的配对码"
                                           : "正在校验配对码…");
            const std::string grouped = snapshot.code.size() == 6
                ? snapshot.code.substr(0, 3) + " " + snapshot.code.substr(3)
                : snapshot.code;
            auto* code = lv_label_create(lan_onboarding_body_);
            lv_label_set_text(code, grouped.c_str());
            lv_obj_set_style_text_font(code, &lv_font_montserrat_26, 0);
            lv_obj_set_style_text_color(code, rodakos_theme_text_primary(), 0);
            lv_obj_set_style_text_letter_space(code, 4, 0);
            lv_obj_align(code, LV_ALIGN_TOP_MID, 0, kContentTop + 30);
            std::string detail;
            if (!snapshot.server_name.empty()) detail = "目标：" + snapshot.server_name;
            auto* server = CreateSettingLabel(lan_onboarding_body_, detail.c_str(), true);
            lv_obj_set_width(server, content_width);
            lv_label_set_long_mode(server, LV_LABEL_LONG_DOT);
            lv_obj_align(server, LV_ALIGN_TOP_LEFT, 0, kContentTop + 72);
            lan_onboarding_countdown_ = CreateSettingLabel(lan_onboarding_body_, "", true);
            lv_obj_align(lan_onboarding_countdown_, LV_ALIGN_TOP_LEFT, 0, kContentTop + 92);
            const int64_t remaining = (snapshot.code_expires_at_ms - now_ms) / 1000;
            lv_label_set_text_fmt(lan_onboarding_countdown_, "%d:%02d 后失效",
                                  static_cast<int>(remaining > 0 ? remaining / 60 : 0),
                                  static_cast<int>(remaining > 0 ? remaining % 60 : 0));
            break;
        }
        case Phase::kCompleted:
            lv_label_set_text(message, "已安全连接到 Rodak。\n正在获取设备凭据…");
            close_text = "完成";
            break;
        case Phase::kFailed: {
            const std::string text = snapshot.locked_until_ms > now_ms
                ? snapshot.message + "\n多次失败，请 1 分钟后再试。"
                : snapshot.message;
            lv_label_set_text(message, text.c_str());
            close_text = "返回";
            if (snapshot.locked_until_ms <= now_ms) {
                primary = CreateActionButton(lan_onboarding_body_, "重新开始", true, LV_ALIGN_BOTTOM_LEFT);
                lv_obj_add_event_cb(primary, [](lv_event_t* e) {
                    auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
                    auto* service = self->context_->services().lan_onboarding();
                    if (service != nullptr) service->StartDiscovery();
                }, LV_EVENT_CLICKED, this);
            }
            break;
        }
    }

    auto* close = CreateActionButton(lan_onboarding_body_, close_text, false,
                                     primary != nullptr ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_MID);
    lv_obj_add_event_cb(close, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        // 关闭会删除本按钮所在的界面，放到下一轮事件循环执行
        lv_async_call([](void* user_data) {
            static_cast<SettingsApp*>(user_data)->CloseLanOnboarding();
        }, self);
    }, LV_EVENT_CLICKED, this);
}

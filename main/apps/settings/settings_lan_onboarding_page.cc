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

lv_obj_t* CreateActionButton(lv_obj_t* parent, const char* text, bool primary, lv_coord_t y) {
    auto* button = lv_btn_create(parent);
    lv_obj_set_size(button, 264, 36);
    lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(button, primary ? rodakos_theme_primary() : rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(button, 7, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, primary ? lv_color_white() : rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(label, &phone_font_14, 0);
    lv_obj_center(label);
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
    if (current_page_ == SettingsPage::kDeviceCloud) RefreshDeviceCloud();
}

void SettingsApp::UpdateLanOnboarding() {
    auto* onboarding = context_ != nullptr ? context_->services().lan_onboarding() : nullptr;
    if (onboarding == nullptr || lan_onboarding_overlay_ == nullptr) return;
    const auto snapshot = onboarding->Snapshot();
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (snapshot.revision == lan_onboarding_revision_ &&
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
    lan_onboarding_countdown_ = nullptr;
    if (lan_onboarding_body_ != nullptr) lv_obj_delete(lan_onboarding_body_);
    lan_onboarding_body_ = lv_obj_create(lan_onboarding_overlay_);
    lv_obj_remove_style_all(lan_onboarding_body_);
    lv_obj_set_size(lan_onboarding_body_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(lan_onboarding_body_, 12, 0);
    lv_obj_clear_flag(lan_onboarding_body_, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = CreateSettingLabel(lan_onboarding_body_, "连接到电脑上的 Rodak");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    auto* message = CreateSettingLabel(lan_onboarding_body_, "", true);
    lv_obj_set_width(message, 272);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 40);

    lv_coord_t button_y = 330;
    using Phase = rodakos::LanOnboardingPhase;
    switch (snapshot.phase) {
        case Phase::kIdle:
        case Phase::kDiscovering:
            lv_label_set_text(message, "正在局域网中查找 Rodak…");
            break;
        case Phase::kChooseServer: {
            lv_label_set_text(message, snapshot.servers.empty()
                                           ? snapshot.message.c_str()
                                           : "选择要连接的电脑：");
            lan_onboarding_servers_ = snapshot.servers;
            lv_coord_t y = 96;
            for (size_t index = 0; index < lan_onboarding_servers_.size() && index < 4; ++index) {
                auto* button = CreateActionButton(lan_onboarding_body_,
                                                  ServerLabel(lan_onboarding_servers_[index]).c_str(),
                                                  index == 0, y);
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
                y += 44;
            }
            auto* rescan = CreateActionButton(lan_onboarding_body_, "重新查找", false, 282);
            lv_obj_add_event_cb(rescan, [](lv_event_t* e) {
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
            lv_obj_set_style_transform_scale(code, 512, 0);
            lv_obj_set_style_transform_pivot_x(code, LV_PCT(50), 0);
            lv_obj_set_style_transform_pivot_y(code, LV_PCT(50), 0);
            lv_obj_align(code, LV_ALIGN_TOP_MID, 0, 148);
            if (!snapshot.server_name.empty()) {
                auto* server = CreateSettingLabel(lan_onboarding_body_,
                                                  ("目标：" + snapshot.server_name).c_str(), true);
                lv_obj_align(server, LV_ALIGN_TOP_MID, 0, 222);
            }
            lan_onboarding_countdown_ = CreateSettingLabel(lan_onboarding_body_, "", true);
            lv_obj_align(lan_onboarding_countdown_, LV_ALIGN_TOP_MID, 0, 250);
            const int64_t remaining = (snapshot.code_expires_at_ms - now_ms) / 1000;
            lv_label_set_text_fmt(lan_onboarding_countdown_, "%d:%02d 后失效",
                                  static_cast<int>(remaining > 0 ? remaining / 60 : 0),
                                  static_cast<int>(remaining > 0 ? remaining % 60 : 0));
            break;
        }
        case Phase::kCompleted:
            lv_label_set_text(message, "已安全连接到 Rodak。\n正在获取设备凭据…");
            break;
        case Phase::kFailed: {
            const std::string text = snapshot.locked_until_ms > now_ms
                ? snapshot.message + "\n多次失败，请 1 分钟后再试。"
                : snapshot.message;
            lv_label_set_text(message, text.c_str());
            if (snapshot.locked_until_ms <= now_ms) {
                auto* retry = CreateActionButton(lan_onboarding_body_, "重新开始", true, 282);
                lv_obj_add_event_cb(retry, [](lv_event_t* e) {
                    auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
                    auto* service = self->context_->services().lan_onboarding();
                    if (service != nullptr) service->StartDiscovery();
                }, LV_EVENT_CLICKED, this);
            }
            break;
        }
    }

    auto* close = CreateActionButton(lan_onboarding_body_,
                                     snapshot.phase == Phase::kCompleted ? "完成" : "取消", false,
                                     button_y);
    lv_obj_add_event_cb(close, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        // 关闭会删除本按钮所在的界面，放到下一轮事件循环执行
        lv_async_call([](void* user_data) {
            static_cast<SettingsApp*>(user_data)->CloseLanOnboarding();
        }, self);
    }, LV_EVENT_CLICKED, this);
}

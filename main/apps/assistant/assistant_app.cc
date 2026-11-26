#include "apps/assistant/assistant_app.h"

#include "phone_os/device_cloud_config.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/voice_assistant_service.h"
#include "phone_os/voice_status_light_service.h"
#include "phone_os/voice_wake_service.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "rodakos_adapters/wifi_adapter.h"

#include <esp_log.h>

#include <cstdio>
#include <memory>

namespace {
constexpr const char* TAG = "AssistantApp";
constexpr uint32_t kRefreshMs = 500;
constexpr lv_coord_t kRowHeight = 38;

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) context->navigation().ReturnHome();
}

void RefreshTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<AssistantApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) self->RefreshState();
}

lv_obj_t* CreatePlain(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* CreateText(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t* CreateDot(lv_obj_t* parent) {
    auto* dot = CreatePlain(parent);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    return dot;
}

lv_obj_t* CreateSwitch(lv_obj_t* parent) {
    auto* sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 44, 24);
    lv_obj_set_style_bg_color(sw, rodakos_theme_bg_tertiary(), LV_PART_MAIN);
    const auto checked = static_cast<lv_style_selector_t>(
        static_cast<uint32_t>(LV_PART_INDICATOR) | static_cast<uint32_t>(LV_STATE_CHECKED));
    lv_obj_set_style_bg_color(sw, rodakos_theme_primary(), checked);
    lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_KNOB);
    // The row toggles; the switch only mirrors the state.
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -12, 0);
    return sw;
}

lv_obj_t* CreateRow(lv_obj_t* list, int index, const char* icon, const char* title) {
    auto* row = CreatePlain(list);
    lv_obj_set_size(row, 300, kRowHeight);
    lv_obj_set_pos(row, 0, index * kRowHeight);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(row, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    if (index > 0) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(row, rodakos_theme_bg_tertiary(), 0);
        lv_obj_set_style_border_width(row, 1, 0);
    }
    auto* glyph = CreateText(row, icon, PhoneIconFont(), rodakos_theme_primary());
    lv_obj_align(glyph, LV_ALIGN_LEFT_MID, 12, 0);
    auto* label = CreateText(row, title, &phone_font_14, rodakos_theme_text_primary());
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 40, 0);
    return row;
}

void SetChecked(lv_obj_t* obj, bool checked) {
    if (checked) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(obj, LV_STATE_CHECKED);
    }
}

void SetHidden(lv_obj_t* obj, bool hidden) {
    if (obj == nullptr) return;
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// Device text for a turn step without a server label (contract section 6.1); same as Home.
const char* TurnStageText(rodakos::VoiceTurnStage stage) {
    switch (stage) {
        case rodakos::VoiceTurnStage::kThinking: return "正在思考";
        case rodakos::VoiceTurnStage::kTool: return "正在处理";
        case rodakos::VoiceTurnStage::kComposing: return "正在组织回答";
        case rodakos::VoiceTurnStage::kNone:
        default: return "已收到";
    }
}

const char* PhaseText(rodakos::VoiceAssistantPhase phase) {
    switch (phase) {
        case rodakos::VoiceAssistantPhase::kConnecting: return "connecting";
        case rodakos::VoiceAssistantPhase::kListening: return "listening";
        case rodakos::VoiceAssistantPhase::kSpeaking: return "speaking";
        case rodakos::VoiceAssistantPhase::kError: return "error";
        case rodakos::VoiceAssistantPhase::kIdle:
        default: return "idle";
    }
}

const char* WakeStatusText(rodakos::VoiceWakeStatus status) {
    switch (status) {
        case rodakos::VoiceWakeStatus::kListening: return "listening";
        case rodakos::VoiceWakeStatus::kAssistantActive: return "active";
        case rodakos::VoiceWakeStatus::kUnavailable: return "unavailable";
        case rodakos::VoiceWakeStatus::kError: return "error";
        case rodakos::VoiceWakeStatus::kDisabled:
        default: return "disabled";
    }
}

}  // namespace

bool AssistantApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    assistant_ = context.services().voice_assistant();
    wake_ = context.services().voice_wake();
    cloud_ = context.services().device_cloud();
    status_light_ = context.services().voice_status_light();

    if (assistant_ != nullptr) assistant_->Init();
    if (wake_ != nullptr) wake_->Init();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return false;

    CreateUi();
    RefreshState();
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, kRefreshMs, this);
    ESP_LOGI(TAG, "Assistant app created");
    return true;
}

void AssistantApp::OnResume() {
    if (ui_ == nullptr) return;
    PhoneUiLock lock(*ui_);
    if (!lock.locked() || root_ == nullptr || !lv_obj_is_valid(root_)) return;
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
    if (refresh_timer_ != nullptr) lv_timer_resume(refresh_timer_);
    drawn_key_.clear();
    RefreshState();
}

void AssistantApp::OnPause() {
    if (ui_ == nullptr) return;
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return;
    if (refresh_timer_ != nullptr) lv_timer_pause(refresh_timer_);
    if (root_ != nullptr && lv_obj_is_valid(root_)) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void AssistantApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            lv_async_call_cancel(DeferOpenSettings, this);
            if (context_ != nullptr) lv_async_call_cancel(DeferReturnHome, context_);
            if (refresh_timer_ != nullptr) {
                lv_timer_delete(refresh_timer_);
                refresh_timer_ = nullptr;
            }
            if (root_ != nullptr && lv_obj_is_valid(root_)) lv_obj_delete(root_);
        }
    }
    root_ = title_label_ = badge_ = badge_icon_ = status_dot_ = status_label_ = subtitle_label_ = nullptr;
    fix_button_ = fix_label_ = nullptr;
    wake_row_ = wake_switch_ = wake_note_label_ = nullptr;
    light_row_ = light_switch_ = light_note_label_ = nullptr;
    cloud_row_ = cloud_dot_ = cloud_label_ = nullptr;
    detail_ = detail_raw_label_ = nullptr;
    for (auto*& value : detail_values_) value = nullptr;
    drawn_key_.clear();
    fix_ = Fix::kNone;
    context_ = nullptr;
    ui_ = nullptr;
    assistant_ = nullptr;
    wake_ = nullptr;
    cloud_ = nullptr;
    status_light_ = nullptr;
}

bool AssistantApp::OnBackRequested() {
    if (detail_ != nullptr) {
        CloseDetail();
        return true;
    }
    NavigateHome();
    return true;
}

void AssistantApp::CreateUi() {
    root_ = CreatePlain(ui_->screen());
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);

    CreateAppHeader(root_, "语音助手", [](lv_event_t* e) {
        static_cast<AssistantApp*>(lv_event_get_user_data(e))->OnBackRequested();
    }, [](lv_event_t* e) {
        static_cast<AssistantApp*>(lv_event_get_user_data(e))->CreateDetail();
    }, this, &title_label_, ICON_INFO);

    // Status card: 300x68 at y=46.
    auto* hero = CreatePlain(root_);
    lv_obj_set_size(hero, 300, 68);
    lv_obj_set_pos(hero, 10, 46);
    lv_obj_set_style_bg_color(hero, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(hero, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hero, 10, 0);

    badge_ = CreatePlain(hero);
    lv_obj_set_size(badge_, 44, 44);
    lv_obj_set_pos(badge_, 12, 12);
    lv_obj_set_style_radius(badge_, 12, 0);
    lv_obj_set_style_bg_opa(badge_, LV_OPA_COVER, 0);
    badge_icon_ = CreateText(badge_, ICON_ASSISTANT, PhoneIconFont(), rodakos_theme_primary());
    lv_obj_center(badge_icon_);

    status_dot_ = CreateDot(hero);
    lv_obj_set_pos(status_dot_, 68, 18);
    status_label_ = CreateText(hero, "", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_size(status_label_, 212, 20);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(status_label_, 82, 11);
    subtitle_label_ = CreateText(hero, "", &phone_font_14, rodakos_theme_text_secondary());
    lv_obj_set_size(subtitle_label_, 220, 20);
    lv_label_set_long_mode(subtitle_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(subtitle_label_, 68, 37);

    fix_button_ = lv_btn_create(hero);
    lv_obj_remove_style_all(fix_button_);
    lv_obj_set_size(fix_button_, 60, 34);
    lv_obj_align(fix_button_, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_radius(fix_button_, 8, 0);
    lv_obj_set_style_bg_color(fix_button_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(fix_button_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fix_button_, rodakos_theme_primary(), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(fix_button_, 4);
    fix_label_ = CreateText(fix_button_, "", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_center(fix_label_);
    lv_obj_add_event_cb(fix_button_, [](lv_event_t* e) {
        static_cast<AssistantApp*>(lv_event_get_user_data(e))->OpenFix();
    }, LV_EVENT_CLICKED, this);

    // Settings list: three 38 px rows at y=120.
    auto* list = CreatePlain(root_);
    lv_obj_set_size(list, 300, kRowHeight * 3);
    lv_obj_set_pos(list, 10, 120);
    lv_obj_set_style_bg_color(list, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list, 8, 0);
    lv_obj_set_style_clip_corner(list, true, 0);

    wake_row_ = CreateRow(list, 0, ICON_MICROPHONE, "语音唤醒");
    wake_switch_ = CreateSwitch(wake_row_);
    wake_note_label_ = CreateText(wake_row_, "", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_align(wake_note_label_, LV_ALIGN_RIGHT_MID, -64, 0);
    lv_obj_add_event_cb(wake_row_, [](lv_event_t* e) {
        static_cast<AssistantApp*>(lv_event_get_user_data(e))->ToggleWakeListening();
    }, LV_EVENT_CLICKED, this);

    light_row_ = CreateRow(list, 1, ICON_LIGHT, "语音状态灯");
    light_switch_ = CreateSwitch(light_row_);
    light_note_label_ = CreateText(light_row_, "", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_align(light_note_label_, LV_ALIGN_RIGHT_MID, -64, 0);
    lv_obj_add_event_cb(light_row_, [](lv_event_t* e) {
        static_cast<AssistantApp*>(lv_event_get_user_data(e))->ToggleStatusLight();
    }, LV_EVENT_CLICKED, this);

    cloud_row_ = CreateRow(list, 2, ICON_CLOUD, "连接服务");
    auto* chevron = CreateText(cloud_row_, ICON_CHEVRON_RIGHT, PhoneIconFont(), rodakos_theme_text_tertiary());
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -8, 0);
    cloud_label_ = CreateText(cloud_row_, "", &phone_font_14, rodakos_theme_text_secondary());
    lv_obj_align(cloud_label_, LV_ALIGN_RIGHT_MID, -30, 0);
    cloud_dot_ = CreateDot(cloud_row_);
    lv_obj_add_event_cb(cloud_row_, [](lv_event_t* e) {
        static_cast<AssistantApp*>(lv_event_get_user_data(e))->OpenSettingsPage("cloud");
    }, LV_EVENT_CLICKED, this);
}

AssistantApp::View AssistantApp::ComputeView() const {
    View view;
    const auto assistant_state = assistant_ != nullptr ? assistant_->GetState() : rodakos::VoiceAssistantState{};
    const auto cloud_state = cloud_ != nullptr ? cloud_->diagnostic_state() : rodakos::CloudDiagnosticState{};
    const auto wake_state = wake_ != nullptr ? wake_->GetState() : rodakos::VoiceWakeState{};
    auto* wifi = context_ != nullptr ? context_->services().wifi() : nullptr;
    const bool wifi_down = wifi != nullptr && wifi->GetStatus() != WiFiStatus::kConnected;

    // Diagnosis priority is unchanged from the English page: the newer of the cloud and the
    // voice diagnosis wins, and a lost Wi-Fi link overrides everything except setup and trust.
    auto diagnostic = cloud_state.code;
    if (assistant_ == nullptr) diagnostic = rodakos::CloudDiagnosticCode::kVoiceUnavailable;
    if (assistant_state.phase == rodakos::VoiceAssistantPhase::kError &&
        rodakos::IsNewerCloudDiagnostic(assistant_state.diagnostic_revision, cloud_state.revision)) {
        diagnostic = assistant_state.diagnostic;
    }
    if (wifi_down && diagnostic != rodakos::CloudDiagnosticCode::kUnconfigured &&
        diagnostic != rodakos::CloudDiagnosticCode::kTrustUnavailable) {
        diagnostic = rodakos::CloudDiagnosticCode::kNetworkUnavailable;
    }

    switch (diagnostic) {
        case rodakos::CloudDiagnosticCode::kReady:
            view.cloud_text = "已连接";
            view.cloud_tone = Tone::kOk;
            break;
        case rodakos::CloudDiagnosticCode::kUnconfigured:
            view.cloud_text = "未连接";
            view.cloud_tone = Tone::kError;
            break;
        case rodakos::CloudDiagnosticCode::kRefreshing:
            view.cloud_text = "正在更新";
            view.cloud_tone = Tone::kIdle;
            break;
        default:
            view.cloud_text = "需要处理";
            view.cloud_tone = Tone::kWarn;
            break;
    }

    const auto phase = assistant_state.phase;
    const bool active = phase == rodakos::VoiceAssistantPhase::kConnecting ||
                        phase == rodakos::VoiceAssistantPhase::kListening ||
                        phase == rodakos::VoiceAssistantPhase::kSpeaking;
    std::string wake_word = wake_state.voice_identity.wake_word;
    if (wake_word.empty()) wake_word = "唤醒词";

    if (active) {
        view.tone = Tone::kActive;
        view.badge_filled = true;
        view.icon = ICON_ASSISTANT;
        view.wake_note = "对话中";
        if (phase == rodakos::VoiceAssistantPhase::kConnecting) {
            view.icon = ICON_CLOUD;
            view.title = "正在连接";
            view.subtitle = "准备语音会话";
        } else if (phase == rodakos::VoiceAssistantPhase::kSpeaking) {
            view.icon = ICON_VOLUME;
            view.tone = Tone::kOk;
            view.title = assistant_state.announce ? "正在播报" : "正在回复";
            view.subtitle = "说话即可打断";
        } else if (assistant_state.awaiting_reply) {
            // Same text as the Home status line: the server label replaces the stage word.
            view.title = assistant_state.turn_label.empty() ? TurnStageText(assistant_state.turn_stage)
                                                            : assistant_state.turn_label;
            std::string name = wake_state.voice_identity.name;
            view.subtitle = "等待" + (name.empty() ? std::string("助手") : name) + "回复…";
        } else {
            view.icon = ICON_MICROPHONE;
            view.title = "正在聆听";
            view.subtitle = "请说话，随时可打断";
        }
        return view;
    }

    if (diagnostic != rodakos::CloudDiagnosticCode::kReady) {
        view.icon = ICON_CLOUD;
        view.tone = diagnostic == rodakos::CloudDiagnosticCode::kUnconfigured ? Tone::kError
                  : diagnostic == rodakos::CloudDiagnosticCode::kRefreshing ? Tone::kIdle
                                                                             : Tone::kWarn;
        view.title = rodakos::CloudDiagnosticTitleZh(diagnostic);
        switch (diagnostic) {
            case rodakos::CloudDiagnosticCode::kNetworkUnavailable:
                if (wifi_down) {
                    view.icon = ICON_WIFI;
                    view.title = "未连接 Wi-Fi";
                    view.subtitle = "连上后才能对话";
                    view.fix = Fix::kWiFi;
                } else {
                    view.subtitle = "服务器无响应";
                    view.fix = Fix::kCloud;
                }
                break;
            case rodakos::CloudDiagnosticCode::kUnconfigured:
                view.subtitle = "完成配对后才能对话";
                view.fix = Fix::kCloud;
                break;
            case rodakos::CloudDiagnosticCode::kRefreshing:
                view.subtitle = "稍候即可对话";
                break;
            case rodakos::CloudDiagnosticCode::kCancelled:
                view.subtitle = "准备好后再说唤醒词";
                break;
            case rodakos::CloudDiagnosticCode::kDeviceMemoryLow:
                view.icon = ICON_CHIP;
                view.subtitle = "关闭其他应用或重启后重试";
                break;
            default:
                view.subtitle = "在连接服务中处理";
                view.fix = Fix::kCloud;
                break;
        }
        return view;
    }

    if (wake_ == nullptr || wake_state.status == rodakos::VoiceWakeStatus::kUnavailable ||
        wake_state.status == rodakos::VoiceWakeStatus::kError) {
        view.icon = ICON_MICROPHONE;
        view.tone = Tone::kWarn;
        view.title = "唤醒不可用";
        view.subtitle = wake_state.runtime_available ? "唤醒服务出错，请重启设备" : "唤醒模型未安装";
        return view;
    }
    if (!wake_state.enabled) {
        view.icon = ICON_MICROPHONE;
        view.tone = Tone::kIdle;
        view.title = "唤醒已关闭";
        view.subtitle = "打开下方开关即可语音唤醒";
        return view;
    }
    if (wake_state.status == rodakos::VoiceWakeStatus::kDisabled) {
        // Enabled but not listening: the wake runtime yields the shared I2S while music plays.
        view.icon = ICON_MICROPHONE;
        view.tone = Tone::kIdle;
        view.title = "唤醒已暂停";
        view.subtitle = "音乐播放结束后恢复";
        view.wake_note = "已暂停";
        return view;
    }
    view.icon = ICON_ASSISTANT;
    view.tone = Tone::kOk;
    view.title = "待命中";
    view.subtitle = "说「" + wake_word + "」开始对话";
    return view;
}

void AssistantApp::ApplyView(const View& view) {
    auto tone_color = [](Tone tone) {
        switch (tone) {
            case Tone::kOk: return rodakos_theme_success();
            case Tone::kActive: return rodakos_theme_primary();
            case Tone::kWarn: return rodakos_theme_warning();
            case Tone::kError: return rodakos_theme_error();
            case Tone::kIdle:
            default: return rodakos_theme_text_tertiary();
        }
    };
    const lv_color_t accent = tone_color(view.tone);

    lv_label_set_text(badge_icon_, view.icon);
    if (view.badge_filled) {
        lv_obj_set_style_bg_color(badge_, rodakos_theme_primary(), 0);
        lv_obj_set_style_text_color(badge_icon_, rodakos_theme_on_primary(), 0);
    } else if (view.tone == Tone::kIdle) {
        lv_obj_set_style_bg_color(badge_, rodakos_theme_bg_tertiary(), 0);
        lv_obj_set_style_text_color(badge_icon_, rodakos_theme_text_tertiary(), 0);
    } else {
        const lv_color_t icon_color = view.tone == Tone::kOk ? rodakos_theme_primary() : accent;
        lv_obj_set_style_bg_color(badge_, lv_color_mix(icon_color, rodakos_theme_bg_tertiary(), LV_OPA_20), 0);
        lv_obj_set_style_text_color(badge_icon_, icon_color, 0);
    }

    lv_obj_set_style_bg_color(status_dot_, accent, 0);
    lv_obj_set_style_bg_opa(status_dot_, view.tone == Tone::kIdle ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(status_dot_, accent, 0);
    lv_obj_set_style_border_width(status_dot_, view.tone == Tone::kIdle ? 1 : 0, 0);
    lv_label_set_text(status_label_, view.title.c_str());
    lv_obj_set_style_text_color(status_label_,
        view.tone == Tone::kOk || view.tone == Tone::kIdle ? rodakos_theme_text_primary() : accent, 0);
    lv_label_set_text(subtitle_label_, view.subtitle.c_str());

    fix_ = view.fix;
    const bool has_fix = view.fix != Fix::kNone;
    SetHidden(fix_button_, !has_fix);
    lv_label_set_text(fix_label_, view.fix == Fix::kWiFi ? "去连接" : "去处理");
    // With the 60 px fix button the text column ends at x=228 inside the card.
    lv_obj_set_width(status_label_, has_fix ? 146 : 212);
    lv_obj_set_width(subtitle_label_, has_fix ? 160 : 220);

    lv_label_set_text(wake_note_label_, view.wake_note.c_str());
    lv_obj_set_style_text_color(cloud_label_, tone_color(view.cloud_tone), 0);
    lv_label_set_text(cloud_label_, view.cloud_text);
    lv_obj_update_layout(cloud_label_);
    lv_obj_align_to(cloud_dot_, cloud_label_, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    lv_obj_set_style_bg_color(cloud_dot_, tone_color(view.cloud_tone), 0);
    lv_obj_set_style_bg_opa(cloud_dot_, LV_OPA_COVER, 0);
}

void AssistantApp::RefreshState() {
    if (root_ == nullptr) return;
    const View view = ComputeView();
    const auto wake_state = wake_ != nullptr ? wake_->GetState() : rodakos::VoiceWakeState{};
    SetChecked(wake_switch_, wake_state.enabled);
    if (status_light_ != nullptr) {
        const auto light = status_light_->settings();
        SetChecked(light_switch_, light.enabled);
        lv_label_set_text_fmt(light_note_label_, light.enabled ? "%u%%" : "已关闭",
                              static_cast<unsigned>(light.max_brightness_percent));
        SetHidden(light_row_, false);
    } else {
        lv_label_set_text(light_note_label_, "不可用");
        lv_obj_add_state(light_row_, LV_STATE_DISABLED);
    }

    // Only touch labels when what they show changes; the timer fires every 500 ms.
    std::string key = view.title + '\x1f' + view.subtitle + '\x1f' + view.wake_note + '\x1f' +
                      view.cloud_text + '\x1f' + std::to_string(static_cast<int>(view.tone)) +
                      std::to_string(static_cast<int>(view.fix)) + (view.icon != nullptr ? view.icon : "");
    if (key != drawn_key_) {
        drawn_key_ = std::move(key);
        ApplyView(view);
    }
    if (detail_ != nullptr) RefreshDetail();
}

void AssistantApp::ToggleWakeListening() {
    if (wake_ == nullptr) {
        ui_->ShowToastUnlocked("唤醒服务不可用");
        return;
    }
    const bool next = !wake_->GetState().enabled;
    if (!wake_->SetEnabled(next)) {
        ui_->ShowToastUnlocked("唤醒设置失败");
    } else {
        ui_->ShowToastUnlocked(next ? "已开启语音唤醒" : "已关闭语音唤醒");
    }
    drawn_key_.clear();
    RefreshState();
}

void AssistantApp::ToggleStatusLight() {
    if (status_light_ == nullptr) return;
    auto next = status_light_->settings();
    next.enabled = !next.enabled;
    if (!status_light_->SetSettings(next)) ui_->ShowToastUnlocked("保存失败");
    RefreshState();
}

void AssistantApp::OpenFix() {
    if (fix_ == Fix::kWiFi) {
        OpenSettingsPage("wifi");
    } else if (fix_ == Fix::kCloud) {
        OpenSettingsPage("cloud");
    }
}

void AssistantApp::OpenSettingsPage(const char* page) {
    if (context_ == nullptr) return;
    lv_async_call_cancel(DeferOpenSettings, this);
    pending_page_ = page;
    lv_async_call(DeferOpenSettings, this);
}

void AssistantApp::DeferOpenSettings(void* user_data) {
    auto* self = static_cast<AssistantApp*>(user_data);
    if (self == nullptr || self->context_ == nullptr || self->pending_page_ == nullptr) return;
    const char* page = self->pending_page_;
    self->pending_page_ = nullptr;
    self->context_->navigation().LaunchFrom("settings", "assistant", page);
}

void AssistantApp::NavigateHome() {
    if (context_ == nullptr) return;
    lv_async_call_cancel(DeferReturnHome, context_);
    lv_async_call(DeferReturnHome, context_);
}

void AssistantApp::CreateDetail() {
    if (detail_ != nullptr) return;
    detail_ = CreatePlain(root_);
    lv_obj_set_size(detail_, 320, 200);
    lv_obj_set_pos(detail_, 0, kRodakosAppHeaderHeight);
    lv_obj_set_style_bg_color(detail_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(detail_, LV_OPA_COVER, 0);
    lv_obj_add_flag(detail_, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(title_label_, "助手详情");

    auto* table = CreatePlain(detail_);
    lv_obj_set_size(table, 300, 140);
    lv_obj_set_pos(table, 10, 8);
    lv_obj_set_style_bg_color(table, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(table, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(table, 8, 0);
    const char* keys[] = {"助手名称", "唤醒词", "识别引擎", "状态灯亮度", "云端诊断"};
    for (int i = 0; i < 5; ++i) {
        auto* key = CreateText(table, keys[i], &phone_font_14, rodakos_theme_text_secondary());
        lv_obj_set_pos(key, 12, 4 + i * 28);
        detail_values_[i] = CreateText(table, "", &phone_font_14, rodakos_theme_text_primary());
        lv_obj_set_width(detail_values_[i], 180);
        lv_label_set_long_mode(detail_values_[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(detail_values_[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(detail_values_[i], LV_ALIGN_TOP_RIGHT, -12, 4 + i * 28);
        if (i > 0) {
            auto* divider = CreatePlain(table);
            lv_obj_set_size(divider, 300, 1);
            lv_obj_set_pos(divider, 0, i * 28);
            lv_obj_set_style_bg_color(divider, rodakos_theme_bg_tertiary(), 0);
            lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
        }
    }
    detail_raw_label_ = CreateText(detail_, "", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_width(detail_raw_label_, 292);
    lv_label_set_long_mode(detail_raw_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(detail_raw_label_, 14, 158);
    RefreshDetail();
}

void AssistantApp::CloseDetail() {
    if (detail_ != nullptr && lv_obj_is_valid(detail_)) lv_obj_delete(detail_);
    detail_ = detail_raw_label_ = nullptr;
    for (auto*& value : detail_values_) value = nullptr;
    lv_label_set_text(title_label_, "语音助手");
}

void AssistantApp::RefreshDetail() {
    const auto wake_state = wake_ != nullptr ? wake_->GetState() : rodakos::VoiceWakeState{};
    const auto assistant_state = assistant_ != nullptr ? assistant_->GetState() : rodakos::VoiceAssistantState{};
    const auto cloud_state = cloud_ != nullptr ? cloud_->diagnostic_state() : rodakos::CloudDiagnosticState{};
    const auto& identity = wake_state.voice_identity;
    lv_label_set_text(detail_values_[0], identity.name.empty() ? "—" : identity.name.c_str());
    lv_label_set_text(detail_values_[1], identity.wake_word.empty() ? "—" : identity.wake_word.c_str());
    lv_label_set_text_fmt(detail_values_[2], "%s · %s",
                          wake_state.runtime_name.empty() ? "wake-runtime" : wake_state.runtime_name.c_str(),
                          wake_state.runtime_available ? "就绪" : "未安装");
    if (status_light_ != nullptr) {
        const auto light = status_light_->settings();
        if (light.enabled) {
            lv_label_set_text_fmt(detail_values_[3], "%u%%", static_cast<unsigned>(light.max_brightness_percent));
        } else {
            lv_label_set_text(detail_values_[3], "已关闭");
        }
    } else {
        lv_label_set_text(detail_values_[3], "不可用");
    }
    lv_label_set_text(detail_values_[4], rodakos::CloudDiagnosticTitleZh(cloud_state.code));
    lv_label_set_text_fmt(detail_raw_label_, "phase=%s wake=%s id_rev=%u", PhaseText(assistant_state.phase),
                          WakeStatusText(wake_state.status), static_cast<unsigned>(identity.revision));
}

void RegisterAssistantApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "assistant",
        .title = "Assistant",
        .icon = ICON_ASSISTANT,
        .category = PhoneAppCategory::kTools,
        .icon_tint = PhoneAppIconTint::kCool,
        .capabilities = PhoneCapability::kNetwork,
        .show_on_home = true,
        .aliases = {"voice", "assistant", "siri", "语音", "助手"},
        .create = []() { return std::make_unique<AssistantApp>(); },
    });
}

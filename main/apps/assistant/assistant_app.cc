#include "apps/assistant/assistant_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/voice_assistant_service.h"
#include "phone_os/voice_wake_service.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <esp_log.h>

#include <memory>

namespace {
constexpr const char* TAG = "AssistantApp";

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        context->navigation().ReturnHome();
    }
}

void RefreshTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<AssistantApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->RefreshState();
    }
}

lv_obj_t* CreateText(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

const char* PhaseText(rodakos::VoiceAssistantPhase phase) {
    switch (phase) {
        case rodakos::VoiceAssistantPhase::kConnecting:
            return "Connecting";
        case rodakos::VoiceAssistantPhase::kListening:
            return "Listening";
        case rodakos::VoiceAssistantPhase::kSpeaking:
            return "Speaking";
        case rodakos::VoiceAssistantPhase::kError:
            return "Error";
        case rodakos::VoiceAssistantPhase::kIdle:
        default:
            return "Ready";
    }
}

const char* WakeStatusText(rodakos::VoiceWakeStatus status) {
    switch (status) {
        case rodakos::VoiceWakeStatus::kListening:
            return "Listening";
        case rodakos::VoiceWakeStatus::kAssistantActive:
            return "Assistant active";
        case rodakos::VoiceWakeStatus::kUnavailable:
            return "Unavailable";
        case rodakos::VoiceWakeStatus::kError:
            return "Error";
        case rodakos::VoiceWakeStatus::kDisabled:
        default:
            return "Disabled";
    }
}

lv_obj_t* CreateCard(lv_obj_t* parent, lv_coord_t y, lv_coord_t height) {
    auto* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 300, height);
    lv_obj_set_pos(card, 10, y);
    lv_obj_set_style_bg_color(card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

void StyleSwitch(lv_obj_t* sw) {
    lv_obj_set_size(sw, 48, 26);
    lv_obj_set_style_bg_color(sw, rodakos_theme_bg_tertiary(), LV_PART_MAIN);
    const auto checked_indicator = static_cast<lv_style_selector_t>(
        static_cast<uint32_t>(LV_PART_INDICATOR) | static_cast<uint32_t>(LV_STATE_CHECKED));
    lv_obj_set_style_bg_color(sw, rodakos_theme_primary(), checked_indicator);
    lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_KNOB);
}

}  // namespace

bool AssistantApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    assistant_ = context.services().voice_assistant();
    wake_ = context.services().voice_wake();

    if (assistant_ != nullptr) {
        assistant_->Init();
    }
    if (wake_ != nullptr) {
        wake_->Init();
    }

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    CreateUi();
    RefreshState();
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, 500, this);

    ESP_LOGI(TAG, "Assistant app created");
    return true;
}

void AssistantApp::OnResume() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked() && root_ != nullptr && lv_obj_is_valid(root_)) {
            lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(root_);
            RefreshState();
        }
    }
}

void AssistantApp::OnPause() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked() && root_ != nullptr && lv_obj_is_valid(root_)) {
            lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void AssistantApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            if (refresh_timer_ != nullptr) {
                lv_timer_delete(refresh_timer_);
                refresh_timer_ = nullptr;
            }
            if (root_ != nullptr && lv_obj_is_valid(root_)) {
                lv_obj_delete(root_);
            }
        }
    }

    root_ = nullptr;
    wake_switch_ = nullptr;
    wake_status_label_ = nullptr;
    assistant_status_label_ = nullptr;
    assistant_detail_label_ = nullptr;
    runtime_detail_label_ = nullptr;
    cloud_detail_label_ = nullptr;
    context_ = nullptr;
    ui_ = nullptr;
    assistant_ = nullptr;
    wake_ = nullptr;
}

void AssistantApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    CreateAppHeader(root_, "Assistant", [](lv_event_t* e) {
        auto* self = static_cast<AssistantApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, [](lv_event_t* e) {
        auto* self = static_cast<AssistantApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, this);

    auto* wake_card = CreateCard(root_, 46, 62);

    auto* wake_icon = CreateText(wake_card, FONT_AWESOME_MICROPHONE,
                                 PhoneIconFont(), rodakos_theme_primary());
    lv_obj_align(wake_icon, LV_ALIGN_LEFT_MID, 12, 0);

    auto* wake_title = CreateText(wake_card, "你好达克", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_width(wake_title, 176);
    lv_label_set_long_mode(wake_title, LV_LABEL_LONG_DOT);
    lv_obj_align(wake_title, LV_ALIGN_TOP_LEFT, 44, 8);

    wake_status_label_ = CreateText(wake_card, "Disabled", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_width(wake_status_label_, 176);
    lv_label_set_long_mode(wake_status_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(wake_status_label_, LV_ALIGN_TOP_LEFT, 44, 32);

    wake_switch_ = lv_switch_create(wake_card);
    StyleSwitch(wake_switch_);
    lv_obj_align(wake_switch_, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_event_cb(wake_switch_, [](lv_event_t* e) {
        auto* self = static_cast<AssistantApp*>(lv_event_get_user_data(e));
        auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
        self->ToggleWakeListening(lv_obj_has_state(sw, LV_STATE_CHECKED));
    }, LV_EVENT_VALUE_CHANGED, this);

    auto* assistant_card = CreateCard(root_, 114, 48);
    auto* assistant_icon = CreateText(assistant_card, FONT_AWESOME_USER_ROBOT,
                                      PhoneIconFont(), rodakos_theme_primary());
    lv_obj_align(assistant_icon, LV_ALIGN_LEFT_MID, 12, 0);

    assistant_status_label_ = CreateText(assistant_card, "Assistant", &phone_font_14,
                                         rodakos_theme_text_primary());
    lv_obj_set_width(assistant_status_label_, 220);
    lv_label_set_long_mode(assistant_status_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(assistant_status_label_, LV_ALIGN_TOP_LEFT, 44, 5);

    assistant_detail_label_ = CreateText(assistant_card, "Ready", &phone_font_12,
                                         rodakos_theme_text_tertiary());
    lv_obj_set_width(assistant_detail_label_, 220);
    lv_label_set_long_mode(assistant_detail_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(assistant_detail_label_, LV_ALIGN_TOP_LEFT, 44, 27);

    auto* runtime_card = CreateCard(root_, 168, 32);
    auto* runtime_icon = CreateText(runtime_card, FONT_AWESOME_CIRCLE_INFO, PhoneIconFont(),
                                    rodakos_theme_primary());
    lv_obj_align(runtime_icon, LV_ALIGN_LEFT_MID, 12, 0);

    runtime_detail_label_ = CreateText(runtime_card, "Wake runtime: unavailable", &phone_font_12,
                                       rodakos_theme_text_secondary());
    lv_obj_set_width(runtime_detail_label_, 236);
    lv_label_set_long_mode(runtime_detail_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(runtime_detail_label_, LV_ALIGN_LEFT_MID, 44, 0);

    auto* cloud_card = CreateCard(root_, 206, 32);
    auto* cloud_icon = CreateText(cloud_card, FONT_AWESOME_CLOUD, PhoneIconFont(), rodakos_theme_primary());
    lv_obj_align(cloud_icon, LV_ALIGN_LEFT_MID, 12, 0);

    cloud_detail_label_ = CreateText(cloud_card, "Wake-only cloud", &phone_font_12,
                                     rodakos_theme_text_tertiary());
    lv_obj_set_width(cloud_detail_label_, 236);
    lv_label_set_long_mode(cloud_detail_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(cloud_detail_label_, LV_ALIGN_LEFT_MID, 44, 0);
}

void AssistantApp::RefreshState() {
    if (assistant_status_label_ == nullptr || assistant_ == nullptr) {
        return;
    }

    const auto assistant_state = assistant_->GetState();
    lv_label_set_text_fmt(assistant_status_label_, "Assistant - %s",
                          PhaseText(assistant_state.phase));
    lv_label_set_text_fmt(assistant_detail_label_, "%s%s",
                          assistant_state.focus_active ? "Exclusive focus - " : "",
                          assistant_state.message.empty() ? "Ready" : assistant_state.message.c_str());

    if (wake_ != nullptr && wake_switch_ != nullptr) {
        const auto wake_state = wake_->GetState();
        if (wake_state.enabled) {
            lv_obj_add_state(wake_switch_, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(wake_switch_, LV_STATE_CHECKED);
        }
        lv_label_set_text_fmt(wake_status_label_, "%s - %s",
                              WakeStatusText(wake_state.status),
                              wake_state.message.empty() ? "Ready" : wake_state.message.c_str());
        lv_obj_set_style_text_color(
            wake_status_label_,
            wake_state.status == rodakos::VoiceWakeStatus::kListening ? rodakos_theme_primary()
                                                                      : rodakos_theme_text_secondary(),
            0);
        lv_label_set_text_fmt(runtime_detail_label_, "%s - %s",
                              wake_state.runtime_name.empty() ? "wake-runtime" : wake_state.runtime_name.c_str(),
                              wake_state.runtime_available ? "ready" : "not installed");
    } else if (wake_status_label_ != nullptr) {
        lv_label_set_text(wake_status_label_, "Wake service unavailable");
    }

    lv_label_set_text_fmt(cloud_detail_label_, "%s/%s - %s",
                          assistant_state.transport_name.empty() ? "offline" : assistant_state.transport_name.c_str(),
                          assistant_state.recorder_name.empty() ? "offline" : assistant_state.recorder_name.c_str(),
                          assistant_state.transport_active ? "wake session" : "wake-only");
}

void AssistantApp::ToggleWakeListening(bool enabled) {
    if (wake_ == nullptr) {
        ui_->ShowToastUnlocked("Wake service unavailable");
        return;
    }
    if (!wake_->SetEnabled(enabled)) {
        const auto state = wake_->GetState();
        ui_->ShowToastUnlocked(
            state.message.empty() ? "Wake setting failed" : state.message.c_str());
    } else {
        ui_->ShowToastUnlocked(enabled ? "Wake listening enabled" : "Wake listening disabled");
    }
    RefreshState();
}

void AssistantApp::NavigateHome() {
    lv_async_call(DeferReturnHome, context_);
}

void RegisterAssistantApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "assistant",
        .title = "Assistant",
        .icon = FONT_AWESOME_USER_ROBOT,
        .category = PhoneAppCategory::kTools,
        .capabilities = PhoneCapability::kNetwork,
        .show_on_home = true,
        .aliases = {"voice", "assistant", "siri", "语音", "助手"},
        .create = []() { return std::make_unique<AssistantApp>(); },
    });
}

#include "apps/assistant/assistant_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/voice_assistant_service.h"
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
        lv_indev_reset(nullptr, nullptr);
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

void StyleButton(lv_obj_t* button, lv_coord_t width, bool primary) {
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, width, 34);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, primary ? rodakos_theme_primary() : rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, rodakos_theme_bg_secondary(), LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(button, 1, LV_STATE_PRESSED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
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

const char* TriggerText(rodakos::VoiceAssistantTrigger trigger) {
    switch (trigger) {
        case rodakos::VoiceAssistantTrigger::kWakeWord:
            return "Wake word";
        case rodakos::VoiceAssistantTrigger::kRemote:
            return "Remote";
        case rodakos::VoiceAssistantTrigger::kManual:
        default:
            return "Manual";
    }
}

}  // namespace

AssistantApp::~AssistantApp() {
    OnDestroy();
}

bool AssistantApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    assistant_ = context.services().voice_assistant();

    if (assistant_ != nullptr) {
        assistant_->Init();
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

void AssistantApp::OnShow() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked() && root_ != nullptr && lv_obj_is_valid(root_)) {
            lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(root_);
            RefreshState();
        }
    }
}

void AssistantApp::OnHide() {
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
    phase_label_ = nullptr;
    detail_label_ = nullptr;
    focus_label_ = nullptr;
    cloud_label_ = nullptr;
    cloud_detail_label_ = nullptr;
    context_ = nullptr;
    ui_ = nullptr;
    assistant_ = nullptr;
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

    auto* status_card = lv_obj_create(root_);
    lv_obj_remove_style_all(status_card);
    lv_obj_set_size(status_card, 300, 78);
    lv_obj_set_pos(status_card, 10, 48);
    lv_obj_set_style_bg_color(status_card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(status_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(status_card, 8, 0);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

    auto* assistant_icon = CreateText(status_card, FONT_AWESOME_USER_ROBOT,
                                      PhoneIconFontLarge(), rodakos_theme_primary());
    lv_obj_align(assistant_icon, LV_ALIGN_LEFT_MID, 14, 0);

    phase_label_ = CreateText(status_card, "Ready", &phone_font_18, rodakos_theme_text_primary());
    lv_obj_set_width(phase_label_, 168);
    lv_label_set_long_mode(phase_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(phase_label_, LV_ALIGN_TOP_LEFT, 56, 12);

    detail_label_ = CreateText(status_card, "Manual", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_width(detail_label_, 168);
    lv_label_set_long_mode(detail_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(detail_label_, LV_ALIGN_TOP_LEFT, 56, 40);

    focus_label_ = CreateText(status_card, "Idle", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_width(focus_label_, 72);
    lv_obj_set_style_text_align(focus_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(focus_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(focus_label_, LV_ALIGN_TOP_RIGHT, -12, 14);

    auto* cloud_card = lv_obj_create(root_);
    lv_obj_remove_style_all(cloud_card);
    lv_obj_set_size(cloud_card, 300, 42);
    lv_obj_set_pos(cloud_card, 10, 136);
    lv_obj_set_style_bg_color(cloud_card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(cloud_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cloud_card, 8, 0);
    lv_obj_clear_flag(cloud_card, LV_OBJ_FLAG_SCROLLABLE);

    auto* cloud_icon = CreateText(cloud_card, FONT_AWESOME_CLOUD, PhoneIconFont(), rodakos_theme_primary());
    lv_obj_align(cloud_icon, LV_ALIGN_LEFT_MID, 12, 0);

    cloud_label_ = CreateText(cloud_card, "Voice link", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_width(cloud_label_, 220);
    lv_label_set_long_mode(cloud_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(cloud_label_, LV_ALIGN_TOP_LEFT, 44, 5);

    cloud_detail_label_ = CreateText(cloud_card, "Offline", &phone_font_12,
                                     rodakos_theme_text_tertiary());
    lv_obj_set_width(cloud_detail_label_, 220);
    lv_label_set_long_mode(cloud_detail_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(cloud_detail_label_, LV_ALIGN_TOP_LEFT, 44, 23);

    auto* controls = lv_obj_create(root_);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, 300, 42);
    lv_obj_set_pos(controls, 10, 188);
    lv_obj_set_layout(controls, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(controls, 10, 0);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

    auto* talk_btn = lv_btn_create(controls);
    StyleButton(talk_btn, 145, true);
    auto* talk_label = CreateText(talk_btn, FONT_AWESOME_MICROPHONE "  Talk", &phone_font_14, lv_color_white());
    lv_obj_center(talk_label);
    lv_obj_add_event_cb(talk_btn, [](lv_event_t* e) {
        static_cast<AssistantApp*>(lv_event_get_user_data(e))->StartInteraction();
    }, LV_EVENT_CLICKED, this);

    auto* stop_btn = lv_btn_create(controls);
    StyleButton(stop_btn, 145, false);
    auto* stop_label = CreateText(stop_btn, FONT_AWESOME_STOP "  Stop", &phone_font_14,
                                  rodakos_theme_text_primary());
    lv_obj_center(stop_label);
    lv_obj_add_event_cb(stop_btn, [](lv_event_t* e) {
        static_cast<AssistantApp*>(lv_event_get_user_data(e))->StopInteraction();
    }, LV_EVENT_CLICKED, this);
}

void AssistantApp::RefreshState() {
    if (phase_label_ == nullptr || assistant_ == nullptr) {
        return;
    }

    const auto state = assistant_->GetState();
    lv_label_set_text(phase_label_, PhaseText(state.phase));
    lv_label_set_text_fmt(detail_label_, "%s - %s",
                          TriggerText(state.trigger),
                          state.message.empty() ? "Ready" : state.message.c_str());
    lv_label_set_text(focus_label_, state.focus_active ? "Exclusive" : "Idle");
    lv_obj_set_style_text_color(focus_label_,
                                state.focus_active ? rodakos_theme_primary()
                                                    : rodakos_theme_text_tertiary(),
                                0);
    lv_label_set_text_fmt(cloud_detail_label_, "%s/%s - %s",
                          state.transport_name.empty() ? "offline" : state.transport_name.c_str(),
                          state.recorder_name.empty() ? "offline" : state.recorder_name.c_str(),
                          state.transport_active && state.recorder_active ? "active" : "idle");
}

void AssistantApp::StartInteraction() {
    if (assistant_ == nullptr) {
        ui_->ShowToastUnlocked("Assistant unavailable");
        return;
    }
    if (!assistant_->StartInteraction(rodakos::VoiceAssistantTrigger::kManual)) {
        ui_->ShowToastUnlocked("Assistant failed");
    }
    RefreshState();
}

void AssistantApp::StopInteraction() {
    if (assistant_ != nullptr) {
        assistant_->StopInteraction();
    }
    RefreshState();
}

void AssistantApp::NavigateHome() {
    if (auto* indev = lv_indev_active(); indev != nullptr) {
        lv_indev_wait_release(indev);
    }
    lv_async_call(DeferReturnHome, context_);
}

void RegisterAssistantApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "assistant",
        .title = "Assistant",
        .icon = FONT_AWESOME_USER_ROBOT,
        .category = PhoneAppCategory::kTools,
        .launch_mode = PhoneAppLaunchMode::kReplaceCurrent,
        .capabilities = PhoneCapability::kNetwork |
                         PhoneCapability::kAudioPlayback |
                         PhoneCapability::kBackgroundTick,
        .show_on_home = true,
        .aliases = {"voice", "xiaozhi", "siri", "语音", "助手"},
        .create = []() { return std::make_unique<AssistantApp>(); },
    });
}

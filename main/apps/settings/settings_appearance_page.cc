#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/appearance_service.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"

using namespace rodakos_settings;

void SettingsApp::CreateAppearancePage() {
    appearance_body_ = lv_obj_create(lv_obj_get_parent(main_body_));
    lv_obj_remove_style_all(appearance_body_);
    lv_obj_set_size(appearance_body_, ui_->width(), lv_obj_get_height(main_body_));
    lv_obj_set_pos(appearance_body_, 0, lv_obj_get_y(main_body_));
    lv_obj_set_style_bg_opa(appearance_body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(appearance_body_, 0, 0);
    lv_obj_set_scroll_dir(appearance_body_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(appearance_body_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(appearance_body_, LV_OBJ_FLAG_HIDDEN);

    auto* guide = CreateSettingCard(appearance_body_, 4, 76);
    auto* guide_label = CreateSettingLabel(guide,
        "在 Rodak 对照发布端指纹。\n一致后，退出远程控制并在本机触摸确认。外观将在下次开机生效。", true);
    lv_obj_set_width(guide_label, 274);
    lv_label_set_long_mode(guide_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(guide_label, LV_ALIGN_LEFT_MID, 0, 0);

    auto* fingerprint = CreateSettingCard(appearance_body_, 88, 92);
    auto* title = CreateSettingLabel(fingerprint, "发布端指纹", true);
    lv_obj_set_style_text_font(title, &phone_font_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    appearance_fingerprint_label_ = CreateSettingLabel(fingerprint, "尚未读取");
    lv_obj_set_style_text_font(appearance_fingerprint_label_, &phone_font_14, 0);
    lv_obj_set_width(appearance_fingerprint_label_, 274);
    lv_label_set_long_mode(appearance_fingerprint_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(appearance_fingerprint_label_, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    auto* status = CreateSettingCard(appearance_body_, 188, 40);
    appearance_status_label_ = CreateSettingLabel(status, "尚未信任", true);
    lv_obj_align(appearance_status_label_, LV_ALIGN_LEFT_MID, 0, 0);

    appearance_refresh_button_ = lv_btn_create(appearance_body_);
    lv_obj_set_size(appearance_refresh_button_, 90, 38);
    lv_obj_set_pos(appearance_refresh_button_, 10, 236);
    lv_obj_set_style_bg_color(appearance_refresh_button_, rodakos_theme_bg_tertiary(), 0);
    auto* refresh_label = CreateSettingLabel(appearance_refresh_button_, "读取/刷新");
    lv_obj_center(refresh_label);
    lv_obj_add_event_cb(appearance_refresh_button_, [](lv_event_t* event) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(event));
        if (auto* service = self->context_->services().appearance(); service != nullptr) {
            service->RequestPublisher();
            self->UpdateAppearancePage();
        }
    }, LV_EVENT_CLICKED, this);

    appearance_confirm_button_ = lv_btn_create(appearance_body_);
    lv_obj_set_size(appearance_confirm_button_, 200, 38);
    lv_obj_set_pos(appearance_confirm_button_, 110, 236);
    lv_obj_set_style_bg_color(appearance_confirm_button_, rodakos_theme_primary(), 0);
    auto* confirm_label = CreateSettingLabel(appearance_confirm_button_, "指纹一致，确认信任");
    lv_obj_set_style_text_color(confirm_label, ui_->theme().accent_text, 0);
    lv_obj_center(confirm_label);
    lv_obj_add_event_cb(appearance_confirm_button_, [](lv_event_t* event) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(event));
        const lv_event_code_t code = lv_event_get_code(event);
        if (code == LV_EVENT_PRESSED) {
            self->appearance_pressed_key_id_ = self->ui_->IsPhysicalInput()
                ? self->appearance_displayed_key_id_ : std::string();
        } else if (code == LV_EVENT_CLICKED) {
            self->ConfirmAppearancePublisher();
            self->appearance_pressed_key_id_.clear();
        } else if (code == LV_EVENT_PRESS_LOST) {
            self->appearance_pressed_key_id_.clear();
        }
    }, LV_EVENT_ALL, this);

    appearance_error_label_ = CreateSettingLabel(appearance_body_, "", true);
    lv_obj_set_width(appearance_error_label_, 296);
    lv_obj_set_pos(appearance_error_label_, 12, 286);
    lv_label_set_long_mode(appearance_error_label_, LV_LABEL_LONG_WRAP);
    appearance_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* self = static_cast<SettingsApp*>(lv_timer_get_user_data(timer));
        if (self != nullptr && self->current_page_ == SettingsPage::kAppearance) {
            self->UpdateAppearancePage();
        }
    }, 750, this);
    if (auto* service = context_->services().appearance(); service != nullptr) {
        service->RequestPublisher();
    }
}

void SettingsApp::UpdateAppearancePage() {
    if (appearance_body_ == nullptr || context_ == nullptr) return;
    auto* service = context_->services().appearance();
    if (service == nullptr) {
        lv_label_set_text(appearance_status_label_, "外观服务不可用");
        lv_obj_add_state(appearance_confirm_button_, LV_STATE_DISABLED);
        lv_obj_add_state(appearance_refresh_button_, LV_STATE_DISABLED);
        return;
    }
    const auto publisher = service->GetPublisherState();
    appearance_displayed_key_id_ = publisher.key_id;
    std::string fingerprint = publisher.fingerprint;
    const size_t middle = fingerprint.find(' ', 19);
    if (middle != std::string::npos) fingerprint[middle] = '\n';
    lv_label_set_text(appearance_fingerprint_label_, fingerprint.empty() ? "尚未读取" : fingerprint.c_str());
    lv_label_set_text(appearance_status_label_, publisher.loading ? "正在读取发布端..." :
        publisher.trusted ? "已信任此发布端" : "尚未信任");
    lv_obj_set_style_text_color(appearance_status_label_, publisher.trusted ?
        rodakos_theme_success() : rodakos_theme_text_secondary(), 0);
    lv_label_set_text(appearance_error_label_, publisher.error.c_str());
    if (publisher.loading) lv_obj_add_state(appearance_refresh_button_, LV_STATE_DISABLED);
    else lv_obj_remove_state(appearance_refresh_button_, LV_STATE_DISABLED);
    if (publisher.loading || publisher.trusted || publisher.key_id.empty() ||
        publisher.public_key.empty() || !publisher.error.empty()) {
        lv_obj_add_state(appearance_confirm_button_, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(appearance_confirm_button_, LV_STATE_DISABLED);
    }
}

void SettingsApp::ConfirmAppearancePublisher() {
    if (context_ == nullptr || ui_ == nullptr) return;
    // 远程按下后断开租约会产生释放事件，不能将该释放误认成本机授权。
    if (!ui_->IsPhysicalInput() || appearance_pressed_key_id_.empty()) {
        ui_->ShowToastUnlocked("请退出远程控制后在本机确认");
        return;
    }
    auto* service = context_->services().appearance();
    if (service == nullptr || appearance_displayed_key_id_.empty()) return;
    const auto publisher = service->GetPublisherState();
    if (publisher.loading || publisher.key_id != appearance_displayed_key_id_ ||
        publisher.key_id != appearance_pressed_key_id_) {
        UpdateAppearancePage();
        ui_->ShowToastUnlocked("发布端已变化，请重新对照指纹");
        return;
    }
    const bool confirmed = service->ConfirmPublisher(appearance_displayed_key_id_);
    UpdateAppearancePage();
    ui_->ShowToastUnlocked(confirmed ? "发布端已确认" : "保存信任失败，请重试");
}

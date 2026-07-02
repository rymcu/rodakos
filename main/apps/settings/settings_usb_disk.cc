#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "usb_msc_mode.h"

#include <esp_system.h>

namespace {
void RestartTimerCallback(lv_timer_t* timer) {
    lv_timer_delete(timer);
    esp_restart();
}
}  // namespace

using namespace rodakos_settings;
void SettingsApp::ShowUsbDiskDialog() {
    if (usb_disk_dialog_ != nullptr) {
        return;
    }

    usb_disk_dialog_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(usb_disk_dialog_);
    lv_obj_set_size(usb_disk_dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(usb_disk_dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(usb_disk_dialog_, LV_OPA_70, 0);
    lv_obj_clear_flag(usb_disk_dialog_, LV_OBJ_FLAG_SCROLLABLE);

    auto* dialog_box = lv_obj_create(usb_disk_dialog_);
    lv_obj_remove_style_all(dialog_box);
    lv_obj_set_size(dialog_box, 286, 154);
    lv_obj_center(dialog_box);
    lv_obj_set_style_bg_color(dialog_box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(dialog_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dialog_box, 10, 0);
    lv_obj_set_style_pad_all(dialog_box, 14, 0);
    lv_obj_clear_flag(dialog_box, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = CreateSettingLabel(dialog_box, "USB Disk Mode");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    auto* message = CreateSettingLabel(
        dialog_box,
        "Reboot and share SD card with PC.\nSafely eject before reset.",
        true);
    lv_obj_set_width(message, 250);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 36);

    auto* cancel_btn = lv_btn_create(dialog_box);
    lv_obj_set_size(cancel_btn, 108, 34);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cancel_btn, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_set_style_shadow_width(cancel_btn, 0, 0);

    auto* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_color(cancel_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(cancel_label, &phone_font_12, 0);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->CloseUsbDiskDialog();
    }, LV_EVENT_CLICKED, this);

    auto* enter_btn = lv_btn_create(dialog_box);
    lv_obj_set_size(enter_btn, 108, 34);
    lv_obj_align(enter_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(enter_btn, rodakos_theme_primary(), 0);
    lv_obj_set_style_radius(enter_btn, 6, 0);
    lv_obj_set_style_shadow_width(enter_btn, 0, 0);

    auto* enter_label = lv_label_create(enter_btn);
    lv_label_set_text(enter_label, "Enter");
    lv_obj_set_style_text_color(enter_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(enter_label, &phone_font_12, 0);
    lv_obj_center(enter_label);
    lv_obj_add_event_cb(enter_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->EnterUsbDiskMode();
    }, LV_EVENT_CLICKED, this);
}

void SettingsApp::CloseUsbDiskDialog() {
    if (usb_disk_dialog_ != nullptr && lv_obj_is_valid(usb_disk_dialog_)) {
        lv_obj_delete(usb_disk_dialog_);
    }
    usb_disk_dialog_ = nullptr;
}

void SettingsApp::EnterUsbDiskMode() {
    if (!RequestUsbMscModeOnNextBoot()) {
        ui_->ShowToastUnlocked("USB disk request failed");
        return;
    }

    CloseUsbDiskDialog();
    ShowUsbDiskEnablePage();
}

void SettingsApp::ShowUsbDiskEnablePage() {
    if (usb_disk_hint_page_ != nullptr) {
        return;
    }

    usb_disk_hint_page_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(usb_disk_hint_page_);
    lv_obj_set_size(usb_disk_hint_page_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(usb_disk_hint_page_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(usb_disk_hint_page_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(usb_disk_hint_page_, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = lv_label_create(usb_disk_hint_page_);
    lv_label_set_text(title, "USB Disk Mode");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_set_style_text_color(title, rodakos_theme_text_primary(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    auto* icon = lv_label_create(usb_disk_hint_page_);
    lv_label_set_text(icon, FONT_AWESOME_SD_CARD);
    lv_obj_set_style_text_font(icon, PhoneIconFontLarge(), 0);
    lv_obj_set_style_text_color(icon, rodakos_theme_primary(), 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 52);

    auto* message = lv_label_create(usb_disk_hint_page_);
    lv_label_set_text(message,
                      "Enabling USB disk...\n\n"
                      "The SD card will appear on your PC.\n"
                      "Touch is disabled in this mode.\n\n"
                      "To return:\n"
                      "1. Safely eject on the PC\n"
                      "2. Press reset or power cycle");
    lv_obj_set_width(message, 286);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(message, &phone_font_12, 0);
    lv_obj_set_style_text_color(message, rodakos_theme_text_secondary(), 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 92);

    auto* footer = lv_label_create(usb_disk_hint_page_);
    lv_label_set_text(footer, "Rebooting now...");
    lv_obj_set_style_text_font(footer, &phone_font_12, 0);
    lv_obj_set_style_text_color(footer, rodakos_theme_primary(), 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_move_foreground(usb_disk_hint_page_);
    lv_refr_now(nullptr);

    if (usb_disk_restart_timer_ != nullptr) {
        lv_timer_delete(usb_disk_restart_timer_);
    }
    usb_disk_restart_timer_ = lv_timer_create(RestartTimerCallback, 1200, nullptr);
    lv_timer_set_repeat_count(usb_disk_restart_timer_, 1);
}

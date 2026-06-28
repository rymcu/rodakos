#include "phone_ui/phone_screen.h"

#include "phone_ui/phone_components.h"

#include <utility>

namespace {
void RunDeferredCallback(void* data) {
    auto* cb = static_cast<std::function<void()>*>(data);
    if (cb != nullptr) {
        (*cb)();
        delete cb;
    }
}
}  // namespace

PhoneScreen::PhoneScreen(PhoneUi& ui, Options options) : ui_(ui), back_callback_(std::move(options.on_back)) {
    PhoneUiLock lock(ui_);
    if (!lock.locked()) {
        return;
    }

    const auto& theme = ui_.theme();
    root_ = lv_obj_create(ui_.screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, theme.background, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    auto* top = lv_obj_create(root_);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), 54);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t title_x = 16;
    if (options.show_back) {
        auto* back = PhoneCreateIconButton(ui_, top, "<", 34, true);
        lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 10);
        lv_obj_add_event_cb(back, [](lv_event_t* e) {
            auto* self = static_cast<PhoneScreen*>(lv_event_get_user_data(e));
            if (self != nullptr && self->back_callback_) {
                lv_async_call(RunDeferredCallback, new std::function<void()>(self->back_callback_));
            }
        }, LV_EVENT_CLICKED, this);
        title_x = 52;
    }

    if (!options.icon.empty()) {
        auto* icon = lv_label_create(top);
        lv_label_set_text(icon, options.icon.c_str());
        lv_obj_set_style_text_color(icon, theme.accent, 0);
        lv_obj_align(icon, LV_ALIGN_TOP_LEFT, title_x, 13);
        title_x += 28;
    }

    auto* title = lv_label_create(top);
    lv_label_set_text(title, options.title.c_str());
    lv_obj_set_width(title, ui_.width() - title_x - 12);
    lv_obj_set_style_text_color(title, theme.text_primary, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, title_x, 10);

    if (!options.subtitle.empty()) {
        auto* subtitle = lv_label_create(top);
        lv_label_set_text(subtitle, options.subtitle.c_str());
        lv_obj_set_width(subtitle, ui_.width() - title_x - 12);
        lv_obj_set_style_text_color(subtitle, theme.text_secondary, 0);
        lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);
        lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, title_x, 32);
    }

    content_ = lv_obj_create(root_);
    lv_obj_remove_style_all(content_);
    lv_obj_set_size(content_, ui_.width(), ui_.height() - 54);
    lv_obj_align(content_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(content_, 10, 0);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
}

PhoneScreen::~PhoneScreen() {
    PhoneUiLock lock(ui_);
    if (lock.locked() && root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_del(root_);
    }
    root_ = nullptr;
    content_ = nullptr;
}

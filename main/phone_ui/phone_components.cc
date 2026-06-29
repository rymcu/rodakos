#include "phone_ui/phone_components.h"

#include "phone_ui/phone_fonts.h"
#include "phone_ui/rodakos_theme.h"

lv_obj_t* PhoneCreateIconButton(PhoneUi& ui, lv_obj_t* parent, const char* icon, lv_coord_t size, bool subtle) {
    const auto& theme = ui.theme();
    auto* button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, size, size);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, subtle ? theme.surface : theme.accent, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, subtle ? theme.surface_alt : theme.surface_alt, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(button, 1, LV_STATE_PRESSED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    auto* label = lv_label_create(button);
    lv_label_set_text(label, icon != nullptr ? icon : "");
    lv_obj_set_style_text_color(label, subtle ? theme.text_primary : theme.accent_text, 0);
    lv_obj_center(label);
    return button;
}

lv_obj_t* PhoneCreateCard(PhoneUi& ui, lv_obj_t* parent, lv_coord_t width, lv_coord_t height) {
    const auto& theme = ui.theme();
    auto* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_bg_color(card, theme.surface, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, theme.border, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t* RodakosCreateHeaderIconButton(lv_obj_t* parent, const char* icon) {
    auto* button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 40, 26);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_color(button, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, rodakos_theme_primary(), LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(button, 1, LV_STATE_PRESSED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    auto* label = lv_label_create(button);
    lv_label_set_text(label, icon != nullptr ? icon : "");
    lv_obj_set_style_text_color(label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(label, PhoneIconFont(), 0);
    lv_obj_center(label);
    return button;
}

lv_obj_t* CreateAppHeader(lv_obj_t* parent,
                          const char* title,
                          lv_event_cb_t back_cb,
                          lv_event_cb_t home_cb,
                          void* user_data,
                          lv_obj_t** title_label) {
    auto* header = lv_obj_create(parent);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), kRodakosAppHeaderHeight);
    lv_obj_set_style_bg_color(header, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(header, kRodakosAppHeaderPadding, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    auto* back_btn = RodakosCreateHeaderIconButton(header, FONT_AWESOME_ARROW_LEFT);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    if (back_cb != nullptr) {
        lv_obj_add_event_cb(back_btn, back_cb, LV_EVENT_CLICKED, user_data);
    }

    auto* label = lv_label_create(header);
    lv_label_set_text(label, title != nullptr ? title : "");
    lv_obj_set_style_text_color(label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(label, &phone_font_18, 0);
    lv_obj_center(label);

    auto* home_btn = RodakosCreateHeaderIconButton(header, FONT_AWESOME_HOUSE);
    lv_obj_align(home_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    if (home_cb != nullptr) {
        lv_obj_add_event_cb(home_btn, home_cb, LV_EVENT_CLICKED, user_data);
    }

    if (title_label != nullptr) {
        *title_label = label;
    }
    return header;
}

lv_obj_t* PhoneCreateLabel(PhoneUi& ui, lv_obj_t* parent, const char* text, bool secondary) {
    const auto& theme = ui.theme();
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_color(label, secondary ? theme.text_secondary : theme.text_primary, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

lv_obj_t* PhoneCreateTextButton(PhoneUi& ui, lv_obj_t* parent, const char* text, lv_coord_t width, lv_coord_t height) {
    const auto& theme = ui.theme();
    auto* button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, theme.accent, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, theme.surface_alt, LV_STATE_PRESSED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    auto* label = lv_label_create(button);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_color(label, theme.accent_text, 0);
    lv_obj_center(label);
    return button;
}

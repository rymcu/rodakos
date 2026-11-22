#include "apps/settings/settings_app_internal.h"

#include "apps/settings/settings_app.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_navigation.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"

namespace rodakos_settings {

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        // Settings 的页面返回由自己的异步导航处理，直接切换到 Home。
        context->navigation().Launch("home");
    }
}

void DeferOpenThemes(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        context->navigation().LaunchFrom("themes", "settings");
    }
}

std::string TrimServerName(const char* text) {
    if (text == nullptr) {
        return "";
    }
    std::string value(text);
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1);
    if (value.size() > 63) {
        value.resize(63);
    }
    return value;
}

std::string TrimCloudUrl(const char* text) {
    if (text == nullptr) {
        return "";
    }
    std::string value(text);
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1);
    if (value.size() > 191) {
        value.resize(191);
    }
    return value;
}

void UpdateBrightnessLabel(lv_obj_t* label, int value) {
    if (label != nullptr) {
        lv_label_set_text_fmt(label, "%d%%", value);
    }
}

lv_obj_t* CreateSettingLabel(lv_obj_t* parent, const char* text, bool secondary) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label,
        secondary ? rodakos_theme_text_secondary() : rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(label, &phone_font_14, 0);
    return label;
}

lv_obj_t* CreateSettingIcon(lv_obj_t* parent, const char* icon) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_color(label, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(label, PhoneIconFont(), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    return label;
}

lv_obj_t* CreateGroupCaption(lv_obj_t* parent, const char* text, lv_coord_t& y) {
    auto* caption = CreateSettingLabel(parent, text, true);
    lv_obj_set_style_text_font(caption, &phone_font_12, 0);
    lv_obj_set_pos(caption, kGridX + 4, y + 3);
    y += kGroupLabelHeight;
    return caption;
}

lv_obj_t* CreateGroupList(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t width,
                          lv_coord_t height) {
    auto* list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, width, height);
    lv_obj_set_pos(list, x, y);
    lv_obj_set_style_bg_color(list, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list, kListRadius, 0);
    lv_obj_set_style_clip_corner(list, true, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    return list;
}

lv_obj_t* CreateListRow(lv_obj_t* list, int index, lv_coord_t width, lv_coord_t height,
                        bool clickable) {
    auto* row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, width, height);
    lv_obj_set_pos(row, 0, height * index);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    if (clickable) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(row, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    }
    if (index > 0) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, rodakos_theme_bg_tertiary(), 0);
    }
    return row;
}

lv_obj_t* CreateRowChevron(lv_obj_t* row) {
    auto* arrow = lv_label_create(row);
    lv_label_set_text(arrow, ICON_CHEVRON_RIGHT);
    lv_obj_set_style_text_color(arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(arrow, PhoneIconFont(), 0);
    // The 20 px glyph has ~6 px of side bearing; pull it to the row's text edge.
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 4, 0);
    return arrow;
}

lv_obj_t* CreateRowValue(lv_obj_t* row, lv_coord_t width, bool chevron) {
    auto* value = CreateSettingLabel(row, "", true);
    lv_obj_set_width(value, width);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(value, &phone_font_12, 0);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, chevron ? -18 : 0, 0);
    return value;
}

void StyleSettingButton(lv_obj_t* button, lv_obj_t* label, SettingButtonStyle style) {
    lv_color_t bg = rodakos_theme_bg_tertiary();
    lv_color_t text = rodakos_theme_text_primary();
    lv_opa_t opa = LV_OPA_COVER;
    lv_coord_t border = 0;
    switch (style) {
        case SettingButtonStyle::kPrimary:
            bg = rodakos_theme_primary();
            text = rodakos_theme_on_primary();
            break;
        case SettingButtonStyle::kDanger:
            bg = rodakos_theme_error();
            text = rodakos_theme_on_error();
            break;
        case SettingButtonStyle::kDangerOutline:
            text = rodakos_theme_error();
            opa = LV_OPA_TRANSP;
            border = 1;
            break;
        case SettingButtonStyle::kSecondary:
            break;
    }
    lv_obj_set_style_bg_color(button, bg, 0);
    lv_obj_set_style_bg_opa(button, opa, 0);
    lv_obj_set_style_border_width(button, border, 0);
    lv_obj_set_style_border_color(button, rodakos_theme_error(), 0);
    if (label != nullptr) lv_obj_set_style_text_color(label, text, 0);
}

lv_obj_t* CreateSettingButton(lv_obj_t* parent, const char* text, SettingButtonStyle style,
                              lv_coord_t width, lv_coord_t height, lv_obj_t** label_out) {
    auto* button = lv_btn_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, kButtonRadius, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    // A fixed width so long text (server names) ends in "…" instead of wrapping out of the button.
    lv_obj_set_width(label, width - 12);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &phone_font_14, 0);
    lv_obj_center(label);
    StyleSettingButton(button, label, style);
    if (label_out != nullptr) *label_out = label;
    return button;
}

SettingDialog CreateSettingDialog(lv_coord_t height) {
    SettingDialog dialog;
    dialog.layer = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(dialog.layer);
    lv_obj_set_size(dialog.layer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(dialog.layer, rodakos_theme_overlay(), 0);
    lv_obj_set_style_bg_opa(dialog.layer, LV_OPA_70, 0);
    lv_obj_clear_flag(dialog.layer, LV_OBJ_FLAG_SCROLLABLE);
    // Swallow taps outside the box so the page underneath does not react.
    lv_obj_add_flag(dialog.layer, LV_OBJ_FLAG_CLICKABLE);

    dialog.box = lv_obj_create(dialog.layer);
    lv_obj_remove_style_all(dialog.box);
    lv_obj_set_size(dialog.box, kDialogWidth, height);
    lv_obj_center(dialog.box);
    lv_obj_set_style_bg_color(dialog.box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(dialog.box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dialog.box, 10, 0);
    lv_obj_set_style_pad_all(dialog.box, kDialogPad, 0);
    lv_obj_clear_flag(dialog.box, LV_OBJ_FLAG_SCROLLABLE);
    return dialog;
}

lv_obj_t* CreateDialogTitle(lv_obj_t* box, const char* text) {
    auto* title = CreateSettingLabel(box, text, false);
    lv_obj_set_width(title, kDialogContentWidth);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    return title;
}

lv_obj_t* CreateDialogMessage(lv_obj_t* box, const char* text, lv_coord_t y) {
    auto* message = CreateSettingLabel(box, text, true);
    lv_obj_set_width(message, kDialogContentWidth);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(message, &phone_font_12, 0);
    lv_obj_align(message, LV_ALIGN_TOP_LEFT, 0, y);
    return message;
}

lv_obj_t* CreateDialogButtons(lv_obj_t* box, const char* action, SettingButtonStyle style,
                              lv_event_cb_t cancel_cb, lv_event_cb_t action_cb, void* user_data) {
    auto* cancel = CreateSettingButton(box, "取消", SettingButtonStyle::kSecondary,
                                       kDialogHalfButton, kDialogButtonHeight);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(cancel, cancel_cb, LV_EVENT_CLICKED, user_data);
    auto* confirm = CreateSettingButton(box, action, style, kDialogHalfButton, kDialogButtonHeight);
    lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(confirm, action_cb, LV_EVENT_CLICKED, user_data);
    return confirm;
}

}  // namespace rodakos_settings

#include "apps/smart/smart_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <esp_err.h>
#include <esp_log.h>

#include <cstdint>
#include <cstdio>
#include <memory>

namespace {
constexpr const char* TAG = "SmartApp";
constexpr lv_coord_t kCardWidth = 300;
constexpr lv_coord_t kSelectorTop = 44;
constexpr lv_coord_t kSelectorHeight = 38;

struct ColorPreset {
    const char* label;
    rodakos::RgbColor color;
};

constexpr ColorPreset kColorPresets[] = {
    {"R", {255, 48, 48}},
    {"G", {40, 220, 120}},
    {"B", {48, 140, 255}},
    {"W", {255, 220, 170}},
    {"C", {0, 220, 220}},
    {"M", {220, 70, 255}},
};

constexpr size_t ColorPresetCount() {
    return sizeof(kColorPresets) / sizeof(kColorPresets[0]);
}

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        lv_indev_reset(nullptr, nullptr);
        context->navigation().ReturnHome();
    }
}

lv_obj_t* CreateText(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t* CreateCard(lv_obj_t* parent, lv_coord_t y, lv_coord_t height) {
    auto* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, kCardWidth, height);
    lv_obj_set_pos(card, 10, y);
    lv_obj_set_style_bg_color(card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

void StyleSwitch(lv_obj_t* sw) {
    const auto indicator_checked =
        static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
        static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
    lv_obj_set_size(sw, 48, 26);
    lv_obj_set_style_bg_color(sw, rodakos_theme_bg_tertiary(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, rodakos_theme_primary(), indicator_checked);
    lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_KNOB);
}

lv_color_t ToLvColor(rodakos::RgbColor color) {
    return lv_color_make(color.red, color.green, color.blue);
}

}  // namespace

SmartApp::~SmartApp() {
    OnDestroy();
}

bool SmartApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    lights_ = context.services().lights();

    if (lights_ != nullptr) {
        lights_->Init();
    }

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    CreateUi();
    Refresh();

    ESP_LOGI(TAG, "Smart app created");
    return true;
}

void SmartApp::OnShow() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked() && root_ != nullptr && lv_obj_is_valid(root_)) {
            lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(root_);
            Refresh();
        }
    }
}

void SmartApp::OnHide() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked() && root_ != nullptr && lv_obj_is_valid(root_)) {
            lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void SmartApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked() && root_ != nullptr && lv_obj_is_valid(root_)) {
            lv_obj_delete(root_);
        }
    }

    root_ = nullptr;
    light_list_ = nullptr;
    empty_label_ = nullptr;
    light_title_label_ = nullptr;
    power_switch_ = nullptr;
    status_label_ = nullptr;
    color_preview_ = nullptr;
    brightness_slider_ = nullptr;
    brightness_label_ = nullptr;
    light_buttons_.clear();
    preset_buttons_.clear();
    selected_index_ = 0;
    context_ = nullptr;
    ui_ = nullptr;
    lights_ = nullptr;
}

void SmartApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    CreateAppHeader(root_, "Smart", [](lv_event_t* e) {
        auto* self = static_cast<SmartApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, [](lv_event_t* e) {
        auto* self = static_cast<SmartApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, this);

    light_list_ = lv_obj_create(root_);
    lv_obj_remove_style_all(light_list_);
    lv_obj_set_size(light_list_, kCardWidth, kSelectorHeight);
    lv_obj_set_pos(light_list_, 10, kSelectorTop);
    lv_obj_set_style_bg_opa(light_list_, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(light_list_, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(light_list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(light_list_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(light_list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(light_list_, 6, 0);

    auto* power_card = CreateCard(root_, 86, 54);
    color_preview_ = lv_obj_create(power_card);
    lv_obj_remove_style_all(color_preview_);
    lv_obj_set_size(color_preview_, 32, 32);
    lv_obj_align(color_preview_, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_set_style_radius(color_preview_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(color_preview_, rodakos_theme_primary(), 0);
    lv_obj_set_style_bg_opa(color_preview_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(color_preview_, LV_OBJ_FLAG_SCROLLABLE);

    light_title_label_ = CreateText(power_card, "Light", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_width(light_title_label_, 170);
    lv_label_set_long_mode(light_title_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(light_title_label_, LV_ALIGN_TOP_LEFT, 56, 6);

    status_label_ = CreateText(power_card, "Starting", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_width(status_label_, 170);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(status_label_, LV_ALIGN_TOP_LEFT, 56, 29);

    power_switch_ = lv_switch_create(power_card);
    StyleSwitch(power_switch_);
    lv_obj_align(power_switch_, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_event_cb(power_switch_, [](lv_event_t* e) {
        auto* self = static_cast<SmartApp*>(lv_event_get_user_data(e));
        auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
        self->SetPower(lv_obj_has_state(sw, LV_STATE_CHECKED));
    }, LV_EVENT_VALUE_CHANGED, this);

    auto* brightness_card = CreateCard(root_, 146, 42);
    auto* brightness_title = CreateText(brightness_card, "Brightness", &phone_font_12,
                                        rodakos_theme_text_secondary());
    lv_obj_align(brightness_title, LV_ALIGN_TOP_LEFT, 12, 4);

    brightness_label_ = CreateText(brightness_card, "--%", &phone_font_12,
                                   rodakos_theme_text_primary());
    lv_obj_align(brightness_label_, LV_ALIGN_TOP_RIGHT, -12, 4);

    brightness_slider_ = lv_slider_create(brightness_card);
    lv_obj_set_size(brightness_slider_, 276, 12);
    lv_obj_align(brightness_slider_, LV_ALIGN_BOTTOM_MID, 0, -7);
    lv_slider_set_range(brightness_slider_, 0, 100);
    lv_obj_set_style_bg_color(brightness_slider_, rodakos_theme_bg_tertiary(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider_, rodakos_theme_primary(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider_, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(brightness_slider_, [](lv_event_t* e) {
        auto* self = static_cast<SmartApp*>(lv_event_get_user_data(e));
        auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        self->SetBrightness(static_cast<uint8_t>(lv_slider_get_value(slider)));
    }, LV_EVENT_VALUE_CHANGED, this);

    auto* color_card = CreateCard(root_, 194, 40);
    auto* color_title = CreateText(color_card, "Color", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_align(color_title, LV_ALIGN_TOP_LEFT, 12, 2);

    constexpr lv_coord_t kPresetX[] = {12, 61, 110, 159, 208, 252};
    for (size_t i = 0; i < ColorPresetCount(); ++i) {
        CreatePresetButton(color_card, kColorPresets[i].label, i, kPresetX[i], 17);
    }

    RebuildLightList();
}

void SmartApp::RebuildLightList() {
    if (light_list_ == nullptr) {
        return;
    }

    lv_obj_clean(light_list_);
    light_buttons_.clear();
    empty_label_ = nullptr;

    const auto light_count = (lights_ != nullptr) ? lights_->ListLights().size() : 0;
    if (light_count == 0) {
        empty_label_ = CreateText(light_list_, "No lights configured", &phone_font_12,
                                  rodakos_theme_text_secondary());
        lv_obj_set_width(empty_label_, 280);
        lv_obj_set_style_text_align(empty_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(empty_label_);
        selected_index_ = 0;
        return;
    }

    if (selected_index_ >= light_count) {
        selected_index_ = 0;
    }

    const auto& lights = lights_->ListLights();
    for (size_t i = 0; i < lights.size(); ++i) {
        CreateLightButton(light_list_, i);
    }
}

void SmartApp::CreateLightButton(lv_obj_t* parent, size_t light_index) {
    const auto* light = lights_ != nullptr ? lights_->GetLight(light_index) : nullptr;
    auto* btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 94, 30);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_pad_hor(btn, 8, 0);
    lv_obj_set_style_pad_ver(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    auto* text = CreateText(btn, light != nullptr ? light->title.c_str() : "Light",
                            &phone_font_12, rodakos_theme_text_primary());
    lv_obj_set_width(text, 78);
    lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
    lv_obj_align(text, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<uintptr_t>(light_index)));
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        auto* self = static_cast<SmartApp*>(lv_event_get_user_data(e));
        const auto index = static_cast<size_t>(reinterpret_cast<uintptr_t>(
            lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e)))));
        if (self != nullptr) {
            self->SelectLight(index);
        }
    }, LV_EVENT_CLICKED, this);

    light_buttons_.push_back(btn);
}

void SmartApp::CreatePresetButton(lv_obj_t* parent,
                                  const char* label,
                                  size_t preset_index,
                                  lv_coord_t x,
                                  lv_coord_t y) {
    const rodakos::RgbColor color = kColorPresets[preset_index].color;
    auto* btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 34, 19);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, ToLvColor(color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    auto* text = CreateText(btn, label, &phone_font_12, lv_color_white());
    lv_obj_center(text);

    lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<uintptr_t>(preset_index)));
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        auto* self = static_cast<SmartApp*>(lv_event_get_user_data(e));
        const auto index = static_cast<size_t>(reinterpret_cast<uintptr_t>(
            lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e)))));
        if (self != nullptr && index < ColorPresetCount()) {
            self->SetColor(kColorPresets[index].color);
        }
    }, LV_EVENT_CLICKED, this);
    preset_buttons_.push_back(btn);
}

void SmartApp::SelectLight(size_t index) {
    if (lights_ == nullptr || index >= lights_->ListLights().size()) {
        return;
    }
    selected_index_ = index;
    Refresh();
}

const rodakos::LightState* SmartApp::SelectedLight() const {
    if (lights_ == nullptr || selected_index_ >= lights_->ListLights().size()) {
        return nullptr;
    }
    return lights_->GetLight(selected_index_);
}

bool SmartApp::HasSelectedLight() const {
    return SelectedLight() != nullptr;
}

void SmartApp::SetControlsDisabled(bool disabled) {
    lv_obj_t* controls[] = {power_switch_, brightness_slider_};
    for (auto* control : controls) {
        if (control == nullptr) {
            continue;
        }
        if (disabled) {
            lv_obj_add_state(control, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(control, LV_STATE_DISABLED);
        }
    }

    for (auto* btn : preset_buttons_) {
        if (btn == nullptr) {
            continue;
        }
        if (disabled) {
            lv_obj_add_state(btn, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(btn, LV_STATE_DISABLED);
        }
    }
}

void SmartApp::Refresh() {
    const auto light_count = (lights_ != nullptr) ? lights_->ListLights().size() : 0;
    if (light_list_ != nullptr && light_count != light_buttons_.size()) {
        RebuildLightList();
    } else if (light_count > 0 && selected_index_ >= light_count) {
        selected_index_ = 0;
    }

    const auto* state = SelectedLight();
    SetControlsDisabled(state == nullptr);

    for (size_t i = 0; i < light_buttons_.size(); ++i) {
        auto* btn = light_buttons_[i];
        if (btn == nullptr) {
            continue;
        }
        const bool selected = i == selected_index_;
        lv_obj_set_style_bg_color(btn, selected ? rodakos_theme_primary()
                                                : rodakos_theme_bg_secondary(),
                                  0);
        lv_obj_set_style_border_color(btn, selected ? rodakos_theme_primary()
                                                    : rodakos_theme_bg_tertiary(),
                                      0);
        auto* label = lv_obj_get_child(btn, 0);
        if (label != nullptr) {
            lv_obj_set_style_text_color(label, selected ? lv_color_white()
                                                       : rodakos_theme_text_primary(),
                                        0);
        }
    }

    if (state == nullptr) {
        if (light_title_label_ != nullptr) {
            lv_label_set_text(light_title_label_, "No lights");
        }
        if (power_switch_ != nullptr) {
            lv_obj_remove_state(power_switch_, LV_STATE_CHECKED);
        }
        if (brightness_slider_ != nullptr) {
            lv_slider_set_value(brightness_slider_, 0, LV_ANIM_OFF);
        }
        if (brightness_label_ != nullptr) {
            lv_label_set_text(brightness_label_, "--%");
        }
        if (color_preview_ != nullptr) {
            lv_obj_set_style_bg_color(color_preview_, rodakos_theme_bg_tertiary(), 0);
            lv_obj_set_style_bg_opa(color_preview_, LV_OPA_40, 0);
        }
        if (status_label_ != nullptr) {
            lv_label_set_text(status_label_, lights_ == nullptr ? "Service unavailable"
                                                                : "No board light devices");
            lv_obj_set_style_text_color(status_label_, rodakos_theme_warning(), 0);
        }
        return;
    }

    if (light_title_label_ != nullptr) {
        lv_label_set_text(light_title_label_, state->title.c_str());
    }
    if (power_switch_ != nullptr) {
        if (state->enabled) {
            lv_obj_add_state(power_switch_, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(power_switch_, LV_STATE_CHECKED);
        }
    }
    if (brightness_slider_ != nullptr) {
        lv_slider_set_value(brightness_slider_, state->brightness_percent, LV_ANIM_OFF);
    }
    if (brightness_label_ != nullptr) {
        lv_label_set_text_fmt(brightness_label_, "%u%%",
                              static_cast<unsigned>(state->brightness_percent));
    }
    if (color_preview_ != nullptr) {
        lv_obj_set_style_bg_color(color_preview_, ToLvColor(state->color), 0);
        lv_obj_set_style_bg_opa(color_preview_,
                                state->enabled && state->available ? LV_OPA_COVER : LV_OPA_40,
                                0);
    }
    if (status_label_ != nullptr) {
        if (!state->available) {
            lv_label_set_text_fmt(status_label_, "Unavailable: %s",
                                  esp_err_to_name(state->last_error));
            lv_obj_set_style_text_color(status_label_, rodakos_theme_warning(), 0);
        } else {
            lv_label_set_text(status_label_, state->enabled ? "On" : "Off");
            lv_obj_set_style_text_color(status_label_,
                                        state->enabled ? rodakos_theme_primary()
                                                       : rodakos_theme_text_secondary(),
                                        0);
        }
    }
}

void SmartApp::SetPower(bool enabled) {
    if (!HasSelectedLight() || lights_ == nullptr || !lights_->SetEnabled(selected_index_, enabled)) {
        ui_->ShowToastUnlocked("Light unavailable");
    }
    Refresh();
}

void SmartApp::SetBrightness(uint8_t brightness) {
    if (!HasSelectedLight() || lights_ == nullptr || !lights_->SetBrightness(selected_index_, brightness)) {
        ui_->ShowToastUnlocked("Light unavailable");
    }
    Refresh();
}

void SmartApp::SetColor(rodakos::RgbColor color) {
    if (!HasSelectedLight() || lights_ == nullptr || !lights_->SetColor(selected_index_, color)) {
        ui_->ShowToastUnlocked("Light unavailable");
    }
    Refresh();
}

void SmartApp::NavigateHome() {
    if (auto* indev = lv_indev_active(); indev != nullptr) {
        lv_indev_wait_release(indev);
    }
    lv_async_call(DeferReturnHome, context_);
}

void RegisterSmartApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "smart",
        .title = "Smart",
        .icon = FONT_AWESOME_POWER_OFF,
        .category = PhoneAppCategory::kTools,
        .launch_mode = PhoneAppLaunchMode::kReplaceCurrent,
        .capabilities = PhoneCapability::kNone,
        .show_on_home = true,
        .aliases = {"light", "rgb", "lamp", "smart home", "灯", "灯光", "智能"},
        .create = []() { return std::make_unique<SmartApp>(); },
    });
}

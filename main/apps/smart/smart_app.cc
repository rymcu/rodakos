#include "apps/smart/smart_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <esp_log.h>

#include <cstdint>
#include <cstdio>
#include <memory>

namespace {
constexpr const char* TAG = "SmartApp";
constexpr uint32_t kRefreshMs = 500;

struct ColorPreset {
    const char* name;
    rodakos::RgbColor color;
};

constexpr ColorPreset kPresets[SmartApp::kPresetCount] = {
    {"暖白", {255, 220, 169}},
    {"冰蓝", {85, 184, 255}},
    {"青色", {44, 224, 208}},
    {"翠绿", {66, 220, 120}},
    {"珊瑚红", {255, 82, 82}},
    {"紫罗兰", {200, 85, 255}},
};

constexpr lv_coord_t kHeroY = 46;
constexpr lv_coord_t kBrightnessY = 116;
constexpr lv_coord_t kColorY = 174;

uint32_t Hex(rodakos::RgbColor color) {
    return (static_cast<uint32_t>(color.red) << 16) | (static_cast<uint32_t>(color.green) << 8) |
           color.blue;
}

bool SameColor(rodakos::RgbColor a, rodakos::RgbColor b) {
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

// Board light titles come from the board YAML in English; show the shipped board RGB in Chinese.
const char* DisplayName(const rodakos::LightState& state) {
    if (state.id == "board_rgb") return "板载彩灯";
    return state.title.empty() ? state.id.c_str() : state.title.c_str();
}

const char* PresetName(rodakos::RgbColor color) {
    for (const auto& preset : kPresets) {
        if (SameColor(preset.color, color)) return preset.name;
    }
    return "自定义";
}

lv_obj_t* CreateBox(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                    lv_coord_t radius) {
    auto* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_radius(box, radius, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

lv_obj_t* CreatePanel(lv_obj_t* parent, lv_coord_t y, lv_coord_t h) {
    auto* panel = CreateBox(parent, 10, y, 300, h, 10);
    lv_obj_set_style_bg_color(panel, rodakos_theme_bg_secondary(), 0);
    return panel;
}

lv_obj_t* CreateText(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t* CreateDot(lv_obj_t* parent) {
    auto* dot = CreateBox(parent, 0, 0, 8, 8, LV_RADIUS_CIRCLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    return dot;
}

// Filled dot for on, hollow ring for off, so the state never depends on colour alone.
void StyleDot(lv_obj_t* dot, bool filled, lv_color_t color) {
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_set_style_bg_opa(dot, filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(dot, color, 0);
    lv_obj_set_style_border_width(dot, filled ? 0 : 1, 0);
}

lv_obj_t* CreateSwitch(lv_obj_t* parent) {
    auto* sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 48, 26);
    lv_obj_set_style_bg_color(sw, rodakos_theme_bg_tertiary(), LV_PART_MAIN);
    const auto checked = static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
                         static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, rodakos_theme_primary(), checked);
    lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_KNOB);
    // The whole row toggles the light; the switch only mirrors the state.
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    return sw;
}

void SetChecked(lv_obj_t* obj, bool checked) {
    if (checked) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(obj, LV_STATE_CHECKED);
    }
}

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) context->navigation().ReturnHome();
}

}  // namespace

void SmartApp::RefreshTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<SmartApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) self->Refresh();
}

bool SmartApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    lights_ = context.services().lights();
    if (lights_ != nullptr) lights_->Init();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return false;

    const size_t count = lights_ != nullptr ? lights_->ListLights().size() : 0;
    view_ = count == 0 ? View::kEmpty : count == 1 ? View::kControl : View::kOverview;
    CreateUi();
    // MQTT and the voice status light change lights behind this page; poll so it stays true.
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, kRefreshMs, this);
    ESP_LOGI(TAG, "Smart app created, lights=%u", static_cast<unsigned>(count));
    return true;
}

void SmartApp::OnResume() {
    paused_ = false;
    if (ui_ == nullptr) return;
    PhoneUiLock lock(*ui_);
    if (!lock.locked() || root_ == nullptr || !lv_obj_is_valid(root_)) return;
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
    if (refresh_timer_ != nullptr) lv_timer_resume(refresh_timer_);
    Refresh();
}

void SmartApp::OnPause() {
    paused_ = true;
    if (ui_ == nullptr) return;
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return;
    if (refresh_timer_ != nullptr) lv_timer_pause(refresh_timer_);
    if (root_ != nullptr && lv_obj_is_valid(root_)) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void SmartApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            if (context_ != nullptr) lv_async_call_cancel(DeferReturnHome, context_);
            if (refresh_timer_ != nullptr) {
                lv_timer_delete(refresh_timer_);
                refresh_timer_ = nullptr;
            }
            DestroyUi();
        }
    }
    context_ = nullptr;
    ui_ = nullptr;
    lights_ = nullptr;
}

bool SmartApp::OnThemeChanged(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return false;
    const bool hidden = root_ != nullptr && lv_obj_is_valid(root_) &&
                        lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    DestroyUi();
    CreateUi();
    if (hidden && root_ != nullptr) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    return true;
}

bool SmartApp::OnBackRequested() {
    const size_t count = lights_ != nullptr ? lights_->ListLights().size() : 0;
    if (view_ == View::kControl && count > 1) {
        ShowView(View::kOverview);
        return true;
    }
    return false;
}

void SmartApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    auto back = [](lv_event_t* e) {
        static_cast<SmartApp*>(lv_event_get_user_data(e))->NavigateBack();
    };
    CreateAppHeader(root_, "智能家居", back, nullptr, this, &title_label_, nullptr);

    CreateControlView();
    CreateOverviewView();
    CreateEmptyView();
    ShowView(view_);
}

void SmartApp::DestroyUi() {
    if (root_ != nullptr && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = nullptr;
    title_label_ = nullptr;
    control_ = hero_ = orb_ = orb_icon_ = name_label_ = status_dot_ = status_label_ = nullptr;
    power_switch_ = brightness_icon_ = brightness_value_ = brightness_slider_ = nullptr;
    color_name_label_ = nullptr;
    presets_.fill(nullptr);
    preset_checks_.fill(nullptr);
    overview_ = nullptr;
    tiles_.fill(Tile{});
    empty_ = empty_hint_ = nullptr;
    dragging_ = false;
    drag_light_id_.clear();
}

void SmartApp::CreateControlView() {
    control_ = lv_obj_create(root_);
    lv_obj_remove_style_all(control_);
    lv_obj_set_size(control_, 320, 240);
    lv_obj_clear_flag(control_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    hero_ = CreatePanel(control_, kHeroY, 64);
    lv_obj_add_flag(hero_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(hero_, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(hero_, [](lv_event_t* e) {
        static_cast<SmartApp*>(lv_event_get_user_data(e))->TogglePower();
    }, LV_EVENT_CLICKED, this);

    orb_ = CreateBox(hero_, 12, 10, 44, 44, LV_RADIUS_CIRCLE);
    lv_obj_clear_flag(orb_, LV_OBJ_FLAG_CLICKABLE);
    orb_icon_ = CreateText(orb_, ICON_LIGHT, PhoneIconFont(), rodakos_theme_text_tertiary());
    lv_obj_center(orb_icon_);

    name_label_ = CreateText(hero_, "", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_size(name_label_, 164, 20);
    lv_label_set_long_mode(name_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(name_label_, 68, 10);

    status_dot_ = CreateDot(hero_);
    lv_obj_set_pos(status_dot_, 68, 41);
    status_label_ = CreateText(hero_, "", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_size(status_label_, 150, 20);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(status_label_, 82, 36);

    power_switch_ = CreateSwitch(hero_);
    lv_obj_align(power_switch_, LV_ALIGN_RIGHT_MID, -12, 0);

    auto* brightness = CreatePanel(control_, kBrightnessY, 52);
    brightness_icon_ = CreateText(brightness, ICON_BRIGHTNESS, PhoneIconFont(), rodakos_theme_primary());
    lv_obj_set_pos(brightness_icon_, 12, 4);
    auto* brightness_title = CreateText(brightness, "亮度", &phone_font_14, rodakos_theme_text_secondary());
    lv_obj_set_pos(brightness_title, 38, 4);
    brightness_value_ = CreateText(brightness, "--%", &phone_font_18, rodakos_theme_text_primary());
    lv_obj_align(brightness_value_, LV_ALIGN_TOP_RIGHT, -12, 4);

    brightness_slider_ = lv_slider_create(brightness);
    lv_obj_set_size(brightness_slider_, 268, 8);
    lv_obj_set_pos(brightness_slider_, 16, 34);
    lv_slider_set_range(brightness_slider_, 1, 100);
    // 40 px tall touch band around the 8 px track.
    lv_obj_set_ext_click_area(brightness_slider_, 16);
    lv_obj_set_style_bg_color(brightness_slider_, rodakos_theme_bg_tertiary(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider_, rodakos_theme_primary(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider_, rodakos_theme_primary(), LV_PART_KNOB);
    lv_obj_set_style_border_color(brightness_slider_, rodakos_theme_bg_secondary(), LV_PART_KNOB);
    lv_obj_set_style_border_width(brightness_slider_, 3, LV_PART_KNOB);
    lv_obj_set_style_pad_all(brightness_slider_, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(brightness_slider_, [](lv_event_t* e) {
        auto* self = static_cast<SmartApp*>(lv_event_get_user_data(e));
        self->PreviewBrightness(lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(e))));
    }, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(brightness_slider_, [](lv_event_t* e) {
        static_cast<SmartApp*>(lv_event_get_user_data(e))->CommitBrightness();
    }, LV_EVENT_RELEASED, this);

    auto* color = CreatePanel(control_, kColorY, 60);
    auto* color_title = CreateText(color, "颜色", &phone_font_14, rodakos_theme_text_secondary());
    lv_obj_set_pos(color_title, 12, 2);
    color_name_label_ = CreateText(color, "", &phone_font_14, rodakos_theme_text_tertiary());
    lv_obj_align(color_name_label_, LV_ALIGN_TOP_RIGHT, -12, 2);
    for (size_t i = 0; i < kPresetCount; ++i) {
        auto* swatch = CreateBox(color, static_cast<lv_coord_t>(12 + i * 47), 24, 40, 30, 8);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(Hex(kPresets[i].color)), 0);
        lv_obj_set_style_border_color(swatch, rodakos_theme_text_primary(), 0);
        lv_obj_set_style_border_width(swatch, 0, 0);
        lv_obj_add_flag(swatch, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(swatch, 3);
        lv_obj_set_user_data(swatch, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(swatch, [](lv_event_t* e) {
            auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            static_cast<SmartApp*>(lv_event_get_user_data(e))->SelectPreset(
                static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target))));
        }, LV_EVENT_CLICKED, this);
        auto* check = CreateText(swatch, ICON_CHECK, PhoneIconFont(),
                                 rodakos_theme_text_on(Hex(kPresets[i].color)));
        lv_obj_center(check);
        lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);
        presets_[i] = swatch;
        preset_checks_[i] = check;
    }
}

void SmartApp::CreateOverviewView() {
    overview_ = lv_obj_create(root_);
    lv_obj_remove_style_all(overview_);
    lv_obj_set_size(overview_, 320, 240);
    lv_obj_clear_flag(overview_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    for (size_t i = 0; i < kMaxTiles; ++i) {
        Tile& tile = tiles_[i];
        tile.card = CreateBox(overview_, static_cast<lv_coord_t>(10 + (i % 2) * 154),
                              static_cast<lv_coord_t>(48 + (i / 2) * 96), 146, 88, 10);
        lv_obj_set_style_bg_color(tile.card, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_bg_color(tile.card, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
        lv_obj_add_flag(tile.card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(tile.card, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(tile.card, [](lv_event_t* e) {
            auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            static_cast<SmartApp*>(lv_event_get_user_data(e))->OpenLight(
                static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target))));
        }, LV_EVENT_CLICKED, this);

        tile.orb = CreateBox(tile.card, 10, 10, 32, 32, LV_RADIUS_CIRCLE);
        lv_obj_clear_flag(tile.orb, LV_OBJ_FLAG_CLICKABLE);
        tile.orb_icon = CreateText(tile.orb, ICON_LIGHT, PhoneIconFont(), rodakos_theme_text_tertiary());
        lv_obj_center(tile.orb_icon);

        // Separate 56x40 hit area for the quick power toggle so it does not open the detail page.
        tile.power = CreateBox(tile.card, 86, 6, 56, 40, 8);
        lv_obj_set_style_bg_opa(tile.power, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(tile.power, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(tile.power, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(tile.power, [](lv_event_t* e) {
            auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            static_cast<SmartApp*>(lv_event_get_user_data(e))->ToggleTile(
                static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target))));
            lv_event_stop_bubbling(e);
        }, LV_EVENT_CLICKED, this);
        auto* sw = CreateSwitch(tile.power);
        lv_obj_set_size(sw, 40, 22);
        lv_obj_center(sw);

        tile.name = CreateText(tile.card, "", &phone_font_14, rodakos_theme_text_primary());
        lv_obj_set_size(tile.name, 126, 20);
        lv_label_set_long_mode(tile.name, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(tile.name, 10, 46);
        tile.dot = CreateDot(tile.card);
        lv_obj_set_pos(tile.dot, 10, 73);
        tile.status = CreateText(tile.card, "", &phone_font_12, rodakos_theme_text_secondary());
        lv_obj_set_size(tile.status, 110, 20);
        lv_label_set_long_mode(tile.status, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(tile.status, 24, 68);
    }
}

void SmartApp::CreateEmptyView() {
    empty_ = lv_obj_create(root_);
    lv_obj_remove_style_all(empty_);
    lv_obj_set_size(empty_, 320, 200);
    lv_obj_set_pos(empty_, 0, 40);
    lv_obj_clear_flag(empty_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    auto* icon = CreateText(empty_, ICON_LIGHT, PhoneIconFontLarge(), rodakos_theme_text_tertiary());
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 42);
    auto* title = CreateText(empty_, "没有可用灯光", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 82);
    empty_hint_ = CreateText(empty_, "当前设备未检测到板载灯光", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_align(empty_hint_, LV_ALIGN_TOP_MID, 0, 106);
}

void SmartApp::ShowView(View view) {
    view_ = view;
    if (root_ == nullptr) return;
    auto show = [](lv_obj_t* obj, bool visible) {
        if (obj == nullptr) return;
        if (visible) {
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    };
    show(control_, view == View::kControl);
    show(overview_, view == View::kOverview);
    show(empty_, view == View::kEmpty);
    Refresh();
}

void SmartApp::Refresh() {
    if (root_ == nullptr || paused_) return;
    const size_t count = lights_ != nullptr ? lights_->ListLights().size() : 0;
    if (count == 0 && view_ != View::kEmpty) {
        ShowView(View::kEmpty);
        return;
    }
    if (count > 0 && view_ == View::kEmpty) {
        ShowView(count == 1 ? View::kControl : View::kOverview);
        return;
    }
    if (current_index_ >= count) current_index_ = 0;
    if (view_ == View::kControl) {
        RefreshControl();
    } else if (view_ == View::kOverview) {
        RefreshOverview();
    } else {
        lv_label_set_text(title_label_, "智能家居");
    }
}

bool SmartApp::CurrentLight(rodakos::LightState& state) const {
    return lights_ != nullptr && lights_->GetLight(current_index_, state);
}

void SmartApp::RefreshControl() {
    rodakos::LightState state;
    if (!CurrentLight(state)) return;
    const size_t count = lights_->ListLights().size();
    lv_label_set_text(title_label_, count > 1 ? DisplayName(state) : "智能家居");
    lv_label_set_text(name_label_, DisplayName(state));

    const bool lit = state.enabled && state.available;
    const bool overlay = lights_->HasOverlay(current_index_);
    const uint32_t hex = Hex(state.color);
    lv_obj_set_style_bg_color(orb_, lit ? lv_color_hex(hex) : rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(orb_icon_, lit ? rodakos_theme_text_on(hex) : rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_border_color(orb_, rodakos_theme_primary(), 0);
    lv_obj_set_style_border_width(orb_, overlay ? 2 : 0, 0);
    SetChecked(power_switch_, state.enabled);

    if (!state.available) {
        StyleDot(status_dot_, true, rodakos_theme_error());
        lv_label_set_text(status_label_, "驱动异常，点按重试");
        lv_obj_set_style_text_color(status_label_, rodakos_theme_error(), 0);
    } else if (overlay) {
        StyleDot(status_dot_, true, rodakos_theme_primary());
        lv_label_set_text(status_label_, "语音状态灯使用中");
        lv_obj_set_style_text_color(status_label_, rodakos_theme_primary(), 0);
    } else if (state.enabled) {
        StyleDot(status_dot_, true, rodakos_theme_success());
        lv_label_set_text_fmt(status_label_, "已开启 · %u%%", static_cast<unsigned>(state.brightness_percent));
        lv_obj_set_style_text_color(status_label_, rodakos_theme_text_secondary(), 0);
    } else {
        StyleDot(status_dot_, false, rodakos_theme_text_tertiary());
        lv_label_set_text(status_label_, "已关闭");
        lv_obj_set_style_text_color(status_label_, rodakos_theme_text_secondary(), 0);
    }

    const lv_color_t accent = lit ? lv_color_hex(hex) : rodakos_theme_text_tertiary();
    lv_obj_set_style_text_color(brightness_icon_, lit ? rodakos_theme_primary() : rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_bg_color(brightness_slider_, accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider_, accent, LV_PART_KNOB);
    if (!dragging_) {
        lv_slider_set_value(brightness_slider_, state.brightness_percent, LV_ANIM_OFF);
        lv_label_set_text_fmt(brightness_value_, "%u%%", static_cast<unsigned>(state.brightness_percent));
    }
    lv_obj_set_style_text_color(brightness_value_, lit ? rodakos_theme_text_primary() : rodakos_theme_text_tertiary(), 0);

    lv_label_set_text(color_name_label_, PresetName(state.color));
    for (size_t i = 0; i < kPresetCount; ++i) {
        const bool selected = SameColor(kPresets[i].color, state.color);
        lv_obj_set_style_border_width(presets_[i], selected ? 2 : 0, 0);
        lv_obj_set_style_bg_opa(presets_[i], lit ? LV_OPA_COVER : LV_OPA_70, 0);
        if (selected) {
            lv_obj_clear_flag(preset_checks_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(preset_checks_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void SmartApp::RefreshOverview() {
    const auto lights = lights_->ListLights();
    lv_label_set_text_fmt(title_label_, "智能家居 · %u", static_cast<unsigned>(lights.size()));
    for (size_t i = 0; i < kMaxTiles; ++i) {
        Tile& tile = tiles_[i];
        if (i >= lights.size()) {
            lv_obj_add_flag(tile.card, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(tile.card, LV_OBJ_FLAG_HIDDEN);
        const auto& state = lights[i];
        const bool lit = state.enabled && state.available;
        const uint32_t hex = Hex(state.color);
        lv_obj_set_style_bg_color(tile.orb, lit ? lv_color_hex(hex) : rodakos_theme_bg_tertiary(), 0);
        lv_obj_set_style_text_color(tile.orb_icon, lit ? rodakos_theme_text_on(hex) : rodakos_theme_text_tertiary(), 0);
        SetChecked(lv_obj_get_child(tile.power, 0), state.enabled);
        lv_label_set_text(tile.name, DisplayName(state));
        if (!state.available) {
            StyleDot(tile.dot, true, rodakos_theme_error());
            lv_label_set_text(tile.status, "驱动异常");
        } else if (state.enabled) {
            StyleDot(tile.dot, true, rodakos_theme_success());
            lv_label_set_text_fmt(tile.status, "已开启 · %u%%", static_cast<unsigned>(state.brightness_percent));
        } else {
            StyleDot(tile.dot, false, rodakos_theme_text_tertiary());
            lv_label_set_text(tile.status, "已关闭");
        }
    }
}

void SmartApp::OpenLight(size_t index) {
    if (lights_ == nullptr || index >= lights_->ListLights().size()) return;
    current_index_ = index;
    ShowView(View::kControl);
}

void SmartApp::Apply(const std::string& id, const rodakos::LightPatch& patch) {
    if (lights_ == nullptr) return;
    const auto result = lights_->ApplyLightPatch(id, patch);
    if (!result.accepted) {
        ESP_LOGW(TAG, "Light patch on '%s' rejected: %s", id.c_str(), result.error_code.c_str());
        // The service keeps the last accepted configuration; Refresh shows that, not the attempt.
        ui_->ShowToastUnlocked("灯光状态未确认");
    }
    Refresh();
}

void SmartApp::TogglePower() {
    rodakos::LightState state;
    if (!CurrentLight(state)) return;
    rodakos::LightPatch patch;
    patch.enabled = !state.enabled;
    Apply(state.id, patch);
}

void SmartApp::ToggleTile(size_t index) {
    rodakos::LightState state;
    if (lights_ == nullptr || !lights_->GetLight(index, state)) return;
    rodakos::LightPatch patch;
    patch.enabled = !state.enabled;
    Apply(state.id, patch);
}

void SmartApp::PreviewBrightness(int32_t value) {
    if (!dragging_) {
        rodakos::LightState state;
        if (!CurrentLight(state)) return;
        dragging_ = true;
        drag_light_id_ = state.id;
    }
    lv_label_set_text_fmt(brightness_value_, "%d%%", static_cast<int>(value));
}

void SmartApp::CommitBrightness() {
    if (!dragging_) return;
    const std::string id = drag_light_id_;
    dragging_ = false;
    drag_light_id_.clear();
    rodakos::LightPatch patch;
    patch.brightness_percent = static_cast<int>(lv_slider_get_value(brightness_slider_));
    Apply(id, patch);
}

void SmartApp::SelectPreset(size_t preset) {
    rodakos::LightState state;
    if (preset >= kPresetCount || !CurrentLight(state)) return;
    // Colour only: choosing a colour while the light is off saves it for the next power-on
    // instead of switching the light on (LightService::SetColor would also enable it).
    rodakos::LightPatch patch;
    patch.color = kPresets[preset].color;
    Apply(state.id, patch);
    if (!state.enabled) ui_->ShowToastUnlocked("已保存颜色，开灯后生效");
}

void SmartApp::NavigateBack() {
    if (OnBackRequested()) return;
    if (context_ != nullptr) {
        lv_async_call_cancel(DeferReturnHome, context_);
        lv_async_call(DeferReturnHome, context_);
    }
}

void RegisterSmartApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "smart",
        .title = "Smart",
        .icon = ICON_LIGHT,
        .category = PhoneAppCategory::kTools,
        .icon_tint = PhoneAppIconTint::kCool,
        .capabilities = PhoneCapability::kNone,
        .show_on_home = true,
        .aliases = {"light", "rgb", "lamp", "smart home", "灯", "灯光", "智能"},
        .create = []() { return std::make_unique<SmartApp>(); },
    });
}

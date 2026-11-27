#include "apps/gyro/gyro_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
constexpr const char* TAG = "GyroApp";
constexpr uint32_t kRefreshPeriodMs = 100;
constexpr float kPi = 3.14159265358979323846f;
constexpr lv_coord_t kDialX = 10;
constexpr lv_coord_t kDialY = 48;
// Bubble travel radius: ±45° maps to the dashed ring.
constexpr float kBubbleTravel = 48.0f;
constexpr float kMaxTiltDeg = 45.0f;
// Bar full scale on the six-axis page.
constexpr float kGyroBarScaleDps = 120.0f;
constexpr float kAccelBarScaleG = 2.0f;

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) context->navigation().ReturnHome();
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

lv_obj_t* CreateCard(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    auto* card = CreatePlain(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    return card;
}

lv_obj_t* CreateDivider(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    auto* line = CreatePlain(parent);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, w, h);
    lv_obj_set_style_bg_color(line, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    return line;
}

lv_obj_t* CreateTextButton(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                           const char* text, lv_obj_t** label_out) {
    auto* button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    auto* label = CreateText(button, text, &phone_font_14, rodakos_theme_text_primary());
    lv_obj_center(label);
    if (label_out != nullptr) *label_out = label;
    return button;
}

void SetHidden(lv_obj_t* obj, bool hidden) {
    if (obj == nullptr) return;
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

float WrapDegrees(float value) {
    while (value > 180.0f) value -= 360.0f;
    while (value < -180.0f) value += 360.0f;
    return value;
}

float Magnitude(const rodakos::MotionVector& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

// "+12.3°" with a real minus sign; the sign keeps the column from jumping.
void FormatAngle(char* buffer, size_t size, float value, int decimals) {
    const float shown = std::fabs(value) < (decimals == 0 ? 0.5f : 0.05f) ? 0.0f : value;
    std::snprintf(buffer, size, decimals == 0 ? "%+.0f°" : "%+.1f°", static_cast<double>(shown));
}

void DrawCircle(lv_layer_t* layer, float cx, float cy, float r, lv_color_t fill, lv_opa_t fill_opa,
                lv_color_t border, int border_width, lv_opa_t border_opa = LV_OPA_COVER) {
    lv_area_t area = {
        .x1 = static_cast<int32_t>(std::lround(cx - r)),
        .y1 = static_cast<int32_t>(std::lround(cy - r)),
        .x2 = static_cast<int32_t>(std::lround(cx + r)),
        .y2 = static_cast<int32_t>(std::lround(cy + r)),
    };
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.bg_color = fill;
    dsc.bg_opa = fill_opa;
    dsc.border_color = border;
    dsc.border_width = border_width;
    dsc.border_opa = border_width > 0 ? border_opa : LV_OPA_TRANSP;
    dsc.shadow_opa = LV_OPA_TRANSP;
    dsc.outline_opa = LV_OPA_TRANSP;
    lv_draw_rect(layer, &dsc, &area);
}

void DrawLine(lv_layer_t* layer, float x1, float y1, float x2, float y2, int width, lv_color_t color,
              lv_opa_t opa, bool round = true) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1 = lv_point_precise_t{static_cast<lv_value_precise_t>(x1), static_cast<lv_value_precise_t>(y1)};
    dsc.p2 = lv_point_precise_t{static_cast<lv_value_precise_t>(x2), static_cast<lv_value_precise_t>(y2)};
    dsc.color = color;
    dsc.width = width;
    dsc.opa = opa;
    dsc.round_start = round ? 1 : 0;
    dsc.round_end = round ? 1 : 0;
    lv_draw_line(layer, &dsc);
}

void DrawTriangle(lv_layer_t* layer, float x1, float y1, float x2, float y2, float x3, float y3,
                  lv_color_t color) {
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.p[0] = lv_point_precise_t{static_cast<lv_value_precise_t>(x1), static_cast<lv_value_precise_t>(y1)};
    dsc.p[1] = lv_point_precise_t{static_cast<lv_value_precise_t>(x2), static_cast<lv_value_precise_t>(y2)};
    dsc.p[2] = lv_point_precise_t{static_cast<lv_value_precise_t>(x3), static_cast<lv_value_precise_t>(y3)};
    dsc.color = color;
    dsc.opa = LV_OPA_COVER;
    lv_draw_triangle(layer, &dsc);
}

}  // namespace

void GyroApp::RefreshTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<GyroApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) self->Refresh();
}

bool GyroApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    motion_ = context.services().motion();
    motion_started_ = motion_ != nullptr && motion_->Start();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return false;

    CreateUi();
    Refresh();
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, kRefreshPeriodMs, this);
    ESP_LOGI(TAG, "Gyro app created, motion=%d", motion_started_ ? 1 : 0);
    return true;
}

void GyroApp::OnResume() {
    if (refresh_timer_ != nullptr) {
        lv_timer_resume(refresh_timer_);
        lv_timer_reset(refresh_timer_);
    }
}

void GyroApp::OnPause() {
    if (refresh_timer_ != nullptr) lv_timer_pause(refresh_timer_);
}

void GyroApp::OnDestroy() {
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
    if (motion_ != nullptr && motion_started_) motion_->Stop();
    context_ = nullptr;
    ui_ = nullptr;
    motion_ = nullptr;
    motion_started_ = false;
}

bool GyroApp::OnThemeChanged(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return false;
    const bool hidden = root_ != nullptr && lv_obj_is_valid(root_) && lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    DestroyUi();
    CreateUi();
    Refresh();
    if (hidden && root_ != nullptr) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    return true;
}

bool GyroApp::OnBackRequested() {
    if (view_ == View::kAxes) {
        ShowView(View::kLevel);
        Refresh();
        return true;
    }
    NavigateHome();
    return true;
}

void GyroApp::CreateUi() {
    root_ = CreatePlain(ui_->screen());
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);

    CreateAppHeader(root_, "陀螺仪", [](lv_event_t* e) {
        static_cast<GyroApp*>(lv_event_get_user_data(e))->OnBackRequested();
    }, [](lv_event_t* e) {
        auto* self = static_cast<GyroApp*>(lv_event_get_user_data(e));
        if (self->view_ == View::kLevel) {
            self->ShowView(View::kAxes);
            self->Refresh();
        } else if (self->view_ == View::kAxes) {
            self->ToggleFreeze();
        }
    }, this, &title_label_, ICON_CHIP, &action_button_);
    action_icon_ = lv_obj_get_child(action_button_, 0);

    CreateLevelView();
    CreateAxesView();
    CreateErrorView();
    ShowView(motion_started_ ? data_view_ : View::kError);
}

void GyroApp::DestroyUi() {
    if (root_ != nullptr && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    ReleaseAttitudeCanvas();
    root_ = title_label_ = action_button_ = action_icon_ = nullptr;
    level_view_ = attitude_canvas_ = state_dot_ = state_label_ = relative_tag_ = nullptr;
    angle_values_.fill(nullptr);
    zero_button_ = zero_label_ = freeze_button_ = freeze_label_ = summary_label_ = nullptr;
    axes_view_ = axes_info_label_ = nullptr;
    gyro_bars_.fill(AxisBar{});
    accel_bars_.fill(AxisBar{});
    error_view_ = error_hint_label_ = nullptr;
}

void GyroApp::CreateLevelView() {
    level_view_ = CreatePlain(root_);
    lv_obj_set_size(level_view_, 320, 240);

    CreateAttitudeCanvas(level_view_);

    summary_label_ = CreateText(level_view_, "", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_width(summary_label_, kCanvasSize);
    lv_obj_set_style_text_align(summary_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(summary_label_, kDialX, 212);

    state_dot_ = CreatePlain(level_view_);
    lv_obj_set_size(state_dot_, 8, 8);
    lv_obj_set_pos(state_dot_, 176, 55);
    lv_obj_set_style_radius(state_dot_, LV_RADIUS_CIRCLE, 0);
    state_label_ = CreateText(level_view_, "", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_pos(state_label_, 190, 49);

    relative_tag_ = CreatePlain(level_view_);
    lv_obj_set_size(relative_tag_, LV_SIZE_CONTENT, 18);
    lv_obj_set_style_pad_hor(relative_tag_, 6, 0);
    lv_obj_set_style_radius(relative_tag_, 4, 0);
    lv_obj_set_style_bg_color(relative_tag_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(relative_tag_, LV_OPA_COVER, 0);
    auto* tag_text = CreateText(relative_tag_, "相对", &phone_font_12, rodakos_theme_primary());
    lv_obj_center(tag_text);
    lv_obj_align(relative_tag_, LV_ALIGN_TOP_RIGHT, -10, 50);

    auto* card = CreateCard(level_view_, 174, 76, 136, 108);
    const char* keys[] = {"横滚", "俯仰", "偏航"};
    for (int i = 0; i < 3; ++i) {
        if (i > 0) CreateDivider(card, 0, i * 36, 136, 1);
        auto* key = CreateText(card, keys[i], &phone_font_14, rodakos_theme_text_secondary());
        lv_obj_set_pos(key, 10, i * 36 + 8);
        angle_values_[i] = CreateText(card, "", &phone_font_18, rodakos_theme_text_primary());
        lv_obj_align(angle_values_[i], LV_ALIGN_TOP_RIGHT, -10, i * 36 + 7);
    }

    zero_button_ = CreateTextButton(level_view_, 174, 192, 65, 38, "归零", &zero_label_);
    lv_obj_add_event_cb(zero_button_, [](lv_event_t* e) {
        static_cast<GyroApp*>(lv_event_get_user_data(e))->ToggleZero();
    }, LV_EVENT_CLICKED, this);
    freeze_button_ = CreateTextButton(level_view_, 245, 192, 65, 38, "定格", &freeze_label_);
    lv_obj_add_event_cb(freeze_button_, [](lv_event_t* e) {
        static_cast<GyroApp*>(lv_event_get_user_data(e))->ToggleFreeze();
    }, LV_EVENT_CLICKED, this);
}

void GyroApp::CreateAxesView() {
    axes_view_ = CreatePlain(root_);
    lv_obj_set_size(axes_view_, 320, 240);

    axes_info_label_ = CreateText(axes_view_, "", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_width(axes_info_label_, 296);
    lv_label_set_long_mode(axes_info_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(axes_info_label_, 12, 46);

    auto make_group = [&](lv_coord_t y, const char* title, const char* unit, std::array<AxisBar, 3>& bars) {
        auto* group = CreateCard(axes_view_, 10, y, 300, 80);
        auto* heading = CreateText(group, title, &phone_font_14, rodakos_theme_text_primary());
        lv_obj_set_pos(heading, 12, 4);
        auto* unit_label = CreateText(group, unit, &phone_font_12, rodakos_theme_text_tertiary());
        lv_obj_align(unit_label, LV_ALIGN_TOP_RIGHT, -12, 6);
        CreateDivider(group, 0, 28, 300, 1);
        const char* axes[] = {"X", "Y", "Z"};
        for (int i = 0; i < 3; ++i) {
            const lv_coord_t x = static_cast<lv_coord_t>(i * 100);
            if (i > 0) CreateDivider(group, x, 29, 1, 51);
            auto* axis = CreateText(group, axes[i], &phone_font_12, rodakos_theme_primary());
            lv_obj_set_pos(axis, x + 8, 36);
            bars[static_cast<size_t>(i)].value = CreateText(group, "", &phone_font_14, rodakos_theme_text_primary());
            lv_obj_align(bars[static_cast<size_t>(i)].value, LV_ALIGN_TOP_RIGHT, -(200 - x) - 8, 35);
            auto* track = CreatePlain(group);
            lv_obj_set_pos(track, x + 8, 62);
            lv_obj_set_size(track, 84, 6);
            lv_obj_set_style_radius(track, 3, 0);
            lv_obj_set_style_bg_color(track, rodakos_theme_bg_tertiary(), 0);
            lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
            auto* mid = CreatePlain(track);
            lv_obj_set_pos(mid, 41, 0);
            lv_obj_set_size(mid, 2, 6);
            lv_obj_set_style_bg_color(mid, rodakos_theme_border(), 0);
            lv_obj_set_style_bg_opa(mid, LV_OPA_COVER, 0);
            bars[static_cast<size_t>(i)].fill = CreatePlain(track);
            lv_obj_set_height(bars[static_cast<size_t>(i)].fill, 6);
            lv_obj_set_style_radius(bars[static_cast<size_t>(i)].fill, 3, 0);
            lv_obj_set_style_bg_opa(bars[static_cast<size_t>(i)].fill, LV_OPA_COVER, 0);
        }
    };
    make_group(68, "角速度", "°/s", gyro_bars_);
    make_group(154, "加速度", "g", accel_bars_);
}

void GyroApp::CreateErrorView() {
    error_view_ = CreatePlain(root_);
    lv_obj_set_size(error_view_, 320, 200);
    lv_obj_set_pos(error_view_, 0, kRodakosAppHeaderHeight);
    auto* icon = CreateText(error_view_, ICON_COMPASS, PhoneIconFontLarge(), rodakos_theme_text_tertiary());
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 26);
    auto* title = CreateText(error_view_, "传感器不可用", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 66);
    error_hint_label_ = CreateText(error_view_, "", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_width(error_hint_label_, 288);
    lv_label_set_long_mode(error_hint_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(error_hint_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(error_hint_label_, LV_ALIGN_TOP_MID, 0, 90);

    auto* retry = lv_btn_create(error_view_);
    lv_obj_remove_style_all(retry);
    lv_obj_set_size(retry, 120, 38);
    lv_obj_align(retry, LV_ALIGN_TOP_MID, 0, 122);
    lv_obj_set_style_radius(retry, 8, 0);
    lv_obj_set_style_bg_color(retry, rodakos_theme_primary(), 0);
    lv_obj_set_style_bg_opa(retry, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(retry, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(retry, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(retry, 6, 0);
    CreateText(retry, ICON_REFRESH, PhoneIconFont(), rodakos_theme_on_primary());
    CreateText(retry, "重试", &phone_font_14, rodakos_theme_on_primary());
    lv_obj_add_event_cb(retry, [](lv_event_t* e) {
        static_cast<GyroApp*>(lv_event_get_user_data(e))->Retry();
    }, LV_EVENT_CLICKED, this);
}

void GyroApp::ShowView(View view) {
    view_ = view;
    if (view != View::kError) data_view_ = view;
    SetHidden(level_view_, view != View::kLevel);
    SetHidden(axes_view_, view != View::kAxes);
    SetHidden(error_view_, view != View::kError);
    if (title_label_ != nullptr) lv_label_set_text(title_label_, view == View::kAxes ? "六轴数据" : "陀螺仪");
    SetHidden(action_button_, view == View::kError);
    UpdateButtons();
}

void GyroApp::UpdateButtons() {
    if (zero_button_ == nullptr) return;
    lv_label_set_text(zero_label_, zeroed_ ? "复位" : "归零");
    lv_obj_set_style_bg_color(zero_button_, zeroed_ ? rodakos_theme_bg_secondary() : rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_border_color(zero_button_, rodakos_theme_primary(), 0);
    lv_obj_set_style_border_width(zero_button_, zeroed_ ? 1 : 0, 0);
    lv_obj_set_style_text_color(zero_label_, zeroed_ ? rodakos_theme_primary() : rodakos_theme_text_primary(), 0);

    lv_label_set_text(freeze_label_, frozen_ ? "继续" : "定格");
    lv_obj_set_style_bg_color(freeze_button_, frozen_ ? rodakos_theme_primary() : rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(freeze_label_, frozen_ ? rodakos_theme_on_primary() : rodakos_theme_text_primary(), 0);
    SetHidden(relative_tag_, !zeroed_);

    if (action_icon_ != nullptr) {
        const bool axes = view_ == View::kAxes;
        lv_label_set_text(action_icon_, axes ? (frozen_ ? ICON_PLAY : ICON_PAUSE) : ICON_CHIP);
        const bool highlight = axes && frozen_;
        lv_obj_set_style_bg_color(action_button_, highlight ? rodakos_theme_primary() : rodakos_theme_bg_tertiary(), 0);
        lv_obj_set_style_text_color(action_icon_, highlight ? rodakos_theme_on_primary() : rodakos_theme_text_primary(), 0);
    }
}

bool GyroApp::CreateAttitudeCanvas(lv_obj_t* parent) {
    // RGB565 is the panel's native format: no alpha to blend, 47 KB in PSRAM for 154x154.
    const uint32_t stride = LV_DRAW_BUF_STRIDE(kCanvasSize, LV_COLOR_FORMAT_RGB565);
    const uint32_t size = stride * kCanvasSize;
    attitude_canvas_buffer_ = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (attitude_canvas_buffer_ == nullptr) {
        ESP_LOGW(TAG, "Failed to allocate attitude canvas buffer");
        return false;
    }
    lv_draw_buf_init(&attitude_draw_buf_, kCanvasSize, kCanvasSize, LV_COLOR_FORMAT_RGB565, stride,
                     attitude_canvas_buffer_, size);
    lv_draw_buf_set_flag(&attitude_draw_buf_, LV_IMAGE_FLAGS_MODIFIABLE);
    attitude_canvas_ = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(attitude_canvas_, &attitude_draw_buf_);
    lv_obj_set_pos(attitude_canvas_, kDialX, kDialY);
    lv_obj_clear_flag(attitude_canvas_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return true;
}

void GyroApp::ReleaseAttitudeCanvas() {
    if (attitude_canvas_buffer_ != nullptr) {
        heap_caps_free(attitude_canvas_buffer_);
        attitude_canvas_buffer_ = nullptr;
    }
    std::memset(&attitude_draw_buf_, 0, sizeof(attitude_draw_buf_));
}

void GyroApp::DrawAttitude(float roll, float pitch, float yaw, bool level) {
    if (attitude_canvas_ == nullptr) return;
    // The canvas has no alpha: paint the page colour first so the dial's corners match it.
    lv_canvas_fill_bg(attitude_canvas_, rodakos_theme_bg_primary(), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(attitude_canvas_, &layer);

    constexpr float c = kCanvasSize / 2.0f - 0.5f;
    const lv_color_t accent = level ? rodakos_theme_success() : rodakos_theme_primary();
    DrawCircle(&layer, c, c, 74, rodakos_theme_bg_secondary(), LV_OPA_COVER,
               zeroed_ ? rodakos_theme_primary() : rodakos_theme_border(), 2);
    // Dashed travel ring: short arcs every 12°.
    for (int a = 0; a < 360; a += 12) {
        const float r1 = static_cast<float>(a) * kPi / 180.0f;
        const float r2 = static_cast<float>(a + 5) * kPi / 180.0f;
        DrawLine(&layer, c + std::cos(r1) * kBubbleTravel, c + std::sin(r1) * kBubbleTravel,
                 c + std::cos(r2) * kBubbleTravel, c + std::sin(r2) * kBubbleTravel, 1,
                 rodakos_theme_bg_tertiary(), LV_OPA_COVER, false);
    }
    DrawLine(&layer, 16, c, 138, c, 1, rodakos_theme_bg_tertiary(), LV_OPA_COVER, false);
    DrawLine(&layer, c, 16, c, 138, 1, rodakos_theme_bg_tertiary(), LV_OPA_COVER, false);

    const float roll_c = std::clamp(roll, -kMaxTiltDeg, kMaxTiltDeg);
    const float pitch_c = std::clamp(pitch, -kMaxTiltDeg, kMaxTiltDeg);
    const float roll_rad = roll_c * kPi / 180.0f;
    DrawLine(&layer, c - std::cos(roll_rad) * 44, c - std::sin(roll_rad) * 44,
             c + std::cos(roll_rad) * 44, c + std::sin(roll_rad) * 44, 2, accent, LV_OPA_40);
    DrawCircle(&layer, c, c, 18, rodakos_theme_bg_secondary(), LV_OPA_TRANSP,
               level ? rodakos_theme_success() : rodakos_theme_border(), 2);

    // Yaw marker on the outer ring; 0° points up.
    const float yaw_rad = (yaw - 90.0f) * kPi / 180.0f;
    DrawTriangle(&layer, c + std::cos(yaw_rad) * 69, c + std::sin(yaw_rad) * 69,
                 c + std::cos(yaw_rad - 0.1f) * 61, c + std::sin(yaw_rad - 0.1f) * 61,
                 c + std::cos(yaw_rad + 0.1f) * 61, c + std::sin(yaw_rad + 0.1f) * 61,
                 rodakos_theme_secondary());

    float bx = (roll_c / kMaxTiltDeg) * kBubbleTravel;
    float by = (pitch_c / kMaxTiltDeg) * kBubbleTravel;
    const float dist = std::sqrt(bx * bx + by * by);
    if (dist > kBubbleTravel) {
        bx *= kBubbleTravel / dist;
        by *= kBubbleTravel / dist;
    }
    DrawCircle(&layer, c + bx, c + by, 13, accent, LV_OPA_COVER, accent, 0);
    DrawCircle(&layer, c + bx, c + by, 3, rodakos_theme_bg_primary(), LV_OPA_COVER, accent, 0);
    DrawCircle(&layer, c, c, 2, rodakos_theme_text_tertiary(), LV_OPA_COVER, accent, 0);

    lv_canvas_finish_layer(attitude_canvas_, &layer);
    lv_obj_set_style_image_opa(attitude_canvas_, frozen_ ? LV_OPA_80 : LV_OPA_COVER, 0);
    lv_obj_invalidate(attitude_canvas_);
}

void GyroApp::UpdateAxisBar(AxisBar& bar, float value, float full_scale, const char* format) {
    if (bar.fill == nullptr) return;
    char text[24];
    std::snprintf(text, sizeof(text), format, static_cast<double>(value));
    lv_label_set_text(bar.value, text);
    const float norm = std::clamp(value / full_scale, -1.0f, 1.0f);
    constexpr lv_coord_t kHalf = 42;
    const lv_coord_t width = std::max<lv_coord_t>(2, static_cast<lv_coord_t>(std::fabs(norm) * kHalf));
    lv_obj_set_width(bar.fill, width);
    lv_obj_set_x(bar.fill, norm >= 0 ? kHalf : kHalf - width);
    lv_obj_set_style_bg_color(bar.fill, std::fabs(norm) > 0.7f ? rodakos_theme_warning() : rodakos_theme_primary(), 0);
}

void GyroApp::Refresh() {
    if (root_ == nullptr) return;

    rodakos::MotionSample sample;
    bool ready = false;
    if (motion_ != nullptr && motion_started_) {
        ready = motion_->ReadSample(sample) && sample.status == rodakos::MotionStatus::kReady;
    }
    if (!ready && !(frozen_ && has_sample_)) {
        if (view_ != View::kError) ShowView(View::kError);
        // Never started: the sensor was not found. Started but not reading: a read failed.
        const char* reason = motion_ == nullptr ? "运动传感器服务不可用"
                           : !motion_started_ ? "未检测到 QMI8658 或读取失败"
                                              : "读取失败，请重试";
        lv_label_set_text(error_hint_label_, reason);
        return;
    }
    if (view_ == View::kError) ShowView(data_view_);
    if (ready && !frozen_) {
        last_sample_ = sample;
        has_sample_ = true;
    }
    const rodakos::MotionSample& shown = last_sample_;

    const float roll = shown.roll_deg - (zeroed_ ? zero_roll_ : 0.0f);
    const float pitch = shown.pitch_deg - (zeroed_ ? zero_pitch_ : 0.0f);
    const float yaw = WrapDegrees(shown.yaw_deg - (zeroed_ ? zero_yaw_ : 0.0f));
    const bool level = std::fabs(roll) <= kLevelToleranceDeg && std::fabs(pitch) <= kLevelToleranceDeg;
    const float gyro_mag = Magnitude(shown.gyro_dps);
    const float accel_mag = Magnitude(shown.accel_g);

    if (view_ == View::kLevel) {
        char text[40];
        FormatAngle(text, sizeof(text), roll, 1);
        lv_label_set_text(angle_values_[0], text);
        FormatAngle(text, sizeof(text), pitch, 1);
        lv_label_set_text(angle_values_[1], text);
        FormatAngle(text, sizeof(text), yaw, 0);
        lv_label_set_text(angle_values_[2], text);
        const lv_color_t value_color = level ? rodakos_theme_success() : rodakos_theme_text_primary();
        lv_obj_set_style_text_color(angle_values_[0], value_color, 0);
        lv_obj_set_style_text_color(angle_values_[1], value_color, 0);
        lv_obj_set_style_text_color(angle_values_[2], rodakos_theme_text_secondary(), 0);

        const lv_color_t state_color = frozen_ ? rodakos_theme_primary()
                                     : level ? rodakos_theme_success() : rodakos_theme_text_tertiary();
        lv_obj_set_style_bg_color(state_dot_, state_color, 0);
        lv_obj_set_style_bg_opa(state_dot_, frozen_ || level ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(state_dot_, state_color, 0);
        lv_obj_set_style_border_width(state_dot_, frozen_ || level ? 0 : 1, 0);
        lv_label_set_text(state_label_, frozen_ ? "已定格" : level ? "已水平" : "倾斜中");
        lv_obj_set_style_text_color(state_label_, frozen_ ? rodakos_theme_primary()
                                                 : level ? rodakos_theme_success() : rodakos_theme_text_primary(), 0);

        std::snprintf(text, sizeof(text), "ω %.1f °/s · %.2f g", static_cast<double>(gyro_mag),
                      static_cast<double>(accel_mag));
        lv_label_set_text(summary_label_, text);
        DrawAttitude(roll, pitch, yaw, level);
    } else if (view_ == View::kAxes) {
        char text[64];
        if (frozen_) {
            std::snprintf(text, sizeof(text), "已定格 · 点右上继续        |ω| %.1f °/s", static_cast<double>(gyro_mag));
        } else {
            const char* name = shown.sensor_name.empty() ? "QMI8658" : shown.sensor_name.c_str();
            std::snprintf(text, sizeof(text), "%s · 10 Hz        |ω| %.1f °/s", name, static_cast<double>(gyro_mag));
        }
        lv_label_set_text(axes_info_label_, text);
        lv_obj_set_style_text_color(axes_info_label_, frozen_ ? rodakos_theme_primary() : rodakos_theme_text_tertiary(), 0);
        UpdateAxisBar(gyro_bars_[0], shown.gyro_dps.x, kGyroBarScaleDps, "%+.1f");
        UpdateAxisBar(gyro_bars_[1], shown.gyro_dps.y, kGyroBarScaleDps, "%+.1f");
        UpdateAxisBar(gyro_bars_[2], shown.gyro_dps.z, kGyroBarScaleDps, "%+.1f");
        UpdateAxisBar(accel_bars_[0], shown.accel_g.x, kAccelBarScaleG, "%+.2f");
        UpdateAxisBar(accel_bars_[1], shown.accel_g.y, kAccelBarScaleG, "%+.2f");
        UpdateAxisBar(accel_bars_[2], shown.accel_g.z, kAccelBarScaleG, "%+.2f");
    }
}

void GyroApp::ToggleZero() {
    if (!has_sample_) return;
    zeroed_ = !zeroed_;
    if (zeroed_) {
        // The current attitude becomes 0°; yaw drift is cancelled from here on.
        zero_roll_ = last_sample_.roll_deg;
        zero_pitch_ = last_sample_.pitch_deg;
        zero_yaw_ = last_sample_.yaw_deg;
    }
    UpdateButtons();
    Refresh();
}

void GyroApp::ToggleFreeze() {
    if (!has_sample_ && !frozen_) return;
    frozen_ = !frozen_;
    UpdateButtons();
    Refresh();
}

void GyroApp::Retry() {
    if (motion_ == nullptr) motion_ = context_ != nullptr ? context_->services().motion() : nullptr;
    if (motion_ != nullptr && !motion_started_) motion_started_ = motion_->Start();
    frozen_ = false;
    UpdateButtons();
    Refresh();
    if (view_ == View::kError) ui_->ShowToastUnlocked("传感器仍不可用");
}

void GyroApp::NavigateHome() {
    if (context_ == nullptr) return;
    lv_async_call_cancel(DeferReturnHome, context_);
    lv_async_call(DeferReturnHome, context_);
}

void RegisterGyroApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "gyro",
        .title = "Gyro",
        .icon = ICON_COMPASS,
        .category = PhoneAppCategory::kTools,
        .capabilities = PhoneCapability::kMotion,
        .show_on_home = true,
        .aliases = {"gyro", "gyroscope", "imu", "motion", "orientation", "level", "陀螺仪", "水平仪"},
        .create = []() { return std::make_unique<GyroApp>(); },
    });
}

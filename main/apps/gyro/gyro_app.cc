#include "apps/gyro/gyro_app.h"

#include "phone_os/motion_service.h"
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
constexpr lv_coord_t kCanvasSize = 96;
constexpr uint32_t kRefreshPeriodMs = 100;
constexpr float kPi = 3.14159265358979323846f;

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
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

float ClampFloat(float value, float min_value, float max_value) {
    return std::max(min_value, std::min(max_value, value));
}

float Magnitude(const rodakos::MotionVector& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

void SetLabelFormatted(lv_obj_t* label, const char* fmt, float value) {
    if (label == nullptr) {
        return;
    }
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), fmt, value);
    lv_label_set_text(label, buffer);
}

void DrawCanvasRoundRect(lv_layer_t* layer,
                         int x1,
                         int y1,
                         int x2,
                         int y2,
                         int radius,
                         uint32_t color,
                         lv_opa_t opa) {
    lv_area_t area = {
        .x1 = static_cast<int16_t>(x1),
        .y1 = static_cast<int16_t>(y1),
        .x2 = static_cast<int16_t>(x2),
        .y2 = static_cast<int16_t>(y2),
    };
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.base.layer = layer;
    dsc.radius = radius;
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = opa;
    dsc.border_width = 0;
    dsc.shadow_opa = LV_OPA_TRANSP;
    dsc.outline_opa = LV_OPA_TRANSP;
    lv_draw_rect(layer, &dsc, &area);
}

void DrawCanvasLine(lv_layer_t* layer,
                    int x1,
                    int y1,
                    int x2,
                    int y2,
                    int width,
                    uint32_t color,
                    lv_opa_t opa) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.base.layer = layer;
    dsc.p1 = lv_point_precise_t{static_cast<lv_value_precise_t>(x1),
                                static_cast<lv_value_precise_t>(y1)};
    dsc.p2 = lv_point_precise_t{static_cast<lv_value_precise_t>(x2),
                                static_cast<lv_value_precise_t>(y2)};
    dsc.color = lv_color_hex(color);
    dsc.width = width;
    dsc.opa = opa;
    dsc.round_start = 1;
    dsc.round_end = 1;
    lv_draw_line(layer, &dsc);
}

void DrawCanvasTriangle(lv_layer_t* layer,
                        int x1,
                        int y1,
                        int x2,
                        int y2,
                        int x3,
                        int y3,
                        uint32_t color,
                        lv_opa_t opa) {
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.base.layer = layer;
    dsc.p[0] = lv_point_precise_t{static_cast<lv_value_precise_t>(x1),
                                  static_cast<lv_value_precise_t>(y1)};
    dsc.p[1] = lv_point_precise_t{static_cast<lv_value_precise_t>(x2),
                                  static_cast<lv_value_precise_t>(y2)};
    dsc.p[2] = lv_point_precise_t{static_cast<lv_value_precise_t>(x3),
                                  static_cast<lv_value_precise_t>(y3)};
    dsc.color = lv_color_hex(color);
    dsc.opa = opa;
    lv_draw_triangle(layer, &dsc);
}

}  // namespace

bool GyroApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    motion_ = context.services().motion();
    motion_started_ = motion_ != nullptr && motion_->Start();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

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
    if (refresh_timer_ != nullptr) {
        lv_timer_pause(refresh_timer_);
    }
}

void GyroApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            DestroyUi();
        }
    }

    if (motion_ != nullptr && motion_started_) {
        motion_->Stop();
    }

    context_ = nullptr;
    ui_ = nullptr;
    motion_ = nullptr;
    motion_started_ = false;
}

bool GyroApp::OnThemeChanged(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    motion_ = context.services().motion();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    const bool was_hidden = root_ != nullptr && lv_obj_is_valid(root_) &&
                            lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    DestroyUi();
    CreateUi();
    Refresh();
    if (was_hidden && root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, kRefreshPeriodMs, this);
    return true;
}

void GyroApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    CreateAppHeader(root_, "Gyro", [](lv_event_t* e) {
        auto* self = static_cast<GyroApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, [](lv_event_t* e) {
        auto* self = static_cast<GyroApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, this);

    status_label_ = CreateText(root_, "--", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_width(status_label_, 300);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(status_label_, 10, 43);

    CreateAttitudeCanvas();

    roll_value_label_ = CreateValuePanel("Roll", 10, 158, 0x17323E);
    pitch_value_label_ = CreateValuePanel("Pitch", 113, 158, 0x182E3E);
    yaw_value_label_ = CreateValuePanel("Yaw", 216, 158, 0x252E3F);

    gyro_x_label_ = CreateAxisLabel("GX --", 12, 204, 0x8BE0F2);
    gyro_y_label_ = CreateAxisLabel("GY --", 113, 204, 0xB4D887);
    gyro_z_label_ = CreateAxisLabel("GZ --", 214, 204, 0xF0B96E);
    acc_x_label_ = CreateAxisLabel("AX --", 12, 221, 0x90AFFF);
    acc_y_label_ = CreateAxisLabel("AY --", 113, 221, 0xD59EE8);
    acc_z_label_ = CreateAxisLabel("AZ --", 214, 221, 0xF08A8A);
}

void GyroApp::DestroyUi() {
    if (refresh_timer_ != nullptr) {
        lv_timer_delete(refresh_timer_);
        refresh_timer_ = nullptr;
    }
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
    ReleaseAttitudeCanvas();
    ResetUiPointers();
}

void GyroApp::ResetUiPointers() {
    root_ = nullptr;
    status_label_ = nullptr;
    attitude_canvas_ = nullptr;
    roll_value_label_ = nullptr;
    pitch_value_label_ = nullptr;
    yaw_value_label_ = nullptr;
    gyro_x_label_ = nullptr;
    gyro_y_label_ = nullptr;
    gyro_z_label_ = nullptr;
    acc_x_label_ = nullptr;
    acc_y_label_ = nullptr;
    acc_z_label_ = nullptr;
}

lv_obj_t* GyroApp::CreateValuePanel(const char* title, lv_coord_t x, lv_coord_t y, uint32_t color) {
    auto* panel = lv_obj_create(root_);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 94, 38);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_80, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, rodakos_theme_border(), 0);
    lv_obj_set_style_pad_all(panel, 4, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    auto* title_label = CreateText(panel, title, &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, -1);

    auto* value_label = CreateText(panel, "--", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_width(value_label, 84);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_RIGHT, 0, 1);
    return value_label;
}

lv_obj_t* GyroApp::CreateAxisLabel(const char* text, lv_coord_t x, lv_coord_t y, uint32_t color) {
    auto* label = CreateText(root_, text, &phone_font_12, lv_color_hex(color));
    lv_obj_set_width(label, 94);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

bool GyroApp::CreateAttitudeCanvas() {
    const uint32_t canvas_size =
        lv_canvas_buf_size(kCanvasSize, kCanvasSize, 32, LV_DRAW_BUF_STRIDE_ALIGN);
    attitude_canvas_buffer_ = heap_caps_malloc(canvas_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (attitude_canvas_buffer_ == nullptr) {
        ESP_LOGW(TAG, "Failed to allocate attitude canvas buffer");
        return false;
    }

    attitude_canvas_ = lv_canvas_create(root_);
    lv_draw_buf_init(&attitude_draw_buf_, kCanvasSize, kCanvasSize, LV_COLOR_FORMAT_ARGB8888,
                     LV_DRAW_BUF_STRIDE(kCanvasSize, LV_COLOR_FORMAT_ARGB8888),
                     attitude_canvas_buffer_, canvas_size);
    lv_draw_buf_set_flag(&attitude_draw_buf_, LV_IMAGE_FLAGS_MODIFIABLE);
    lv_canvas_set_draw_buf(attitude_canvas_, &attitude_draw_buf_);
    lv_obj_set_size(attitude_canvas_, kCanvasSize, kCanvasSize);
    lv_obj_align(attitude_canvas_, LV_ALIGN_TOP_MID, 0, 59);
    lv_obj_clear_flag(attitude_canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(attitude_canvas_, LV_OBJ_FLAG_CLICKABLE);
    return true;
}

void GyroApp::ReleaseAttitudeCanvas() {
    if (attitude_canvas_buffer_ != nullptr) {
        heap_caps_free(attitude_canvas_buffer_);
        attitude_canvas_buffer_ = nullptr;
    }
    std::memset(&attitude_draw_buf_, 0, sizeof(attitude_draw_buf_));
}

void GyroApp::Refresh() {
    if (status_label_ == nullptr) {
        return;
    }

    rodakos::MotionSample sample;
    if (motion_ == nullptr) {
        sample.status = rodakos::MotionStatus::kUnavailable;
        sample.message = "Motion service not ready";
    } else if (!motion_started_) {
        sample.status = rodakos::MotionStatus::kError;
        sample.message = motion_->last_error().empty() ? "Motion sensor unavailable" : motion_->last_error();
    } else {
        motion_->ReadSample(sample);
    }

    const bool ready = sample.status == rodakos::MotionStatus::kReady;
    if (ready) {
        char status[48] = {};
        std::snprintf(status, sizeof(status), "Motion %.0f dps", Magnitude(sample.gyro_dps));
        SetStatus(status, true);
    } else {
        SetStatus(sample.message.c_str(), false);
    }

    if (!ready) {
        SetLabelFormatted(roll_value_label_, "%.1f deg", 0.0f);
        SetLabelFormatted(pitch_value_label_, "%.1f deg", 0.0f);
        SetLabelFormatted(yaw_value_label_, "%.1f deg", 0.0f);
        lv_label_set_text(gyro_x_label_, "GX --");
        lv_label_set_text(gyro_y_label_, "GY --");
        lv_label_set_text(gyro_z_label_, "GZ --");
        lv_label_set_text(acc_x_label_, "AX --");
        lv_label_set_text(acc_y_label_, "AY --");
        lv_label_set_text(acc_z_label_, "AZ --");
        DrawAttitude(sample);
        return;
    }

    SetLabelFormatted(roll_value_label_, "%.1f deg", sample.roll_deg);
    SetLabelFormatted(pitch_value_label_, "%.1f deg", sample.pitch_deg);
    SetLabelFormatted(yaw_value_label_, "%.1f deg", sample.yaw_deg);

    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "GX %.1f", sample.gyro_dps.x);
    lv_label_set_text(gyro_x_label_, buffer);
    std::snprintf(buffer, sizeof(buffer), "GY %.1f", sample.gyro_dps.y);
    lv_label_set_text(gyro_y_label_, buffer);
    std::snprintf(buffer, sizeof(buffer), "GZ %.1f", sample.gyro_dps.z);
    lv_label_set_text(gyro_z_label_, buffer);

    std::snprintf(buffer, sizeof(buffer), "AX %.2fg", sample.accel_g.x);
    lv_label_set_text(acc_x_label_, buffer);
    std::snprintf(buffer, sizeof(buffer), "AY %.2fg", sample.accel_g.y);
    lv_label_set_text(acc_y_label_, buffer);
    std::snprintf(buffer, sizeof(buffer), "AZ %.2fg", sample.accel_g.z);
    lv_label_set_text(acc_z_label_, buffer);

    DrawAttitude(sample);
}

void GyroApp::DrawAttitude(const rodakos::MotionSample& sample) {
    if (attitude_canvas_ == nullptr) {
        return;
    }

    lv_canvas_fill_bg(attitude_canvas_, lv_color_hex(0x071118), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(attitude_canvas_, &layer);

    DrawCanvasRoundRect(&layer, 0, 0, kCanvasSize - 1, kCanvasSize - 1, LV_RADIUS_CIRCLE, 0x13232B,
                        LV_OPA_COVER);
    DrawCanvasRoundRect(&layer, 5, 5, kCanvasSize - 6, kCanvasSize - 6, LV_RADIUS_CIRCLE, 0x0D171C,
                        LV_OPA_COVER);
    DrawCanvasLine(&layer, kCanvasSize / 2, 12, kCanvasSize / 2, kCanvasSize - 12, 1, 0x284653,
                   LV_OPA_70);
    DrawCanvasLine(&layer, 12, kCanvasSize / 2, kCanvasSize - 12, kCanvasSize / 2, 1, 0x284653,
                   LV_OPA_70);

    const float roll = ClampFloat(sample.roll_deg, -45.0f, 45.0f);
    const float pitch = ClampFloat(sample.pitch_deg, -45.0f, 45.0f);
    const float roll_rad = roll * kPi / 180.0f;
    const int center = kCanvasSize / 2;
    const int center_x = center + static_cast<int>(roll * 0.36f);
    const int center_y = center + static_cast<int>(pitch * 0.36f);
    const int half = 32;
    const int dx = static_cast<int>(std::cos(roll_rad) * half);
    const int dy = static_cast<int>(std::sin(roll_rad) * half);

    DrawCanvasLine(&layer, center_x - dx, center_y - dy, center_x + dx, center_y + dy, 5, 0x8BE0F2,
                   LV_OPA_COVER);
    DrawCanvasLine(&layer, center_x - dx, center_y - dy + 7, center_x + dx, center_y + dy + 7, 2,
                   0x335D6B, LV_OPA_80);
    DrawCanvasRoundRect(&layer, center_x - 6, center_y - 6, center_x + 6, center_y + 6,
                        LV_RADIUS_CIRCLE, 0xF4FBFF, LV_OPA_COVER);

    const float gyro_z = ClampFloat(sample.gyro_dps.z, -180.0f, 180.0f);
    const int needle_angle = static_cast<int>(gyro_z);
    const int needle_x = center + ((34 * lv_trigo_cos(needle_angle)) >> LV_TRIGO_SHIFT);
    const int needle_y = center + ((34 * lv_trigo_sin(needle_angle)) >> LV_TRIGO_SHIFT);
    DrawCanvasLine(&layer, center, center, needle_x, needle_y, 2, 0xF0B96E, LV_OPA_COVER);

    const bool moving = std::fabs(sample.gyro_dps.x) + std::fabs(sample.gyro_dps.y) +
                        std::fabs(sample.gyro_dps.z) > 12.0f;
    if (moving) {
        DrawCanvasTriangle(&layer, center, 14, center + 6, 26, center - 6, 26, 0xB4D887,
                           LV_OPA_COVER);
    }

    lv_canvas_finish_layer(attitude_canvas_, &layer);
    lv_obj_invalidate(attitude_canvas_);
}

void GyroApp::SetStatus(const char* text, bool ready) {
    if (status_label_ == nullptr) {
        return;
    }
    lv_obj_set_style_text_color(status_label_,
                                ready ? rodakos_theme_success() : rodakos_theme_error(),
                                0);
    lv_label_set_text(status_label_, text != nullptr ? text : "");
}

void GyroApp::RefreshTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<GyroApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->Refresh();
    }
}

void GyroApp::NavigateHome() {
    ESP_LOGI(TAG, "Returning home");
    lv_async_call(DeferReturnHome, context_);
}

void RegisterGyroApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "gyro",
        .title = "Gyro",
        .icon = FONT_AWESOME_COMPASS,
        .category = PhoneAppCategory::kTools,
        .capabilities = PhoneCapability::kMotion,
        .show_on_home = true,
        .aliases = {"gyro", "gyroscope", "imu", "motion", "orientation", "陀螺仪"},
        .create = []() { return std::make_unique<GyroApp>(); },
    });
}

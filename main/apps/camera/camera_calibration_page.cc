// Camera calibration page: an overlay on the Camera app's own preview.
//
// The Camera app keeps the preview lease; CameraCalibrationService only reads frames. Boxes are
// drawn in preview-box coordinates. While calibrating, the viewfinder widens over the shutter rail
// to the full 320x200 so the result panel fits beside the 0.7x frame (see CameraApp::UpdatePreview).
#include "apps/camera/camera_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <new>

#include <esp_log.h>

namespace {

constexpr const char* TAG = "CameraCalibrationPage";
constexpr lv_coord_t kBoxWidth = 320;
constexpr lv_coord_t kBoxHeight = 200;
constexpr lv_coord_t kViewfinderWidth = 266;
// While calibrating, the whole 4:3 frame is shown left-aligned at 224x168 (0.7x), vertically
// centred, and the result panel takes the column to its right, so it never covers what is being
// measured. The column is 84 px: four CJK characters per line, since phone_font_12 falls back to
// the 16 px PuHui font for Chinese.
constexpr int32_t kCalibrationScale = LV_SCALE_NONE * 7 / 10;
constexpr lv_coord_t kFrameWidth = 320 * 7 / 10;
constexpr lv_coord_t kPanelX = kFrameWidth + 4;
constexpr lv_coord_t kPanelWidth = kBoxWidth - kPanelX;
constexpr uint32_t kRefreshMs = 150;

lv_obj_t* CreateOutline(lv_obj_t* parent, lv_color_t color, bool dashed) {
    auto* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, color, 0);
    lv_obj_set_style_border_opa(box, dashed ? LV_OPA_60 : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 3, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
    return box;
}

lv_obj_t* CreateText(lv_obj_t* parent, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

// Maps a frame rectangle to the preview box. Same placement as CameraApp::UpdatePreview while
// calibrating: the whole frame fitted to the box and aligned to its left edge.
void PlaceFrameRect(lv_obj_t* obj, int frame_w, int frame_h, double x, double y, double w, double h) {
    if (frame_w <= 0 || frame_h <= 0) return;
    const double scale = static_cast<double>(kCalibrationScale) / LV_SCALE_NONE;
    const double offset_x = 0.0;
    const double offset_y = (kBoxHeight - frame_h * scale) / 2.0;
    lv_obj_set_pos(obj, static_cast<lv_coord_t>(std::lround(offset_x + x * scale)),
                   static_cast<lv_coord_t>(std::lround(offset_y + y * scale)));
    lv_obj_set_size(obj, static_cast<lv_coord_t>(std::lround(w * scale)),
                    static_cast<lv_coord_t>(std::lround(h * scale)));
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

const char* IssueText(rodakos::CameraFrameIssue issue) {
    switch (issue) {
        case rodakos::CameraFrameIssue::kTooDark: return "光线不足";
        case rodakos::CameraFrameIssue::kOverexposed: return "画面过亮";
        case rodakos::CameraFrameIssue::kMoving: return "保持不动";
        case rodakos::CameraFrameIssue::kFlat: return "对准文字或校准图";
        case rodakos::CameraFrameIssue::kNone:
        default: return nullptr;
    }
}

const char* DistanceText(rodakos::CameraDistanceHint hint) {
    switch (hint) {
        case rodakos::CameraDistanceHint::kCloser: return "靠近一些";
        case rodakos::CameraDistanceHint::kFurther: return "远一点";
        case rodakos::CameraDistanceHint::kGood: return "距离合适";
        default: return nullptr;
    }
}

}  // namespace

void CameraApp::OpenCalibration() {
    if (preview_box_ == nullptr || calibration_layer_ != nullptr || camera_ == nullptr) return;
    if (!calibration_) {
        calibration_.reset(new (std::nothrow) rodakos::CameraCalibrationService(camera_));
        if (!calibration_) {
            UpdateStatus("校准启动失败：内存不足", true);
            return;
        }
    }
    if (!calibration_->Start()) {
        UpdateStatus("校准启动失败", true);
        return;
    }
    lv_obj_set_width(preview_box_, kBoxWidth);
    lv_obj_move_foreground(preview_box_);

    // Layer over the preview: outlines plus a translucent result panel at the bottom.
    calibration_layer_ = lv_obj_create(preview_box_);
    lv_obj_remove_style_all(calibration_layer_);
    lv_obj_set_size(calibration_layer_, kBoxWidth, kBoxHeight);
    lv_obj_set_pos(calibration_layer_, 0, 0);
    lv_obj_clear_flag(calibration_layer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(calibration_layer_, LV_OBJ_FLAG_CLICKABLE);

    calibration_center_box_ = CreateOutline(calibration_layer_, lv_color_white(), true);
    calibration_qr_box_ = CreateOutline(calibration_layer_, rodakos_theme_success(), false);
    calibration_region_box_ = CreateOutline(calibration_layer_, rodakos_theme_primary(), false);

    calibration_panel_ = lv_obj_create(calibration_layer_);
    lv_obj_remove_style_all(calibration_panel_);
    lv_obj_set_size(calibration_panel_, kPanelWidth, kBoxHeight);
    lv_obj_set_pos(calibration_panel_, kPanelX, 0);
    lv_obj_set_style_bg_color(calibration_panel_, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(calibration_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(calibration_panel_, 6, 0);
    lv_obj_set_style_pad_all(calibration_panel_, 4, 0);
    lv_obj_clear_flag(calibration_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(calibration_panel_, LV_OBJ_FLAG_CLICKABLE);
    constexpr lv_coord_t kInner = kPanelWidth - 8;
    constexpr lv_coord_t kBarTop = 26;
    constexpr lv_coord_t kBarHeight = 64;

    calibration_score_ = CreateText(calibration_panel_, &phone_font_18, rodakos_theme_text_primary());
    lv_obj_set_width(calibration_score_, kInner);
    lv_obj_set_style_text_align(calibration_score_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(calibration_score_, LV_ALIGN_TOP_MID, 0, 0);

    // Vertical bar: higher is sharper. The tick marks the best score so far.
    calibration_bar_ = lv_bar_create(calibration_panel_);
    lv_obj_set_size(calibration_bar_, 10, kBarHeight);
    lv_obj_align(calibration_bar_, LV_ALIGN_TOP_MID, 0, kBarTop);
    lv_bar_set_range(calibration_bar_, 0, 100);
    lv_obj_set_style_bg_color(calibration_bar_, rodakos_theme_bg_tertiary(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(calibration_bar_, rodakos_theme_primary(), LV_PART_INDICATOR);

    calibration_best_mark_ = lv_obj_create(calibration_panel_);
    lv_obj_remove_style_all(calibration_best_mark_);
    lv_obj_set_size(calibration_best_mark_, 20, 2);
    lv_obj_set_style_bg_color(calibration_best_mark_, rodakos_theme_text_secondary(), 0);
    lv_obj_set_style_bg_opa(calibration_best_mark_, LV_OPA_COVER, 0);
    lv_obj_add_flag(calibration_best_mark_, LV_OBJ_FLAG_HIDDEN);

    // Status and hint stack in a column flex container, so a status that wraps to two lines
    // pushes the hint down instead of running into it.
    auto* text_column = lv_obj_create(calibration_panel_);
    lv_obj_remove_style_all(text_column);
    lv_obj_set_size(text_column, kInner, kBoxHeight - 8 - (kBarTop + kBarHeight + 4));
    lv_obj_align(text_column, LV_ALIGN_TOP_MID, 0, kBarTop + kBarHeight + 4);
    lv_obj_set_flex_flow(text_column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(text_column, 2, 0);
    lv_obj_clear_flag(text_column, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(text_column, LV_OBJ_FLAG_CLICKABLE);

    calibration_status_ = CreateText(text_column, &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_width(calibration_status_, kInner);
    lv_label_set_long_mode(calibration_status_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(calibration_status_, LV_TEXT_ALIGN_CENTER, 0);

    calibration_hint_ = CreateText(text_column, &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_width(calibration_hint_, kInner);
    lv_label_set_long_mode(calibration_hint_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(calibration_hint_, LV_TEXT_ALIGN_CENTER, 0);

    // Taking photos is not part of calibration; the widened viewfinder covers the shutter rail.
    if (rail_ != nullptr) lv_obj_add_flag(rail_, LV_OBJ_FLAG_HIDDEN);
    if (calibration_button_ != nullptr) lv_obj_add_state(calibration_button_, LV_STATE_CHECKED);
    calibration_revision_ = 0;
    lv_label_set_text(calibration_status_, "正在准备…");
    calibration_timer_ = lv_timer_create([](lv_timer_t* timer) {
        static_cast<CameraApp*>(lv_timer_get_user_data(timer))->UpdateCalibration();
    }, kRefreshMs, this);
    // The result pill would overlap the frame; the panel carries the messages instead.
    HideStatus();
    UpdatePreviewLayout();
    ESP_LOGI(TAG, "Calibration page opened");
}

void CameraApp::CloseCalibration() {
    if (calibration_) calibration_->Stop();
    if (calibration_timer_ != nullptr) {
        lv_timer_delete(calibration_timer_);
        calibration_timer_ = nullptr;
    }
    if (calibration_layer_ != nullptr && lv_obj_is_valid(calibration_layer_)) {
        lv_obj_delete(calibration_layer_);
    }
    calibration_layer_ = nullptr;
    calibration_center_box_ = nullptr;
    calibration_qr_box_ = nullptr;
    calibration_region_box_ = nullptr;
    calibration_panel_ = nullptr;
    calibration_status_ = nullptr;
    calibration_score_ = nullptr;
    calibration_bar_ = nullptr;
    calibration_best_mark_ = nullptr;
    calibration_hint_ = nullptr;
    if (rail_ != nullptr && lv_obj_is_valid(rail_)) lv_obj_clear_flag(rail_, LV_OBJ_FLAG_HIDDEN);
    if (preview_box_ != nullptr && lv_obj_is_valid(preview_box_)) {
        lv_obj_set_width(preview_box_, kViewfinderWidth);
    }
    if (calibration_button_ != nullptr && lv_obj_is_valid(calibration_button_)) {
        lv_obj_clear_state(calibration_button_, LV_STATE_CHECKED);
    }
    UpdatePreviewLayout();
}

void CameraApp::UpdateCalibration() {
    if (!calibration_ || calibration_layer_ == nullptr) return;
    const auto snapshot = calibration_->snapshot();
    if (snapshot.revision == 0 || snapshot.revision == calibration_revision_) return;
    calibration_revision_ = snapshot.revision;
    const auto& r = snapshot.result;
    const int fw = snapshot.frame_width;
    const int fh = snapshot.frame_height;

    // Outlines: the measured region, and the QR code when the chart is found.
    if (r.on_chart) {
        lv_obj_add_flag(calibration_center_box_, LV_OBJ_FLAG_HIDDEN);
        PlaceFrameRect(calibration_region_box_, fw, fh, r.region.x, r.region.y, r.region.width, r.region.height);
    } else {
        lv_obj_add_flag(calibration_region_box_, LV_OBJ_FLAG_HIDDEN);
        PlaceFrameRect(calibration_center_box_, fw, fh, r.region.x, r.region.y, r.region.width, r.region.height);
    }
    if (r.chart.found) {
        double min_x = 1e9, min_y = 1e9, max_x = -1e9, max_y = -1e9;
        for (const auto& p : r.chart.corners) {
            min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
            min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
        }
        lv_obj_set_style_border_color(calibration_qr_box_,
                                      r.chart.mirrored ? rodakos_theme_warning() : rodakos_theme_success(), 0);
        PlaceFrameRect(calibration_qr_box_, fw, fh, min_x, min_y, max_x - min_x, max_y - min_y);
    } else {
        lv_obj_add_flag(calibration_qr_box_, LV_OBJ_FLAG_HIDDEN);
    }

    // Line 1: the conclusion. Line 2: what to do next.
    std::string status;
    std::string hint;
    lv_color_t status_color = rodakos_theme_text_secondary();
    if (r.chart.found && r.chart.mirrored) {
        status = "画面被镜像";
        status_color = rodakos_theme_warning();
        hint = "看图上箭头判断方向";
    } else if (r.chart.found) {
        status = "方向正确";
        status_color = rodakos_theme_success();
        if (const char* distance = DistanceText(r.distance)) status += std::string("\n") + distance;
        hint = r.on_chart ? "保持不动" : "让整张图入画";
    } else {
        status = "清晰度";
        hint = "测量框内画面";
    }
    if (const char* issue = IssueText(r.issue)) hint = issue;
    lv_label_set_text(calibration_status_, status.c_str());
    lv_obj_set_style_text_color(calibration_status_, status_color, 0);
    lv_label_set_text(calibration_hint_, hint.c_str());

    if (r.score >= 0 && !r.chart.mirrored) {
        lv_label_set_text_fmt(calibration_score_, "%d", r.score);
        lv_bar_set_value(calibration_bar_, r.score, LV_ANIM_OFF);
    } else {
        lv_label_set_text(calibration_score_, "—");
        lv_bar_set_value(calibration_bar_, 0, LV_ANIM_OFF);
    }
    if (r.best >= 0 && !r.chart.mirrored) {
        const lv_coord_t bar_h = lv_obj_get_height(calibration_bar_);
        lv_obj_align_to(calibration_best_mark_, calibration_bar_, LV_ALIGN_BOTTOM_MID, 0,
                        -static_cast<lv_coord_t>(bar_h * r.best / 100) + 1);
        lv_obj_clear_flag(calibration_best_mark_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(calibration_best_mark_, LV_OBJ_FLAG_HIDDEN);
    }
}

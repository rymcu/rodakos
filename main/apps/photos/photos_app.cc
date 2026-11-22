#include "photos_app.h"
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
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <utility>

namespace {
constexpr const char* TAG = "PhotosApp";

// Layout follows the approved 320x240 design: a 40 px header, a 4x2 grid of 78 px tiles with
// 2 px seams, and a 40 px page bar.
constexpr lv_coord_t kHeaderHeight = kRodakosAppHeaderHeight;
constexpr lv_coord_t kBarHeight = 40;
constexpr int kGridCols = 4;
constexpr int kGridRows = 2;
constexpr size_t kPhotosPerPage = kGridCols * kGridRows;
constexpr lv_coord_t kTileSize = 78;
constexpr lv_coord_t kTileGap = 2;
constexpr lv_coord_t kGridLeft = 1;
constexpr lv_coord_t kGridTop = 1;
constexpr lv_coord_t kPagerButtonWidth = 64;
constexpr lv_coord_t kOverlayTopHeight = 40;
constexpr lv_coord_t kOverlayBottomHeight = 32;
constexpr lv_coord_t kEdgeButtonWidth = 32;
constexpr lv_coord_t kEdgeButtonHeight = 40;
constexpr lv_opa_t kOverlayOpa = LV_OPA_50;
constexpr uint32_t kThumbnailTimerMs = 120;
constexpr int32_t kSwipeThreshold = 40;
// Same floor as TimeService: earlier timestamps come from an unsynced clock.
constexpr time_t kMinimumValidTime = 1577836800;

const char* ScanStatusText(rodakos::ImageLibrary::ImageScanStatus status) {
    using Status = rodakos::ImageLibrary::ImageScanStatus;
    switch (status) {
        case Status::kReady: return "还没有照片";
        case Status::kServiceUnavailable: return "存储服务不可用";
        case Status::kStorageUnavailable: return "读不到 SD 卡";
        case Status::kDirectoryMissing: return "相册目录不可用";
        case Status::kReadFailed: return "无法读取相册";
    }
    return "无法读取相册";
}

const char* LoadStatusText(rodakos::ImageLibrary::ImageLoadStatus status) {
    using Status = rodakos::ImageLibrary::ImageLoadStatus;
    switch (status) {
        case Status::kUnsupported: return "不支持这种图片格式";
        case Status::kReadFailed: return "无法读取照片";
        case Status::kInsufficientMemory: return "图片太大，内存不够";
        case Status::kLoaded:
        case Status::kDecodeFailed: return "文件已损坏";
    }
    return "文件已损坏";
}

std::string Extension(const std::string& filename) {
    const auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string upper = filename.substr(dot + 1, 4);
    for (auto& c : upper) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return upper;
}

std::string FormatPhotoDate(uint64_t modified_time) {
    if (modified_time < static_cast<uint64_t>(kMinimumValidTime)) return {};
    const time_t now = time(nullptr);
    if (now < kMinimumValidTime) return {};
    const time_t when = static_cast<time_t>(modified_time);
    tm photo{};
    tm today{};
    if (localtime_r(&when, &photo) == nullptr || localtime_r(&now, &today) == nullptr) return {};
    char text[48];
    if (photo.tm_year == today.tm_year && photo.tm_yday == today.tm_yday) {
        std::snprintf(text, sizeof(text), "今天 %02d:%02d", photo.tm_hour, photo.tm_min);
    } else if (photo.tm_year == today.tm_year) {
        std::snprintf(text, sizeof(text), "%d月%d日", photo.tm_mon + 1, photo.tm_mday);
    } else {
        std::snprintf(text, sizeof(text), "%d年%d月%d日", photo.tm_year + 1900, photo.tm_mon + 1,
                      photo.tm_mday);
    }
    return text;
}

void DeferReturnHome(void* user_data) {
    static_cast<PhoneAppContext*>(user_data)->navigation().ReturnHome();
}

void SetHidden(lv_obj_t* obj, bool hidden) {
    if (obj == nullptr) return;
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void SetEnabled(lv_obj_t* obj, bool enabled) {
    if (obj == nullptr) return;
    if (enabled) lv_obj_clear_state(obj, LV_STATE_DISABLED);
    else lv_obj_add_state(obj, LV_STATE_DISABLED);
}

lv_obj_t* CreatePlainBox(lv_obj_t* parent) {
    auto* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    return box;
}

lv_obj_t* CreateGlyph(lv_obj_t* parent, const char* icon, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_font(label, PhoneIconFont(), 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

// A flat icon button: no fill at rest, a translucent press state, and the glyph centered.
lv_obj_t* CreateBarButton(lv_obj_t* parent, const char* icon, lv_coord_t width, lv_coord_t height,
                          lv_color_t color) {
    auto* button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, color, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(CreateGlyph(button, icon, color));
    return button;
}

void LogPhotoHeap(const char* phase, const char* reason) {
    ESP_LOGI(TAG,
             "Photo image memory %s (%s): internal_free=%u internal_largest=%u "
             "spiram_free=%u spiram_largest=%u",
             phase,
             reason != nullptr ? reason : "unknown",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}
}  // namespace

bool PhotosApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return false;

    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, ui_->theme().background, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // Tiles share two styles so eight of them add no per-object style allocations.
    lv_style_init(&tile_style_);
    lv_style_set_bg_color(&tile_style_, ui_->theme().surface);
    lv_style_set_bg_opa(&tile_style_, LV_OPA_COVER);
    lv_style_set_radius(&tile_style_, 3);
    lv_style_set_clip_corner(&tile_style_, true);
    lv_style_init(&tile_pressed_style_);
    lv_style_set_border_color(&tile_pressed_style_, ui_->theme().accent);
    lv_style_set_border_width(&tile_pressed_style_, 2);
    lv_style_set_border_post(&tile_pressed_style_, true);

    CreateView(ViewMode::kGrid);
    RefreshPhotos();
    return true;
}

void PhotosApp::OnDestroy() {
    if (root_ != nullptr) {
        PhoneUiLock lock(*ui_, 0);
        if (!lock.locked()) return;
        lv_async_call_cancel(DeferReturnHome, context_);
        ClearView();
        lv_obj_delete(root_);
        lv_style_reset(&tile_style_);
        lv_style_reset(&tile_pressed_style_);
        root_ = nullptr;
    }
    photos_.clear();
    current_view_ = ViewMode::kNone;
    context_ = nullptr;
    ui_ = nullptr;
}

bool PhotosApp::OnBackRequested() {
    if (current_view_ != ViewMode::kFullScreen) return false;
    PhoneUiLock lock(*ui_, 0);
    if (lock.locked()) BackToGrid();
    return true;
}

void PhotosApp::ClearGridPage() {
    StopThumbnailTimer();
    for (auto& item : thumbnail_items_) {
        if (item.thumbnail == nullptr) continue;
        lv_image_set_src(item.image, nullptr);
        lv_image_cache_drop(item.thumbnail->GetImageSource());
    }
    thumbnail_items_.clear();
    if (current_view_ == ViewMode::kGrid && image_area_ != nullptr) lv_obj_clean(image_area_);
}

void PhotosApp::ClearView() {
    ClearGridPage();
    ReleaseCurrentImage("leave view");
    lv_obj_clean(root_);
    image_area_ = title_label_ = back_button_ = previous_button_ = next_button_ = nullptr;
    notice_ = notice_title_ = notice_detail_ = retry_button_ = nullptr;
    refresh_button_ = page_indicator_ = page_label_ = nullptr;
    photo_img_ = top_bar_ = bottom_bar_ = count_label_ = filename_label_ = date_label_ = nullptr;
    swipe_direction_ = 0;
    swipe_tracking_ = false;
}

void PhotosApp::CreateView(ViewMode mode) {
    if (current_view_ != ViewMode::kNone) ClearView();
    current_view_ = mode;
    preview_failed_ = false;
    if (mode == ViewMode::kGrid) CreateGridChrome();
    else CreateViewerChrome();
}

void PhotosApp::CreateGridChrome() {
    auto* header = CreateAppHeader(root_, "相册", [](lv_event_t* e) {
        static_cast<PhotosApp*>(lv_event_get_user_data(e))->NavigateHome();
    }, [](lv_event_t* e) {
        static_cast<PhotosApp*>(lv_event_get_user_data(e))->RefreshPhotos();
    }, this, &title_label_, ICON_REFRESH, &refresh_button_);
    back_button_ = lv_obj_get_child(header, 0);
    lv_obj_set_style_opa(refresh_button_, LV_OPA_40, LV_STATE_DISABLED);

    image_area_ = CreatePlainBox(root_);
    lv_obj_set_size(image_area_, ui_->width(), ui_->height() - kHeaderHeight - kBarHeight);
    lv_obj_set_pos(image_area_, 0, kHeaderHeight);
    lv_obj_add_flag(image_area_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(image_area_, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(image_area_, HandleImageInput, LV_EVENT_ALL, this);

    auto* bar = CreatePlainBox(root_);
    lv_obj_set_size(bar, ui_->width(), kBarHeight);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    previous_button_ = CreateBarButton(bar, ICON_CHEVRON_LEFT, kPagerButtonWidth, kBarHeight,
                                       ui_->theme().text_primary);
    next_button_ = CreateBarButton(bar, ICON_CHEVRON_RIGHT, kPagerButtonWidth, kBarHeight,
                                   ui_->theme().text_primary);
    lv_obj_align(previous_button_, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(next_button_, LV_ALIGN_RIGHT_MID, 0, 0);
    const auto step = [](lv_event_t* e) {
        auto* self = static_cast<PhotosApp*>(lv_event_get_user_data(e));
        self->Step(lv_event_get_target_obj(e) == self->previous_button_ ? -1 : 1);
    };
    lv_obj_add_event_cb(previous_button_, step, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(next_button_, step, LV_EVENT_CLICKED, this);

    // Up to eight pages draw as dots in one object; beyond that a "3 / 12" label is clearer.
    page_indicator_ = CreatePlainBox(bar);
    lv_obj_set_size(page_indicator_, 128, 8);
    lv_obj_center(page_indicator_);
    lv_obj_add_event_cb(page_indicator_, DrawPageDots, LV_EVENT_DRAW_MAIN, this);
    page_label_ = lv_label_create(bar);
    lv_obj_set_style_text_font(page_label_, &phone_font_14, 0);
    lv_obj_set_style_text_color(page_label_, ui_->theme().text_secondary, 0);
    lv_obj_center(page_label_);
}

void PhotosApp::CreateViewerChrome() {
    lv_obj_set_style_bg_color(root_, lv_color_black(), 0);

    image_area_ = CreatePlainBox(root_);
    lv_obj_set_size(image_area_, ui_->width(), ui_->height());
    lv_obj_add_flag(image_area_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(image_area_, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(image_area_, HandleImageInput, LV_EVENT_ALL, this);
    photo_img_ = lv_image_create(image_area_);
    lv_obj_clear_flag(photo_img_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(photo_img_, ui_->width(), ui_->height());
    lv_image_set_inner_align(photo_img_, LV_IMAGE_ALIGN_CENTER);
    lv_obj_center(photo_img_);

    const lv_color_t white = lv_color_white();
    top_bar_ = CreatePlainBox(root_);
    lv_obj_set_size(top_bar_, ui_->width(), kOverlayTopHeight);
    lv_obj_set_style_bg_color(top_bar_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(top_bar_, kOverlayOpa, 0);
    back_button_ = CreateBarButton(top_bar_, ICON_ARROW_LEFT, 40, 26, white);
    lv_obj_set_style_bg_color(back_button_, white, 0);
    lv_obj_set_style_bg_opa(back_button_, LV_OPA_20, 0);
    lv_obj_set_style_radius(back_button_, 6, 0);
    lv_obj_set_ext_click_area(back_button_, 7);
    lv_obj_align(back_button_, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_add_event_cb(back_button_, [](lv_event_t* e) {
        static_cast<PhotosApp*>(lv_event_get_user_data(e))->BackToGrid();
    }, LV_EVENT_CLICKED, this);
    count_label_ = lv_label_create(top_bar_);
    lv_obj_set_style_text_font(count_label_, &phone_font_14, 0);
    lv_obj_set_style_text_color(count_label_, white, 0);
    lv_obj_center(count_label_);

    bottom_bar_ = CreatePlainBox(root_);
    lv_obj_set_size(bottom_bar_, ui_->width(), kOverlayBottomHeight);
    lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bottom_bar_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bottom_bar_, kOverlayOpa, 0);
    filename_label_ = lv_label_create(bottom_bar_);
    lv_obj_set_style_text_font(filename_label_, &phone_font_12, 0);
    lv_obj_set_style_text_color(filename_label_, white, 0);
    lv_obj_set_style_text_opa(filename_label_, LV_OPA_80, 0);
    lv_label_set_long_mode(filename_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(filename_label_, 196);
    lv_obj_align(filename_label_, LV_ALIGN_LEFT_MID, 12, 0);
    date_label_ = lv_label_create(bottom_bar_);
    lv_obj_set_style_text_font(date_label_, &phone_font_14, 0);
    lv_obj_set_style_text_color(date_label_, white, 0);
    lv_obj_align(date_label_, LV_ALIGN_RIGHT_MID, -12, 0);

    const auto make_edge = [&](const char* icon, lv_align_t align, lv_coord_t x) {
        auto* button = CreateBarButton(root_, icon, kEdgeButtonWidth, kEdgeButtonHeight, white);
        lv_obj_set_style_bg_color(button, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_40, 0);
        lv_obj_set_ext_click_area(button, 8);
        lv_obj_align(button, align, x, 0);
        lv_obj_add_event_cb(button, [](lv_event_t* e) {
            auto* self = static_cast<PhotosApp*>(lv_event_get_user_data(e));
            self->Step(lv_event_get_target_obj(e) == self->previous_button_ ? -1 : 1);
        }, LV_EVENT_CLICKED, this);
        return button;
    };
    previous_button_ = make_edge(ICON_CHEVRON_LEFT, LV_ALIGN_LEFT_MID, 6);
    next_button_ = make_edge(ICON_CHEVRON_RIGHT, LV_ALIGN_RIGHT_MID, -6);
    overlay_visible_ = true;
}

void PhotosApp::ShowNotice(const char* title, const char* detail, bool retry, bool dark) {
    if (notice_ == nullptr) {
        notice_ = CreatePlainBox(root_);
        lv_obj_set_size(notice_, ui_->width(), 130);
        lv_obj_align(notice_, LV_ALIGN_CENTER, 0, dark ? 0 : 6);
        const lv_color_t muted = dark ? lv_color_white() : rodakos_theme_text_tertiary();
        auto* icon = CreateGlyph(notice_, ICON_IMAGE, muted);
        lv_obj_set_style_text_opa(icon, dark ? LV_OPA_60 : LV_OPA_COVER, 0);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);
        notice_title_ = lv_label_create(notice_);
        lv_obj_set_style_text_font(notice_title_, &phone_font_14, 0);
        lv_obj_set_style_text_color(notice_title_, dark ? lv_color_white() : ui_->theme().text_primary, 0);
        lv_obj_align(notice_title_, LV_ALIGN_TOP_MID, 0, 30);
        notice_detail_ = lv_label_create(notice_);
        lv_obj_set_style_text_font(notice_detail_, &phone_font_14, 0);
        lv_obj_set_style_text_color(notice_detail_, dark ? lv_color_white() : ui_->theme().text_secondary, 0);
        lv_obj_set_style_text_opa(notice_detail_, dark ? LV_OPA_70 : LV_OPA_COVER, 0);
        lv_obj_set_style_text_align(notice_detail_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(notice_detail_, ui_->width() - 32);
        lv_obj_align(notice_detail_, LV_ALIGN_TOP_MID, 0, 54);
        retry_button_ = lv_button_create(notice_);
        lv_obj_remove_style_all(retry_button_);
        lv_obj_set_size(retry_button_, 120, 40);
        lv_obj_set_style_radius(retry_button_, 10, 0);
        lv_obj_set_style_bg_color(retry_button_, ui_->theme().accent, 0);
        lv_obj_set_style_bg_opa(retry_button_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(retry_button_, lv_color_darken(ui_->theme().accent, 40), LV_STATE_PRESSED);
        lv_obj_set_style_opa(retry_button_, LV_OPA_40, LV_STATE_DISABLED);
        lv_obj_align(retry_button_, LV_ALIGN_TOP_MID, 0, 88);
        lv_obj_set_flex_flow(retry_button_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(retry_button_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(retry_button_, 6, 0);
        CreateGlyph(retry_button_, ICON_REFRESH, ui_->theme().accent_text);
        auto* text = lv_label_create(retry_button_);
        lv_label_set_text(text, "重试");
        lv_obj_set_style_text_font(text, &phone_font_14, 0);
        lv_obj_set_style_text_color(text, ui_->theme().accent_text, 0);
        lv_obj_add_event_cb(retry_button_, [](lv_event_t* e) {
            static_cast<PhotosApp*>(lv_event_get_user_data(e))->Retry();
        }, LV_EVENT_CLICKED, this);
        // Keep the overlay bars and edge arrows above the notice in the viewer.
        if (top_bar_ != nullptr) lv_obj_move_to_index(notice_, 1);
    }
    lv_label_set_text(notice_title_, title);
    lv_label_set_text(notice_detail_, detail != nullptr ? detail : "");
    SetHidden(notice_detail_, detail == nullptr || detail[0] == '\0');
    SetHidden(retry_button_, !retry);
}

void PhotosApp::HideNotice() {
    if (notice_ == nullptr) return;
    lv_obj_delete(notice_);
    notice_ = notice_title_ = notice_detail_ = retry_button_ = nullptr;
}

void PhotosApp::DrawPageDots(lv_event_t* event) {
    auto* self = static_cast<PhotosApp*>(lv_event_get_user_data(event));
    const size_t pages = self->PageCount();
    if (pages < 2 || pages > 8) return;
    auto* object = lv_event_get_target_obj(event);
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    constexpr lv_coord_t kDot = 4;
    constexpr lv_coord_t kActive = 14;
    constexpr lv_coord_t kSpacing = 6;
    const lv_coord_t total = static_cast<lv_coord_t>((pages - 1) * (kDot + kSpacing) + kActive);
    lv_coord_t x = area.x1 + (lv_area_get_width(&area) - total) / 2;
    const lv_coord_t y = area.y1 + (lv_area_get_height(&area) - kDot) / 2;
    lv_draw_rect_dsc_t dot;
    lv_draw_rect_dsc_init(&dot);
    dot.radius = kDot / 2;
    dot.bg_opa = LV_OPA_COVER;
    for (size_t page = 0; page < pages; ++page) {
        const bool active = page == self->current_page_;
        const lv_coord_t width = active ? kActive : kDot;
        dot.bg_color = active ? self->ui_->theme().accent : self->ui_->theme().surface_alt;
        const lv_area_t rect{x, y, static_cast<lv_coord_t>(x + width - 1), static_cast<lv_coord_t>(y + kDot - 1)};
        lv_draw_rect(lv_event_get_layer(event), &dot, &rect);
        x = static_cast<lv_coord_t>(x + width + kSpacing);
    }
}

void PhotosApp::UpdateGridChrome() {
    const size_t pages = PageCount();
    const bool any = pages != 0;
    lv_label_set_text_fmt(title_label_, any ? "相册 · %zu" : "相册", photos_.size());
    SetHidden(previous_button_, pages < 2);
    SetHidden(next_button_, pages < 2);
    SetEnabled(previous_button_, current_page_ > 0);
    SetEnabled(next_button_, current_page_ + 1 < pages);
    SetHidden(page_indicator_, pages < 2 || pages > 8);
    SetHidden(page_label_, pages <= 8);
    if (pages > 8) lv_label_set_text_fmt(page_label_, "%zu / %zu", current_page_ + 1, pages);
    lv_obj_invalidate(page_indicator_);
}

void PhotosApp::UpdateViewerChrome(bool failed) {
    preview_failed_ = failed;
    const auto& photo = photos_[current_photo_index_];
    lv_label_set_text_fmt(count_label_, "%zu / %zu", current_photo_index_ + 1, photos_.size());
    lv_label_set_text(filename_label_, photo.filename.c_str());
    const auto date = FormatPhotoDate(photo.modified_time);
    lv_label_set_text(date_label_, date.c_str());
    SetEnabled(previous_button_, current_photo_index_ > 0);
    SetEnabled(next_button_, current_photo_index_ + 1 < photos_.size());
    // A failed photo keeps the bars so the viewer can move on without a hidden-control guess.
    SetOverlayVisible(failed || overlay_visible_);
    SetHidden(bottom_bar_, failed || !overlay_visible_);
}

void PhotosApp::SetOverlayVisible(bool visible) {
    SetHidden(top_bar_, !visible);
    SetHidden(bottom_bar_, !visible);
    SetHidden(previous_button_, !visible);
    SetHidden(next_button_, !visible);
}

void PhotosApp::RefreshPhotos() {
    ClearGridPage();
    current_page_ = 0;
    current_photo_index_ = 0;
    SetEnabled(refresh_button_, false);
    lv_refr_now(lv_obj_get_display(root_));

    auto scan = rodakos::ImageLibrary::ScanPhotoLibrary(context_->services().file_service());
    scan_status_ = scan.status;
    photos_.clear();
    photos_.reserve(scan.paths.size());
    for (size_t i = 0; i < scan.paths.size(); ++i) {
        auto filename = rodakos::ImageLibrary::Basename(scan.paths[i]);
        const uint64_t modified = i < scan.modified_times.size() ? scan.modified_times[i] : 0;
        photos_.push_back({std::move(scan.paths[i]), std::move(filename), modified});
    }
    ESP_LOGI(TAG, "Found %zu photos", photos_.size());
    SetEnabled(refresh_button_, true);
    BuildGridPage();
}

size_t PhotosApp::PageCount() const {
    return (photos_.size() + kPhotosPerPage - 1) / kPhotosPerPage;
}

void PhotosApp::BuildGridPage() {
    ClearGridPage();
    if (photos_.empty()) {
        const bool error = scan_status_ != rodakos::ImageLibrary::ImageScanStatus::kReady;
        ShowNotice(ScanStatusText(scan_status_),
                   error ? "检查 SD 卡后重试" : "用相机拍一张，或放进 photos 文件夹", error, false);
        UpdateGridChrome();
        return;
    }
    HideNotice();
    const size_t first = current_page_ * kPhotosPerPage;
    const size_t last = std::min(photos_.size(), first + kPhotosPerPage);
    thumbnail_items_.reserve(last - first);
    for (size_t i = first; i < last; ++i) {
        const int slot = static_cast<int>(i - first);
        auto* button = lv_button_create(image_area_);
        lv_obj_remove_style_all(button);
        lv_obj_add_style(button, &tile_style_, 0);
        lv_obj_add_style(button, &tile_pressed_style_, LV_STATE_PRESSED);
        lv_obj_set_size(button, kTileSize, kTileSize);
        lv_obj_set_pos(button, kGridLeft + (slot % kGridCols) * (kTileSize + kTileGap),
                       kGridTop + (slot / kGridCols) * (kTileSize + kTileGap));
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(button, HandleImageInput, LV_EVENT_ALL, this);

        auto* image = lv_image_create(button);
        lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(image, kTileSize, kTileSize);
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CENTER);
        lv_obj_center(image);

        thumbnail_items_.push_back({i, button, image, nullptr, nullptr, ThumbnailState::kPending});
    }
    UpdateGridChrome();
    StartThumbnailTimer();
}

void PhotosApp::StopThumbnailTimer() {
    if (thumbnail_timer_ != nullptr) {
        lv_timer_delete(thumbnail_timer_);
        thumbnail_timer_ = nullptr;
    }
}

void PhotosApp::StartThumbnailTimer() {
    if (thumbnail_timer_ == nullptr) {
        thumbnail_timer_ = lv_timer_create([](lv_timer_t* timer) {
            static_cast<PhotosApp*>(lv_timer_get_user_data(timer))->LoadNextThumbnail();
        }, kThumbnailTimerMs, this);
    }
    if (thumbnail_timer_ != nullptr) {
        lv_timer_resume(thumbnail_timer_);
        lv_timer_ready(thumbnail_timer_);
        return;
    }
    // Without a timer no thumbnail will load; offer a manual retry instead of spinning.
    ShowNotice("缩略图没有加载", nullptr, true, false);
}

void PhotosApp::RetryThumbnails() {
    for (auto& item : thumbnail_items_) {
        if (item.state == ThumbnailState::kFailed) {
            item.state = ThumbnailState::kPending;
            if (item.placeholder != nullptr) lv_obj_delete(item.placeholder);
            item.placeholder = nullptr;
        }
    }
    HideNotice();
    StartThumbnailTimer();
}

void PhotosApp::LoadNextThumbnail() {
    const auto pending = [](const ThumbnailItem& item) {
        return item.state == ThumbnailState::kPending;
    };
    auto item = std::find_if(thumbnail_items_.begin(), thumbnail_items_.end(), pending);
    if (item != thumbnail_items_.end()) {
        // Cover, not fit: the decoder scales to at least the tile on both axes, then the image is
        // centered and the tile clips the overflow.
        auto loaded = rodakos::ImageLibrary::LoadThumbnailDetailed(
            photos_[item->photo_index].path, kTileSize, kTileSize);
        item->state = loaded.status == rodakos::ImageLibrary::ImageLoadStatus::kUnsupported
                          ? ThumbnailState::kUnsupported
                          : ThumbnailState::kFailed;
        if (loaded.image != nullptr) {
            const void* source = loaded.image->GetImageSource();
            lv_image_header_t header = {};
            if (lv_image_decoder_get_info(source, &header) == LV_RESULT_OK && header.w != 0 && header.h != 0) {
                item->thumbnail = std::move(loaded.image);
                item->state = ThumbnailState::kReady;
                lv_image_set_src(item->image, source);
            } else {
                lv_image_cache_drop(source);
            }
        }
        if (item->state != ThumbnailState::kReady) {
            item->placeholder = CreatePlainBox(item->button);
            lv_obj_set_size(item->placeholder, kTileSize, kTileSize);
            auto* icon = CreateGlyph(item->placeholder, ICON_IMAGE, rodakos_theme_text_tertiary());
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, -6);
            auto* format = lv_label_create(item->placeholder);
            lv_label_set_text(format, Extension(photos_[item->photo_index].filename).c_str());
            lv_obj_set_style_text_font(format, &phone_font_12, 0);
            lv_obj_set_style_text_color(format, rodakos_theme_text_tertiary(), 0);
            lv_obj_align(format, LV_ALIGN_BOTTOM_MID, 0, -6);
        }
    }
    if (std::none_of(thumbnail_items_.begin(), thumbnail_items_.end(), pending)) lv_timer_pause(thumbnail_timer_);
}

void PhotosApp::ReleaseCurrentImage(const char* reason) {
    if (current_image_ == nullptr) {
        return;
    }

    LogPhotoHeap("before release", reason);
    const void* source = current_image_->GetImageSource();
    if (photo_img_ != nullptr && lv_obj_is_valid(photo_img_)) {
        lv_image_set_src(photo_img_, nullptr);
    }
    if (source != nullptr) {
        lv_image_cache_drop(source);
    }
    current_image_.reset();
    LogPhotoHeap("after release", reason);
}

void PhotosApp::ShowFullScreen(size_t index) {
    if (index >= photos_.size()) return;
    if (current_view_ != ViewMode::kFullScreen) CreateView(ViewMode::kFullScreen);
    ReleaseCurrentImage("replace");
    HideNotice();
    current_photo_index_ = index;
    const auto& photo = photos_[index];
    UpdateViewerChrome(false);
    lv_refr_now(lv_obj_get_display(root_));

    auto loaded = rodakos::ImageLibrary::LoadImageForDisplayDetailed(photo.path);
    current_image_ = std::move(loaded.image);
    if (current_image_ != nullptr) {
        const void* source = current_image_->GetImageSource();
        lv_image_header_t header = {};
        if (lv_image_decoder_get_info(source, &header) == LV_RESULT_OK && header.w != 0 && header.h != 0) {
            const int32_t scale_w = (ui_->width() * LV_SCALE_NONE) / static_cast<int32_t>(header.w);
            const int32_t scale_h = (ui_->height() * LV_SCALE_NONE) / static_cast<int32_t>(header.h);
            lv_image_set_scale(photo_img_, static_cast<uint32_t>(std::max<int32_t>(1, std::min(scale_w, scale_h))));
            lv_image_set_src(photo_img_, source);
        } else {
            ReleaseCurrentImage("invalid dimensions");
            loaded.status = rodakos::ImageLibrary::ImageLoadStatus::kDecodeFailed;
        }
    }
    if (current_image_ == nullptr) {
        ESP_LOGW(TAG, "Failed to load image: %s", photo.path.c_str());
        ShowNotice("打不开这张照片", LoadStatusText(loaded.status), true, true);
        UpdateViewerChrome(true);
    }
}

void PhotosApp::HandleImageInput(lv_event_t* event) {
    const auto code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST && code != LV_EVENT_CLICKED) return;
    auto* self = static_cast<PhotosApp*>(lv_event_get_user_data(event));
    auto* target = lv_event_get_target_obj(event);
    auto* indev = lv_event_get_indev(event);
    if (code == LV_EVENT_PRESSED) {
        self->swipe_direction_ = 0;
        self->swipe_tracking_ = indev != nullptr && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER;
        if (self->swipe_tracking_) lv_indev_get_point(indev, &self->press_origin_);
    } else if (code == LV_EVENT_PRESS_LOST) {
        self->swipe_tracking_ = false;
    } else if ((code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) && self->swipe_tracking_) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        const int32_t dx = point.x - self->press_origin_.x;
        const int32_t dy = point.y - self->press_origin_.y;
        if (self->swipe_direction_ == 0 && std::abs(dx) >= kSwipeThreshold && 2 * std::abs(dx) >= 3 * std::abs(dy)) {
            self->swipe_direction_ = dx < 0 ? 1 : -1;
        }
        if (code == LV_EVENT_RELEASED) {
            self->swipe_tracking_ = false;
            if (self->swipe_direction_ != 0) {
                const int direction = self->swipe_direction_;
                // A swipe at the boundary must also cancel the tile click.
                lv_indev_reset(indev, target);
                self->Step(direction);
            }
        }
    } else if (code == LV_EVENT_CLICKED && self->swipe_direction_ == 0) {
        if (self->current_view_ == ViewMode::kFullScreen) {
            if (!self->preview_failed_) {
                self->overlay_visible_ = !self->overlay_visible_;
                self->SetOverlayVisible(self->overlay_visible_);
            }
            return;
        }
        if (indev != nullptr && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_point_t point;
            lv_indev_get_point(indev, &point);
            if (!lv_obj_hit_test(target, &point)) return;
        }
        const auto item = std::find_if(self->thumbnail_items_.begin(), self->thumbnail_items_.end(),
                                       [target](const ThumbnailItem& tile) { return tile.button == target; });
        if (item != self->thumbnail_items_.end()) self->ShowFullScreen(item->photo_index);
    }
}

void PhotosApp::Step(int direction) {
    const bool grid = current_view_ == ViewMode::kGrid;
    const size_t index = grid ? current_page_ : current_photo_index_;
    const size_t count = grid ? PageCount() : photos_.size();
    if (direction < 0 ? index == 0 : count == 0 || index >= count - 1) return;
    const size_t next = direction < 0 ? index - 1 : index + 1;
    if (grid) {
        current_page_ = next;
        BuildGridPage();
    } else {
        ShowFullScreen(next);
    }
}

void PhotosApp::Retry() {
    if (current_view_ == ViewMode::kFullScreen) ShowFullScreen(current_photo_index_);
    else if (photos_.empty()) RefreshPhotos();
    else RetryThumbnails();
}

void PhotosApp::BackToGrid() {
    current_page_ = current_photo_index_ / kPhotosPerPage;
    lv_obj_set_style_bg_color(root_, ui_->theme().background, 0);
    overlay_visible_ = true;
    CreateView(ViewMode::kGrid);
    BuildGridPage();
}

void PhotosApp::NavigateHome() {
    lv_async_call_cancel(DeferReturnHome, context_);
    if (lv_async_call(DeferReturnHome, context_) != LV_RESULT_OK)
        ui_->ShowToastUnlocked("暂时无法返回桌面");
}

void RegisterPhotosApp(PhoneAppRegistry& registry) {
    PhoneAppDescriptor desc;
    desc.id = "photos";
    desc.title = "Photos";
    desc.icon = ICON_IMAGE;
    desc.category = PhoneAppCategory::kMedia;
    desc.icon_tint = PhoneAppIconTint::kWarm;
    desc.capabilities = PhoneCapability::kStorage;
    desc.show_on_home = true;
    desc.create = []() -> std::unique_ptr<PhoneApp> {
        return std::make_unique<PhotosApp>();
    };
    registry.Register(std::move(desc));
}

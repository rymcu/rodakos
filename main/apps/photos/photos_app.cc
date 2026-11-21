#include "photos_app.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_layout.h"
#include "phone_ui/rodakos_theme.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <algorithm>
#include <cstdlib>
#include <utility>

namespace {
constexpr const char* TAG = "PhotosApp";
constexpr int kGridCols = 3;
constexpr int kGridRows = 2;
constexpr size_t kPhotosPerPage = kGridCols * kGridRows;
constexpr lv_coord_t kThumbnailSize = 68;
constexpr lv_coord_t kThumbnailGap = 8;
constexpr lv_coord_t kGridWidth = kGridCols * kThumbnailSize + (kGridCols - 1) * kThumbnailGap;
constexpr lv_coord_t kGridHeight = kGridRows * kThumbnailSize + (kGridRows - 1) * kThumbnailGap;
constexpr uint32_t kThumbnailTimerMs = 120;
constexpr lv_coord_t kPagerHeight = 44;
constexpr lv_coord_t kPagerButtonWidth = 72;
constexpr lv_coord_t kButtonHeight = 40;

const char* ScanStatusText(rodakos::ImageLibrary::ImageScanStatus status) {
    using Status = rodakos::ImageLibrary::ImageScanStatus;
    switch (status) {
        case Status::kReady: return "暂无照片 · 将图片放入 /photos";
        case Status::kServiceUnavailable: return "存储服务不可用";
        case Status::kStorageUnavailable: return "SD 卡不可用";
        case Status::kDirectoryMissing: return "相册目录不可用";
        case Status::kReadFailed: return "无法读取相册";
    }
    return "无法读取相册";
}

const char* LoadStatusText(rodakos::ImageLibrary::ImageLoadStatus status) {
    using Status = rodakos::ImageLibrary::ImageLoadStatus;
    switch (status) {
        case Status::kUnsupported: return "不支持此图片格式";
        case Status::kReadFailed: return "无法读取照片";
        case Status::kInsufficientMemory: return "图片内存不足";
        case Status::kLoaded:
        case Status::kDecodeFailed: return "无法解码照片";
    }
    return "无法解码照片";
}

void DeferReturnHome(void* user_data) {
    static_cast<PhoneAppContext*>(user_data)->navigation().ReturnHome();
}

void SetHidden(lv_obj_t* obj, bool hidden) {
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void SetEnabled(lv_obj_t* obj, bool enabled) {
    if (enabled) lv_obj_clear_state(obj, LV_STATE_DISABLED);
    else lv_obj_add_state(obj, LV_STATE_DISABLED);
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

    lv_style_init(&tile_style_);
    lv_style_set_bg_color(&tile_style_, ui_->theme().surface);
    lv_style_set_bg_opa(&tile_style_, LV_OPA_COVER);
    lv_style_set_radius(&tile_style_, 8);
    lv_style_init(&tile_pressed_style_);
    lv_style_set_bg_color(&tile_pressed_style_, ui_->theme().surface_alt);

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
    if (current_view_ == ViewMode::kGrid) lv_obj_clean(image_area_);
}

void PhotosApp::ClearView() {
    ClearGridPage();
    ReleaseCurrentImage("leave view");
    lv_obj_clean(root_);
    image_area_ = content_status_ = photo_img_ = filename_label_ = nullptr;
    refresh_button_ = preview_retry_button_ = nullptr;
    status_label_ = previous_page_button_ = next_page_button_ = nullptr;
    swipe_direction_ = 0;
    swipe_tracking_ = false;
}

void PhotosApp::CreateView(ViewMode mode) {
    if (current_view_ != ViewMode::kNone) ClearView();
    current_view_ = mode;
    const bool grid = mode == ViewMode::kGrid;
    const auto back = [](lv_event_t* e) {
        auto* self = static_cast<PhotosApp*>(lv_event_get_user_data(e));
        if (self->current_view_ == ViewMode::kFullScreen) self->BackToGrid();
        else self->NavigateHome();
    };
    const auto refresh = [](lv_event_t* e) {
        static_cast<PhotosApp*>(lv_event_get_user_data(e))->RefreshPhotos();
    };
    CreateAppHeader(root_, grid ? "相册" : "", back, refresh, this, &filename_label_,
                    grid ? ICON_REFRESH : nullptr, &refresh_button_);
    if (grid) {
        lv_obj_set_style_opa(refresh_button_, LV_OPA_50, LV_STATE_DISABLED);
    } else {
        lv_obj_set_width(filename_label_, ui_->width() - 2 * (kRodakosAppHeaderPadding + 48));
        lv_obj_set_height(filename_label_, lv_font_get_line_height(&phone_font_18));
        lv_label_set_long_mode(filename_label_, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(filename_label_, LV_TEXT_ALIGN_CENTER, 0);
    }

    image_area_ = lv_obj_create(root_);
    lv_obj_remove_style_all(image_area_);
    const lv_coord_t height = ui_->height() - kRodakosAppHeaderHeight - kPagerHeight;
    lv_obj_set_size(image_area_, ui_->width(), height);
    lv_obj_align(image_area_, LV_ALIGN_TOP_MID, 0, kRodakosAppHeaderHeight);
    lv_obj_clear_flag(image_area_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(image_area_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(image_area_, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(image_area_, HandleImageInput, LV_EVENT_ALL, this);
    if (grid) {
        lv_obj_set_style_pad_hor(image_area_, (ui_->width() - kGridWidth) / 2, 0);
        lv_obj_set_style_pad_ver(image_area_, (height - kGridHeight) / 2, 0);
    } else {
        photo_img_ = lv_image_create(image_area_);
        lv_obj_clear_flag(photo_img_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(photo_img_, ui_->width(), height);
        lv_image_set_inner_align(photo_img_, LV_IMAGE_ALIGN_CENTER);
        lv_obj_center(photo_img_);
    }
    CreatePager();
}

void PhotosApp::CreatePager() {
    auto* pager = lv_obj_create(root_);
    lv_obj_remove_style_all(pager);
    lv_obj_set_size(pager, LV_PCT(100), kPagerHeight);
    lv_obj_set_style_bg_color(pager, ui_->theme().surface, 0);
    lv_obj_set_style_bg_opa(pager, LV_OPA_COVER, 0);
    lv_obj_align(pager, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(pager, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(pager, &phone_font_14, 0);

    previous_page_button_ = RodakosCreateHeaderIconButton(pager, ICON_CHEVRON_LEFT);
    next_page_button_ = RodakosCreateHeaderIconButton(pager, ICON_CHEVRON_RIGHT);
    lv_obj_set_size(previous_page_button_, kPagerButtonWidth, kButtonHeight);
    lv_obj_set_size(next_page_button_, kPagerButtonWidth, kButtonHeight);
    lv_obj_set_ext_click_area(previous_page_button_, 0);
    lv_obj_set_ext_click_area(next_page_button_, 0);
    lv_obj_set_style_opa(previous_page_button_, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_set_style_opa(next_page_button_, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_align(previous_page_button_, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_align(next_page_button_, LV_ALIGN_RIGHT_MID, -6, 0);
    const auto step = [](lv_event_t* e) {
        auto* self = static_cast<PhotosApp*>(lv_event_get_user_data(e));
        self->Step(lv_event_get_target_obj(e) == self->previous_page_button_ ? -1 : 1);
    };
    lv_obj_add_event_cb(previous_page_button_, step, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(next_page_button_, step, LV_EVENT_CLICKED, this);

    auto status = CreatePhoneStatusBlock(*ui_, pager, "", "重试");
    status_label_ = status.label;
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    preview_retry_button_ = status.button;
    lv_obj_add_event_cb(preview_retry_button_, [](lv_event_t* e) {
        auto* self = static_cast<PhotosApp*>(lv_event_get_user_data(e));
        if (self->current_view_ == ViewMode::kFullScreen) self->ShowFullScreen(self->current_photo_index_);
        else if (self->photos_.empty()) self->RefreshPhotos();
        else self->RetryThumbnails();
    }, LV_EVENT_CLICKED, this);
}

void PhotosApp::UpdatePager(size_t index, size_t count, const char* action, bool show_count) {
    const bool any = count != 0;
    SetHidden(previous_page_button_, !any);
    SetHidden(next_page_button_, !any);
    SetEnabled(previous_page_button_, index > 0);
    SetEnabled(next_page_button_, any && index < count - 1);
    SetHidden(status_label_, !any || !show_count);
    if (any && show_count) lv_label_set_text_fmt(status_label_, "%zu / %zu", index + 1, count);
    SetHidden(preview_retry_button_, action == nullptr);
    if (action != nullptr) lv_label_set_text(lv_obj_get_child(preview_retry_button_, 0), action);
    const bool both = any && show_count && action != nullptr;
    lv_obj_set_width(status_label_, both ? 62 : 154);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, both ? -46 : 0, 0);
    lv_obj_align(preview_retry_button_, LV_ALIGN_CENTER, both ? 32 : 0, 0);
}

void PhotosApp::SetContentStatus(const char* message, bool error) {
    if (message == nullptr) {
        if (content_status_ != nullptr) lv_obj_delete(content_status_);
        content_status_ = nullptr;
        return;
    }
    if (content_status_ == nullptr) {
        content_status_ = CreatePhoneStatusBlock(*ui_, root_, message).label;
        lv_obj_set_width(content_status_, ui_->width() - 24);
        lv_obj_center(content_status_);
    } else {
        lv_label_set_text(content_status_, message);
    }
    lv_obj_set_style_text_color(content_status_, error ? rodakos_theme_error() : ui_->theme().text_secondary, 0);
}

void PhotosApp::RefreshPhotos() {
    ClearGridPage();
    current_page_ = 0;
    current_photo_index_ = 0;
    SetContentStatus("正在读取相册");
    SetEnabled(refresh_button_, false);
    UpdatePager(0, 0);
    lv_refr_now(lv_obj_get_display(root_));

    auto scan = rodakos::ImageLibrary::ScanPhotoLibrary(context_->services().file_service());
    scan_status_ = scan.status;
    photos_.clear();
    photos_.reserve(scan.paths.size());
    for (auto& path : scan.paths) {
        auto filename = rodakos::ImageLibrary::Basename(path);
        photos_.push_back({std::move(path), std::move(filename)});
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
        SetContentStatus(ScanStatusText(scan_status_), error);
        UpdatePager(0, 0, error ? "重试" : "刷新");
        return;
    }
    SetContentStatus(nullptr);
    const size_t first = current_page_ * kPhotosPerPage;
    const size_t last = std::min(photos_.size(), first + kPhotosPerPage);
    thumbnail_items_.reserve(last - first);
    for (size_t i = first; i < last; ++i) {
        auto* button = lv_btn_create(image_area_);
        lv_obj_remove_style_all(button);
        lv_obj_add_style(button, &tile_style_, 0);
        lv_obj_add_style(button, &tile_pressed_style_, LV_STATE_PRESSED);
        lv_obj_set_size(button, kThumbnailSize, kThumbnailSize);
        rodakos_layout_grid_place(image_area_, button, static_cast<int>(i - first), kGridCols,
                                  kThumbnailSize, kThumbnailSize, kThumbnailGap, kThumbnailGap);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(button, HandleImageInput, LV_EVENT_ALL, this);

        auto* image = lv_image_create(button);
        lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(image, kThumbnailSize, kThumbnailSize);
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CENTER);
        lv_obj_center(image);

        auto* label = PhoneCreateLabel(*ui_, button, photos_[i].filename.c_str());
        lv_obj_set_width(label, kThumbnailSize - 8);
        lv_obj_set_height(label, kThumbnailSize - 8);
        lv_obj_set_style_text_font(label, &phone_font_12, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(label);

        thumbnail_items_.push_back({i, button, image, label, nullptr, ThumbnailState::kPending});
    }
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
    }
    UpdateThumbnailStatus();
}

void PhotosApp::RetryThumbnails() {
    for (auto& item : thumbnail_items_) {
        if (item.state == ThumbnailState::kFailed) item.state = ThumbnailState::kPending;
    }
    StartThumbnailTimer();
}

void PhotosApp::UpdateThumbnailStatus() {
    const bool failed = thumbnail_timer_ == nullptr ||
        std::any_of(thumbnail_items_.begin(), thumbnail_items_.end(),
                    [](const ThumbnailItem& item) { return item.state == ThumbnailState::kFailed; });
    UpdatePager(current_page_, PageCount(), failed ? "重试缩略图" : nullptr);
}

void PhotosApp::LoadNextThumbnail() {
    const auto pending = [](const ThumbnailItem& item) {
        return item.state == ThumbnailState::kPending;
    };
    auto item = std::find_if(thumbnail_items_.begin(), thumbnail_items_.end(), pending);
    if (item != thumbnail_items_.end()) {
        auto loaded = rodakos::ImageLibrary::LoadThumbnailDetailed(
            photos_[item->photo_index].path, kThumbnailSize, kThumbnailSize);
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
                SetHidden(item->label, true);
            } else {
                lv_image_cache_drop(source);
            }
        }
        if (item->state == ThumbnailState::kFailed) UpdateThumbnailStatus();
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
    current_photo_index_ = index;
    const auto& photo = photos_[index];
    lv_label_set_text(filename_label_, photo.filename.c_str());
    SetContentStatus("正在打开照片");
    UpdatePager(0, 0);
    lv_refr_now(lv_obj_get_display(root_));

    auto loaded = rodakos::ImageLibrary::LoadImageForDisplayDetailed(photo.path);
    current_image_ = std::move(loaded.image);
    if (current_image_ != nullptr) {
        const void* source = current_image_->GetImageSource();
        lv_image_header_t header = {};
        if (lv_image_decoder_get_info(source, &header) == LV_RESULT_OK && header.w != 0 && header.h != 0) {
            const int32_t scale_w = (ui_->width() * LV_SCALE_NONE) / static_cast<int32_t>(header.w);
            const int32_t height = ui_->height() - kRodakosAppHeaderHeight - kPagerHeight;
            const int32_t scale_h = (height * LV_SCALE_NONE) / static_cast<int32_t>(header.h);
            lv_image_set_scale(photo_img_, static_cast<uint32_t>(std::max<int32_t>(1, std::min(scale_w, scale_h))));
            lv_image_set_src(photo_img_, source);
        } else {
            ReleaseCurrentImage("invalid dimensions");
            loaded.status = rodakos::ImageLibrary::ImageLoadStatus::kDecodeFailed;
        }
    }
    if (current_image_ == nullptr) {
        ESP_LOGW(TAG, "Failed to load image: %s", photo.path.c_str());
        SetContentStatus(LoadStatusText(loaded.status), true);
        UpdatePager(index, photos_.size(), "重试", false);
    } else {
        SetContentStatus(nullptr);
        UpdatePager(index, photos_.size());
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
        if (self->swipe_direction_ == 0 && std::abs(dx) >= 40 && 2 * std::abs(dx) >= 3 * std::abs(dy)) {
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
    } else if (code == LV_EVENT_CLICKED && self->current_view_ == ViewMode::kGrid && self->swipe_direction_ == 0) {
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

void PhotosApp::BackToGrid() {
    current_page_ = current_photo_index_ / kPhotosPerPage;
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

    registry.Register(desc);
}

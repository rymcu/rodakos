#pragma once

#include "phone_os/phone_app.h"
#include "phone_ui/image_library.h"

#include <lvgl.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class PhoneAppContext;
class PhoneUi;
class PhoneAppRegistry;

class PhotosApp : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override {}
    void OnPause() override {}
    void OnDestroy() override;
    bool OnBackRequested() override;

private:
    enum class ViewMode { kNone, kGrid, kFullScreen };
    enum class ThumbnailState { kPending, kReady, kUnsupported, kFailed };

    struct PhotoEntry {
        std::string path;
        std::string filename;
        uint64_t modified_time;
    };

    struct ThumbnailItem {
        size_t photo_index;
        lv_obj_t* button;
        lv_obj_t* image;
        lv_obj_t* placeholder;
        std::shared_ptr<rodakos::LvglImage> thumbnail;
        ThumbnailState state;
    };

    // Grid view: header, tile grid, page bar. Fullscreen view: photo with overlay bars that a tap
    // hides. Only one of the two trees exists at a time.
    void CreateView(ViewMode mode);
    void ClearView();
    void CreateGridChrome();
    void CreateViewerChrome();
    void ShowNotice(const char* title, const char* detail, bool retry, bool dark);
    void HideNotice();
    void UpdateGridChrome();
    void UpdateViewerChrome(bool failed);
    void SetOverlayVisible(bool visible);
    void RefreshPhotos();
    size_t PageCount() const;
    void ClearGridPage();
    void BuildGridPage();
    void StartThumbnailTimer();
    void StopThumbnailTimer();
    void RetryThumbnails();
    void LoadNextThumbnail();
    void ShowFullScreen(size_t index);
    void ReleaseCurrentImage(const char* reason);
    void BackToGrid();
    void NavigateHome();
    void Step(int direction);
    void Retry();
    static void HandleImageInput(lv_event_t* event);
    static void DrawPageDots(lv_event_t* event);

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    lv_obj_t* root_ = nullptr;
    ViewMode current_view_ = ViewMode::kNone;

    // Shared by both views.
    lv_obj_t* image_area_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* back_button_ = nullptr;
    lv_obj_t* previous_button_ = nullptr;
    lv_obj_t* next_button_ = nullptr;
    lv_obj_t* notice_ = nullptr;
    lv_obj_t* notice_title_ = nullptr;
    lv_obj_t* notice_detail_ = nullptr;
    lv_obj_t* retry_button_ = nullptr;

    // Grid only.
    lv_obj_t* refresh_button_ = nullptr;
    lv_obj_t* page_indicator_ = nullptr;
    lv_obj_t* page_label_ = nullptr;
    lv_style_t tile_style_ = {};
    lv_style_t tile_pressed_style_ = {};

    // Fullscreen only.
    lv_obj_t* photo_img_ = nullptr;
    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* bottom_bar_ = nullptr;
    lv_obj_t* count_label_ = nullptr;
    lv_obj_t* filename_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    bool overlay_visible_ = true;
    bool preview_failed_ = false;

    lv_timer_t* thumbnail_timer_ = nullptr;
    std::vector<PhotoEntry> photos_;
    std::vector<ThumbnailItem> thumbnail_items_;
    std::shared_ptr<rodakos::LvglImage> current_image_;
    size_t current_photo_index_ = 0;
    size_t current_page_ = 0;
    rodakos::ImageLibrary::ImageScanStatus scan_status_ = rodakos::ImageLibrary::ImageScanStatus::kReady;
    lv_point_t press_origin_ = {};
    bool swipe_tracking_ = false;
    int swipe_direction_ = 0;
};

void RegisterPhotosApp(PhoneAppRegistry& registry);

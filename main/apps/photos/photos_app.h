#pragma once

#include "phone_os/phone_app.h"
#include "phone_ui/image_library.h"

#include <lvgl.h>
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
    };

    struct ThumbnailItem {
        size_t photo_index;
        lv_obj_t* button;
        lv_obj_t* image;
        lv_obj_t* label;
        std::shared_ptr<rodakos::LvglImage> thumbnail;
        ThumbnailState state;
    };

    void CreateView(ViewMode mode);
    void ClearView();
    void CreatePager();
    void UpdatePager(size_t index, size_t count, const char* action = nullptr, bool show_count = true);
    void SetContentStatus(const char* message, bool error = false);
    void RefreshPhotos();
    size_t PageCount() const;
    void ClearGridPage();
    void BuildGridPage();
    void StartThumbnailTimer();
    void StopThumbnailTimer();
    void RetryThumbnails();
    void LoadNextThumbnail();
    void UpdateThumbnailStatus();
    void ShowFullScreen(size_t index);
    void ReleaseCurrentImage(const char* reason);
    void BackToGrid();
    void NavigateHome();
    void Step(int direction);
    static void HandleImageInput(lv_event_t* event);

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    lv_obj_t* root_ = nullptr;
    ViewMode current_view_ = ViewMode::kNone;
    lv_style_t tile_style_ = {};
    lv_style_t tile_pressed_style_ = {};

    lv_obj_t* image_area_ = nullptr;
    lv_obj_t* content_status_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* refresh_button_ = nullptr;
    lv_obj_t* previous_page_button_ = nullptr;
    lv_obj_t* next_page_button_ = nullptr;
    lv_obj_t* preview_retry_button_ = nullptr;
    lv_obj_t* photo_img_ = nullptr;
    lv_obj_t* filename_label_ = nullptr;
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

#pragma once

#include "phone_os/phone_app.h"
#include "rodakos_adapters/file_service.h"
#include "phone_ui/image_library.h"
#include <lvgl.h>
#include <memory>
#include <vector>
#include <string>

class PhoneAppContext;
class PhoneUi;
class PhoneAppRegistry;

/**
 * @brief 照片 App
 *
 * 功能：
 * - 浏览 SD 卡中的图片文件（jpg, png, bmp）
 * - 网格缩略图视图
 * - 全屏查看
 * - 图片导航
 */
class PhotosApp : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override {}
    void OnPause() override {}
    void OnDestroy() override;

    // Public for event callbacks
    void ShowFullScreen(size_t index);

private:
    enum class ViewMode {
        kNone,
        kGrid,      // 网格视图
        kFullScreen // 全屏查看
    };

    struct PhotoEntry {
        std::string path;
        std::string filename;
        size_t size;
    };

    struct ThumbnailItem {
        lv_obj_t* button = nullptr;
        lv_obj_t* image = nullptr;
        lv_obj_t* label = nullptr;
        std::shared_ptr<rodakos::LvglImage> thumbnail;
        bool thumbnail_unavailable = false;
    };

    // 页面管理
    void ShowGridView();
    void CreateGridView();
    void CreateFullScreenView();

    // 图片扫描
    void ScanPhotos();

    // 导航
    void ShowNextPhoto();
    void ShowPreviousPhoto();
    void BackToGrid();
    void NavigateBack();
    void NavigateHome();

    // 缩略图懒加载
    void ScheduleThumbnailUpdate();
    void UpdateVisibleThumbnails();
    void ReleaseThumbnail(ThumbnailItem& item);
    void ReleaseAllThumbnails();
    void StopThumbnailTimer();
    static void ThumbnailTimerCallback(lv_timer_t* timer);

    // 大图内存释放
    void ReleaseCurrentImage(const char* reason);

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    lv_obj_t* root_ = nullptr;
    ViewMode current_view_ = ViewMode::kNone;

    // 网格视图控件
    lv_obj_t* grid_body_ = nullptr;
    lv_obj_t* grid_container_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_timer_t* thumbnail_timer_ = nullptr;
    std::vector<ThumbnailItem> thumbnail_items_;

    // 全屏视图控件
    lv_obj_t* fullscreen_body_ = nullptr;
    lv_obj_t* photo_img_ = nullptr;
    lv_obj_t* filename_label_ = nullptr;
    std::shared_ptr<rodakos::LvglImage> current_image_;

    std::vector<PhotoEntry> photos_;
    size_t current_photo_index_ = 0;
};

void RegisterPhotosApp(PhoneAppRegistry& registry);

#include "photos_app.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/rodakos_theme.h"
#include "phone_ui/image_library.h"

#include <esp_log.h>
#include <algorithm>
#include <cctype>
#include <memory>
#include <utility>

namespace {
constexpr const char* TAG = "PhotosApp";
constexpr const char* kPhotosPath = "/DCIM";  // 标准相机目录
constexpr const char* kRodakosPhotosPath = "/photos";

// 网格布局常量
constexpr int kGridCols = 3;
constexpr int kGridRows = 3;
constexpr lv_coord_t kThumbnailSize = 84;
constexpr lv_coord_t kThumbnailGap = 8;
constexpr int32_t kPhotoAreaWidth = 300;
constexpr int32_t kPhotoAreaHeight = 146;
constexpr lv_coord_t kPhotoAreaTop = 42;
constexpr lv_coord_t kFullscreenTopBarHeight = 40;

// UI 常量 - 参考 xiaozhi image_app 的圆形按钮设计
constexpr lv_coord_t kRoundButtonSize = 38;

lv_obj_t* CreateRoundButton(lv_obj_t* parent, const char* icon_text, bool is_primary) {
    auto* button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, kRoundButtonSize, kRoundButtonSize);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);

    // 使用全局主题颜色
    lv_obj_set_style_bg_color(button, is_primary ? rodakos_theme_primary() : rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);

    // 按压效果 - 轻微变亮 + 外发光 + 位移
    lv_obj_set_style_bg_opa(button, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(button, 2, LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(button, rodakos_theme_primary(), LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(button, 2, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    auto* label = lv_label_create(button);
    lv_label_set_text(label, icon_text);
    lv_obj_set_style_text_font(label, PhoneIconFont(), 0);
    lv_obj_set_style_text_color(label, rodakos_theme_text_primary(), 0);
    lv_obj_center(label);
    return button;
}

struct PhotoButtonPayload {
    PhotosApp* app = nullptr;
    size_t index = 0;
};

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        lv_indev_reset(nullptr, nullptr);
        context->navigation().ReturnHome();
    }
}

void OnPhotoClicked(lv_event_t* e) {
    auto* payload = static_cast<PhotoButtonPayload*>(lv_event_get_user_data(e));
    if (payload != nullptr && payload->app != nullptr) {
        payload->app->ShowFullScreen(payload->index);
    }
}

void OnPhotoButtonDelete(lv_event_t* e) {
    auto* payload = static_cast<PhotoButtonPayload*>(lv_event_get_user_data(e));
    delete payload;
}

void ShowImageLoadError(lv_obj_t* image, lv_obj_t* label, const char* message) {
    if (image != nullptr) {
        lv_image_set_src(image, nullptr);
    }
    if (label != nullptr) {
        lv_label_set_text(label, message);
    }
}

}  // namespace

PhotosApp::~PhotosApp() {
    OnDestroy();
}

bool PhotosApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();

    // Probe storage before taking the LVGL lock; SD card init can timeout when
    // no card is inserted.
    ScanPhotos();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    // 根容器
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // 显示网格视图
    ShowGridView();

    ESP_LOGI(TAG, "Photos app initialized with %zu photos", photos_.size());
    return true;
}

void PhotosApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            if (root_ != nullptr && lv_obj_is_valid(root_)) {
                lv_obj_delete(root_);
            }
        }
    }
    root_ = nullptr;
    grid_body_ = nullptr;
    grid_container_ = nullptr;
    status_label_ = nullptr;
    fullscreen_body_ = nullptr;
    photo_img_ = nullptr;
    filename_label_ = nullptr;
    current_image_.reset();
    context_ = nullptr;
    ui_ = nullptr;
}

void PhotosApp::ScanPhotos() {
    photos_.clear();

    auto* fs = context_->services().file_service();
    if (fs == nullptr) {
        ESP_LOGW(TAG, "File service not available");
        return;
    }
    if (!fs->IsMounted() && !fs->Init()) {
        ESP_LOGW(TAG, "SD card not available");
        return;
    }

    std::vector<std::string> image_paths;
    auto append_scanned_images = [&](const char* directory, int max_depth) {
        if (!fs->Exists(directory)) {
            return;
        }
        auto scanned = rodakos::ImageLibrary::ScanImagesWithFileService(fs, directory, max_depth);
        for (auto& path : scanned) {
            if (std::find(image_paths.begin(), image_paths.end(), path) == image_paths.end()) {
                image_paths.push_back(std::move(path));
            }
        }
    };

    append_scanned_images(kRodakosPhotosPath, 3);
    append_scanned_images(kPhotosPath, 3);

    // 如果常用照片目录为空，扫描根目录
    if (image_paths.empty()) {
        image_paths = rodakos::ImageLibrary::ScanImagesWithFileService(fs, "/", 2);
    }

    // 转换为 PhotoEntry
    for (const auto& path : image_paths) {
        PhotoEntry entry;
        entry.path = path;
        entry.filename = rodakos::ImageLibrary::Basename(path);
        entry.size = fs->GetFileSize(path);
        photos_.push_back(entry);
    }

    ESP_LOGI(TAG, "Found %zu photos", photos_.size());
}

void PhotosApp::ShowFullScreen(size_t index) {
    if (index >= photos_.size()) {
        return;
    }

    current_photo_index_ = index;
    current_view_ = ViewMode::kFullScreen;

    // 隐藏网格视图
    if (grid_body_ != nullptr) {
        lv_obj_add_flag(grid_body_, LV_OBJ_FLAG_HIDDEN);
    }

    // 创建或显示全屏视图
    if (fullscreen_body_ == nullptr) {
        CreateFullScreenView();
    } else {
        lv_obj_clear_flag(fullscreen_body_, LV_OBJ_FLAG_HIDDEN);
    }

    // 加载图片
    const auto& photo = photos_[current_photo_index_];
    ESP_LOGI(TAG, "Loading photo: %s", photo.path.c_str());

    // 更新文件名（左上角）
    lv_label_set_text(filename_label_, photo.filename.c_str());

    // 使用 ImageLibrary 加载图片
    current_image_ = rodakos::ImageLibrary::LoadImageForDisplay(photo.path);
    if (current_image_ != nullptr) {
        const void* image_source = current_image_->GetImageSource();
        lv_image_header_t header = {};
        if (lv_image_decoder_get_info(image_source, &header) != LV_RESULT_OK ||
            header.w == 0 || header.h == 0) {
            ESP_LOGW(TAG, "Unsupported image: %s", photo.path.c_str());
            current_image_.reset();
            ShowImageLoadError(photo_img_, filename_label_, "Unsupported image");
            return;
        }

        const int32_t scale_w = (kPhotoAreaWidth * LV_SCALE_NONE) / static_cast<int32_t>(header.w);
        const int32_t scale_h = (kPhotoAreaHeight * LV_SCALE_NONE) / static_cast<int32_t>(header.h);
        const int32_t scale = std::max<int32_t>(1, std::min(scale_w, scale_h));

        ESP_LOGI(TAG, "Displaying image: %s (%ux%u, cf=%u, scale=%d)",
                 photo.path.c_str(),
                 static_cast<unsigned>(header.w),
                 static_cast<unsigned>(header.h),
                 static_cast<unsigned>(header.cf),
                 static_cast<int>(scale));

        lv_image_set_src(photo_img_, nullptr);
        lv_obj_set_size(photo_img_, kPhotoAreaWidth, kPhotoAreaHeight);
        lv_image_set_inner_align(photo_img_, LV_IMAGE_ALIGN_CENTER);
        lv_image_set_scale(photo_img_, static_cast<uint32_t>(scale));
        lv_image_set_src(photo_img_, image_source);
        lv_obj_align(photo_img_, LV_ALIGN_TOP_MID, 0, kPhotoAreaTop);
    } else {
        ESP_LOGW(TAG, "Failed to load image: %s", photo.path.c_str());
        ShowImageLoadError(photo_img_, filename_label_, "Failed to load image");
        return;
    }
}

void PhotosApp::ShowGridView() {
    if (current_view_ == ViewMode::kGrid) {
        return;
    }

    current_view_ = ViewMode::kGrid;

    // 隐藏全屏视图
    if (fullscreen_body_ != nullptr) {
        lv_obj_add_flag(fullscreen_body_, LV_OBJ_FLAG_HIDDEN);
    }

    // 创建或显示网格视图
    if (grid_body_ == nullptr) {
        CreateGridView();
    } else {
        lv_obj_clear_flag(grid_body_, LV_OBJ_FLAG_HIDDEN);
    }
}

void PhotosApp::CreateGridView() {
    grid_body_ = lv_obj_create(root_);
    lv_obj_remove_style_all(grid_body_);
    lv_obj_set_size(grid_body_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(grid_body_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(grid_body_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(grid_body_, 0, 0);

    // 标题栏
    auto* title_bar = lv_obj_create(grid_body_);
    lv_obj_remove_style_all(title_bar);
    lv_obj_set_size(title_bar, LV_PCT(100), 44);
    lv_obj_set_style_bg_color(title_bar, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(title_bar, 12, 0);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);

    auto* back_btn = RodakosCreateHeaderIconButton(title_bar, FONT_AWESOME_ARROW_LEFT);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        auto* self = static_cast<PhotosApp*>(lv_event_get_user_data(e));
        self->NavigateBack();
    }, LV_EVENT_CLICKED, this);

    auto* home_btn = RodakosCreateHeaderIconButton(title_bar, FONT_AWESOME_HOUSE);
    lv_obj_align(home_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(home_btn, [](lv_event_t* e) {
        auto* self = static_cast<PhotosApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, LV_EVENT_CLICKED, this);

    // 标题
    auto* title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "Photos");
    lv_obj_set_style_text_color(title_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(title_label, &phone_font_14, 0);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 0);

    // 状态标签
    status_label_ = lv_label_create(title_bar);
    char status_text[32];
    snprintf(status_text, sizeof(status_text), "%zu photos", photos_.size());
    lv_label_set_text(status_label_, status_text);
    lv_obj_set_style_text_color(status_label_, rodakos_theme_text_secondary(), 0);
    lv_obj_set_style_text_font(status_label_, &phone_font_12, 0);
    lv_obj_align(status_label_, LV_ALIGN_RIGHT_MID, -48, 0);

    // 可滚动容器
    grid_container_ = lv_obj_create(grid_body_);
    lv_obj_remove_style_all(grid_container_);
    lv_obj_set_size(grid_container_, 292, 196);
    lv_obj_align(grid_container_, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_style_pad_all(grid_container_, 4, 0);
    lv_obj_set_flex_flow(grid_container_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid_container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid_container_, kThumbnailGap, 0);
    lv_obj_set_style_pad_column(grid_container_, kThumbnailGap, 0);

    // 创建缩略图网格
    size_t max_display = std::min(photos_.size(), static_cast<size_t>(kGridCols * kGridRows));
    for (size_t i = 0; i < max_display; ++i) {
        const auto& photo = photos_[i];

        auto* btn = lv_btn_create(grid_container_);
        lv_obj_set_size(btn, kThumbnailSize, kThumbnailSize);
        lv_obj_set_style_bg_color(btn, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_border_color(btn, rodakos_theme_primary(), LV_STATE_PRESSED);
        lv_obj_set_style_translate_y(btn, 1, LV_STATE_PRESSED);

        // TODO: 加载缩略图
        // auto* img = lv_img_create(btn);
        // lv_img_set_src(img, photo.path.c_str());
        // lv_obj_center(img);

        // 临时显示文件名
        auto* name_label = lv_label_create(btn);
        lv_label_set_text(name_label, photo.filename.c_str());
        lv_obj_set_width(name_label, kThumbnailSize - 8);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(name_label, rodakos_theme_text_primary(), 0);
        lv_obj_set_style_text_font(name_label, &phone_font_12, 0);
        lv_obj_center(name_label);

        auto* payload = new PhotoButtonPayload{.app = this, .index = i};
        lv_obj_add_event_cb(btn, OnPhotoClicked, LV_EVENT_CLICKED, payload);
        lv_obj_add_event_cb(btn, OnPhotoButtonDelete, LV_EVENT_DELETE, payload);
    }

    // 如果没有照片，显示提示
    if (photos_.empty()) {
        auto* empty_label = lv_label_create(grid_container_);
        lv_label_set_text(empty_label, "No photos found\n\nInsert SD card with photos");
        lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(empty_label, rodakos_theme_text_secondary(), 0);
        lv_obj_set_style_text_font(empty_label, &phone_font_14, 0);
        lv_obj_center(empty_label);
    }
}

void PhotosApp::CreateFullScreenView() {
    fullscreen_body_ = lv_obj_create(root_);
    lv_obj_remove_style_all(fullscreen_body_);
    lv_obj_set_size(fullscreen_body_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(fullscreen_body_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(fullscreen_body_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(fullscreen_body_, 0, 0);

    auto* top_bar = lv_obj_create(fullscreen_body_);
    lv_obj_remove_style_all(top_bar);
    lv_obj_set_size(top_bar, LV_PCT(100), kFullscreenTopBarHeight);
    lv_obj_set_style_bg_color(top_bar, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(top_bar, 12, 0);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);

    auto* close_btn = RodakosCreateHeaderIconButton(top_bar, FONT_AWESOME_ARROW_LEFT);
    lv_obj_align(close_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        auto* self = static_cast<PhotosApp*>(lv_event_get_user_data(e));
        self->BackToGrid();
    }, LV_EVENT_CLICKED, this);

    filename_label_ = lv_label_create(top_bar);
    lv_label_set_text(filename_label_, "");
    lv_obj_set_width(filename_label_, 236);
    lv_label_set_long_mode(filename_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(filename_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(filename_label_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(filename_label_, &phone_font_14, 0);
    lv_obj_align(filename_label_, LV_ALIGN_CENTER, 0, 0);

    // 照片显示区域放在顶部栏和底部按钮之间，避免图片盖住控件。
    photo_img_ = lv_image_create(fullscreen_body_);
    lv_obj_set_size(photo_img_, kPhotoAreaWidth, kPhotoAreaHeight);
    lv_obj_align(photo_img_, LV_ALIGN_TOP_MID, 0, kPhotoAreaTop);
    lv_obj_set_style_bg_opa(photo_img_, LV_OPA_TRANSP, 0);
    lv_image_set_inner_align(photo_img_, LV_IMAGE_ALIGN_CENTER);

    // 导航按钮容器（底部居中，圆形按钮）
    auto* controls = lv_obj_create(fullscreen_body_);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, 220, 44);
    lv_obj_align(controls, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_layout(controls, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(controls, 18, 0);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

    // 上一张按钮
    auto* prev_btn = CreateRoundButton(controls, FONT_AWESOME_BACKWARD_STEP, false);
    lv_obj_add_event_cb(prev_btn, [](lv_event_t* e) {
        auto* self = static_cast<PhotosApp*>(lv_event_get_user_data(e));
        self->ShowPreviousPhoto();
    }, LV_EVENT_CLICKED, this);

    // 返回网格按钮（中间，主按钮）
    auto* back_btn = CreateRoundButton(controls, FONT_AWESOME_IMAGE, true);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        auto* self = static_cast<PhotosApp*>(lv_event_get_user_data(e));
        self->BackToGrid();
    }, LV_EVENT_CLICKED, this);

    // 下一张按钮
    auto* next_btn = CreateRoundButton(controls, FONT_AWESOME_FORWARD_STEP, false);
    lv_obj_add_event_cb(next_btn, [](lv_event_t* e) {
        auto* self = static_cast<PhotosApp*>(lv_event_get_user_data(e));
        self->ShowNextPhoto();
    }, LV_EVENT_CLICKED, this);
}

void PhotosApp::ShowNextPhoto() {
    if (photos_.empty()) {
        return;
    }

    current_photo_index_ = (current_photo_index_ + 1) % photos_.size();
    ShowFullScreen(current_photo_index_);
}

void PhotosApp::ShowPreviousPhoto() {
    if (photos_.empty()) {
        return;
    }

    if (current_photo_index_ == 0) {
        current_photo_index_ = photos_.size() - 1;
    } else {
        current_photo_index_--;
    }
    ShowFullScreen(current_photo_index_);
}

void PhotosApp::BackToGrid() {
    ShowGridView();
}

void PhotosApp::NavigateBack() {
    if (current_view_ == ViewMode::kFullScreen) {
        BackToGrid();
        return;
    }
    NavigateHome();
}

void PhotosApp::NavigateHome() {
    if (auto* indev = lv_indev_active(); indev != nullptr) {
        lv_indev_wait_release(indev);
    }
    ESP_LOGI(TAG, "Header home button returning home");
    lv_async_call(DeferReturnHome, context_);
}

void RegisterPhotosApp(PhoneAppRegistry& registry) {
    PhoneAppDescriptor desc;
    desc.id = "photos";
    desc.title = "Photos";
    desc.icon = FONT_AWESOME_IMAGE;  // Font Awesome 图片图标
    desc.category = PhoneAppCategory::kMedia;
    desc.capabilities = PhoneCapability::kStorage;
    desc.show_on_home = true;
    desc.create = []() -> std::unique_ptr<PhoneApp> {
        return std::make_unique<PhotosApp>();
    };

    registry.Register(desc);
}

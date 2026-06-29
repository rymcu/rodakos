#include "apps/file_manager/file_manager_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <esp_log.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <utility>

namespace {
constexpr const char* TAG = "FileManagerApp";
constexpr lv_coord_t kPathBarHeight = 32;
constexpr lv_coord_t kListTop = kRodakosAppHeaderHeight + kPathBarHeight;
constexpr lv_coord_t kListHeight = 240 - kListTop;
constexpr lv_coord_t kPreviewAreaWidth = 292;
constexpr lv_coord_t kPreviewAreaHeight = 158;

struct EntryPayload {
    FileManagerApp* app = nullptr;
    size_t index = 0;
};

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        lv_indev_reset(nullptr, nullptr);
        context->navigation().ReturnHome();
    }
}

void OnEntryClicked(lv_event_t* e) {
    auto* payload = static_cast<EntryPayload*>(lv_event_get_user_data(e));
    if (payload != nullptr && payload->app != nullptr) {
        payload->app->OpenEntry(payload->index);
    }
}

void OnEntryPayloadDelete(lv_event_t* e) {
    auto* payload = static_cast<EntryPayload*>(lv_event_get_user_data(e));
    delete payload;
}

std::string JoinPath(const std::string& base, const std::string& name) {
    if (base.empty() || base == "/") {
        return "/" + name;
    }
    return base + "/" + name;
}

std::string FormatSize(size_t size) {
    char buffer[32] = {};
    if (size < 1024) {
        std::snprintf(buffer, sizeof(buffer), "%u B", static_cast<unsigned>(size));
    } else if (size < 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(size) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(size) / (1024.0 * 1024.0));
    }
    return buffer;
}

std::string FormatTime(uint64_t epoch) {
    if (epoch == 0) {
        return "Unknown";
    }

    std::time_t time_value = static_cast<std::time_t>(epoch);
    std::tm timeinfo = {};
    localtime_r(&time_value, &timeinfo);

    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &timeinfo);
    return buffer;
}

lv_obj_t* CreateText(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

const char* EntryBadgeText(const rodakos::FileEntry& entry) {
    if (entry.is_directory) {
        return "DIR";
    }
    if (rodakos::ImageLibrary::IsSupportedImage(entry.name)) {
        return "IMG";
    }
    return "FILE";
}

lv_color_t EntryBadgeColor(const rodakos::FileEntry& entry) {
    if (entry.is_directory) {
        return rodakos_theme_primary();
    }
    if (rodakos::ImageLibrary::IsSupportedImage(entry.name)) {
        return rodakos_theme_success();
    }
    return rodakos_theme_text_tertiary();
}

}  // namespace

FileManagerApp::~FileManagerApp() {
    OnDestroy();
}

bool FileManagerApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    file_service_ = context.services().file_service();

    storage_ready_ = file_service_ != nullptr &&
                     (file_service_->IsMounted() || file_service_->Init());
    if (storage_ready_) {
        LoadDirectory("/");
    } else {
        ESP_LOGW(TAG, "SD card not available for File Manager");
    }

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    CreateUi();
    RebuildList();
    ESP_LOGI(TAG, "File Manager created, path=%s entries=%zu",
             current_path_.c_str(), entries_.size());
    return true;
}

void FileManagerApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            if (root_ != nullptr && lv_obj_is_valid(root_)) {
                lv_obj_delete(root_);
            }
        }
    }

    current_image_.reset();
    root_ = nullptr;
    title_label_ = nullptr;
    path_label_ = nullptr;
    list_container_ = nullptr;
    preview_body_ = nullptr;
    preview_image_ = nullptr;
    preview_title_label_ = nullptr;
    info_body_ = nullptr;
    info_title_label_ = nullptr;
    info_detail_label_ = nullptr;
    entries_.clear();
    context_ = nullptr;
    ui_ = nullptr;
    file_service_ = nullptr;
}

void FileManagerApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    CreateAppHeader(root_, "Files", [](lv_event_t* e) {
        auto* self = static_cast<FileManagerApp*>(lv_event_get_user_data(e));
        self->NavigateBack();
    }, [](lv_event_t* e) {
        auto* self = static_cast<FileManagerApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, this, &title_label_);

    auto* path_bar = lv_obj_create(root_);
    lv_obj_remove_style_all(path_bar);
    lv_obj_set_size(path_bar, 300, 28);
    lv_obj_set_pos(path_bar, 10, 44);
    lv_obj_set_style_bg_color(path_bar, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(path_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(path_bar, 8, 0);
    lv_obj_set_style_pad_left(path_bar, 10, 0);
    lv_obj_set_style_pad_right(path_bar, 4, 0);
    lv_obj_clear_flag(path_bar, LV_OBJ_FLAG_SCROLLABLE);

    path_label_ = CreateText(path_bar, "/", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_width(path_label_, 230);
    lv_label_set_long_mode(path_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(path_label_, LV_ALIGN_LEFT_MID, 0, 0);

    auto* refresh_btn = lv_btn_create(path_bar);
    lv_obj_remove_style_all(refresh_btn);
    lv_obj_set_size(refresh_btn, 36, 24);
    lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(refresh_btn, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(refresh_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(refresh_btn, 6, 0);

    auto* refresh_icon = CreateText(refresh_btn, FONT_AWESOME_ARROWS_ROTATE, PhoneIconFont(),
                                    rodakos_theme_primary());
    lv_obj_center(refresh_icon);
    lv_obj_add_event_cb(refresh_btn, [](lv_event_t* e) {
        auto* self = static_cast<FileManagerApp*>(lv_event_get_user_data(e));
        self->RefreshDirectory();
    }, LV_EVENT_CLICKED, this);

    list_container_ = lv_obj_create(root_);
    lv_obj_remove_style_all(list_container_);
    lv_obj_set_size(list_container_, 300, kListHeight - 4);
    lv_obj_set_pos(list_container_, 10, kListTop + 2);
    lv_obj_set_style_bg_opa(list_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(list_container_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(list_container_, LV_DIR_VER);
}

void FileManagerApp::RebuildList() {
    if (list_container_ == nullptr) {
        return;
    }

    lv_obj_clean(list_container_);
    current_image_.reset();
    view_mode_ = ViewMode::kList;

    if (preview_body_ != nullptr) {
        lv_obj_add_flag(preview_body_, LV_OBJ_FLAG_HIDDEN);
    }
    if (info_body_ != nullptr) {
        lv_obj_add_flag(info_body_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(list_container_, LV_OBJ_FLAG_HIDDEN);

    if (path_label_ != nullptr) {
        lv_label_set_text(path_label_, DisplayPath().c_str());
    }

    if (!storage_ready_) {
        auto* empty = CreateText(list_container_, "SD card not mounted", &phone_font_14,
                                 rodakos_theme_text_secondary());
        lv_obj_set_width(empty, 280);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
        return;
    }

    if (entries_.empty()) {
        auto* empty = CreateText(list_container_, "Empty folder", &phone_font_14,
                                 rodakos_theme_text_secondary());
        lv_obj_set_width(empty, 280);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
        return;
    }

    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];
        auto* item = lv_btn_create(list_container_);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, 292, 44);
        lv_obj_set_pos(item, 0, static_cast<lv_coord_t>(i * 48));
        lv_obj_set_style_bg_color(item, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(item, 8, 0);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        auto* badge = lv_obj_create(item);
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 38, 24);
        lv_obj_align(badge, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(badge, EntryBadgeColor(entry), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_20, 0);
        lv_obj_set_style_radius(badge, 6, 0);
        lv_obj_set_style_border_color(badge, EntryBadgeColor(entry), 0);
        lv_obj_set_style_border_width(badge, 1, 0);

        auto* badge_text = CreateText(badge, EntryBadgeText(entry), &phone_font_12, EntryBadgeColor(entry));
        lv_obj_center(badge_text);

        auto* name = CreateText(item, entry.name.c_str(), &phone_font_14, rodakos_theme_text_primary());
        lv_obj_set_width(name, 178);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 48, 1);

        std::string detail = entry.is_directory ? "Folder" : FormatSize(entry.size);
        auto* meta = CreateText(item, detail.c_str(), &phone_font_12, rodakos_theme_text_tertiary());
        lv_obj_set_width(meta, 104);
        lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
        lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 48, 0);

        auto* arrow = CreateText(item,
                                 entry.is_directory ? FONT_AWESOME_ANGLE_RIGHT : FONT_AWESOME_CIRCLE_INFO,
                                 PhoneIconFont(), rodakos_theme_text_tertiary());
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);

        auto* payload = new EntryPayload{.app = this, .index = i};
        lv_obj_add_event_cb(item, OnEntryClicked, LV_EVENT_CLICKED, payload);
        lv_obj_add_event_cb(item, OnEntryPayloadDelete, LV_EVENT_DELETE, payload);
    }
}

bool FileManagerApp::LoadDirectory(const std::string& path) {
    if (file_service_ == nullptr) {
        return false;
    }
    if (!file_service_->IsMounted() && !file_service_->Init()) {
        storage_ready_ = false;
        entries_.clear();
        return false;
    }

    std::vector<rodakos::FileEntry> entries;
    if (!file_service_->ListDirectory(path, entries)) {
        if (root_ != nullptr) {
            ui_->ShowToastUnlocked("Open folder failed");
        }
        return false;
    }

    entries_ = std::move(entries);
    current_path_ = path.empty() ? "/" : path;
    storage_ready_ = true;
    ESP_LOGI(TAG, "Loaded directory %s (%zu entries)", current_path_.c_str(), entries_.size());
    return true;
}

void FileManagerApp::ShowListView() {
    RebuildList();
}

void FileManagerApp::OpenEntry(size_t index) {
    if (index >= entries_.size()) {
        return;
    }

    const auto entry = entries_[index];
    if (entry.is_directory) {
        if (LoadDirectory(JoinPath(current_path_, entry.name))) {
            RebuildList();
        }
        return;
    }

    if (rodakos::ImageLibrary::IsSupportedImage(entry.name)) {
        ShowImagePreview(entry);
    } else {
        ShowFileInfo(entry);
    }
}

void FileManagerApp::ShowImagePreview(const rodakos::FileEntry& entry) {
    if (list_container_ != nullptr) {
        lv_obj_add_flag(list_container_, LV_OBJ_FLAG_HIDDEN);
    }
    if (info_body_ != nullptr) {
        lv_obj_add_flag(info_body_, LV_OBJ_FLAG_HIDDEN);
    }

    if (preview_body_ == nullptr) {
        preview_body_ = lv_obj_create(root_);
        lv_obj_remove_style_all(preview_body_);
        lv_obj_set_size(preview_body_, 300, 164);
        lv_obj_set_pos(preview_body_, 10, 76);
        lv_obj_set_style_bg_color(preview_body_, rodakos_theme_bg_primary(), 0);
        lv_obj_set_style_bg_opa(preview_body_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(preview_body_, LV_OBJ_FLAG_SCROLLABLE);

        preview_image_ = lv_image_create(preview_body_);
        lv_obj_set_size(preview_image_, kPreviewAreaWidth, kPreviewAreaHeight);
        lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, -8);
        lv_image_set_inner_align(preview_image_, LV_IMAGE_ALIGN_CENTER);

        preview_title_label_ = CreateText(preview_body_, "", &phone_font_12,
                                          rodakos_theme_text_primary());
        lv_obj_set_width(preview_title_label_, 292);
        lv_label_set_long_mode(preview_title_label_, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(preview_title_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(preview_title_label_, LV_ALIGN_BOTTOM_MID, 0, -2);
    } else {
        lv_obj_clear_flag(preview_body_, LV_OBJ_FLAG_HIDDEN);
    }

    view_mode_ = ViewMode::kPreview;
    current_image_ = rodakos::ImageLibrary::LoadImageForDisplay(entry.path);
    if (current_image_ == nullptr) {
        lv_image_set_src(preview_image_, nullptr);
        lv_label_set_text(preview_title_label_, "Failed to load image");
        ui_->ShowToastUnlocked("Image load failed");
        return;
    }

    const void* source = current_image_->GetImageSource();
    lv_image_header_t header = {};
    if (lv_image_decoder_get_info(source, &header) != LV_RESULT_OK || header.w == 0 || header.h == 0) {
        current_image_.reset();
        lv_image_set_src(preview_image_, nullptr);
        lv_label_set_text(preview_title_label_, "Unsupported image");
        ui_->ShowToastUnlocked("Unsupported image");
        return;
    }

    const int32_t scale_w = (kPreviewAreaWidth * LV_SCALE_NONE) / static_cast<int32_t>(header.w);
    const int32_t scale_h = (kPreviewAreaHeight * LV_SCALE_NONE) / static_cast<int32_t>(header.h);
    const int32_t scale = std::max<int32_t>(1, std::min(scale_w, scale_h));

    auto image = std::move(current_image_);
    lv_image_set_src(preview_image_, nullptr);
    current_image_ = std::move(image);
    lv_obj_set_size(preview_image_, header.w, header.h);
    lv_image_set_scale(preview_image_, static_cast<uint32_t>(scale));
    lv_image_set_src(preview_image_, source);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, -8);
    lv_label_set_text(preview_title_label_, entry.name.c_str());

    ESP_LOGI(TAG, "Preview image %s (%ux%u, scale=%d)",
             entry.path.c_str(),
             static_cast<unsigned>(header.w),
             static_cast<unsigned>(header.h),
             static_cast<int>(scale));
}

void FileManagerApp::ShowFileInfo(const rodakos::FileEntry& entry) {
    if (list_container_ != nullptr) {
        lv_obj_add_flag(list_container_, LV_OBJ_FLAG_HIDDEN);
    }
    if (preview_body_ != nullptr) {
        lv_obj_add_flag(preview_body_, LV_OBJ_FLAG_HIDDEN);
    }
    current_image_.reset();

    if (info_body_ == nullptr) {
        info_body_ = lv_obj_create(root_);
        lv_obj_remove_style_all(info_body_);
        lv_obj_set_size(info_body_, 300, 160);
        lv_obj_set_pos(info_body_, 10, 78);
        lv_obj_set_style_bg_color(info_body_, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_bg_opa(info_body_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(info_body_, 8, 0);
        lv_obj_set_style_pad_all(info_body_, 12, 0);
        lv_obj_clear_flag(info_body_, LV_OBJ_FLAG_SCROLLABLE);

        info_title_label_ = CreateText(info_body_, "", &phone_font_14, rodakos_theme_text_primary());
        lv_obj_set_width(info_title_label_, 276);
        lv_label_set_long_mode(info_title_label_, LV_LABEL_LONG_DOT);
        lv_obj_align(info_title_label_, LV_ALIGN_TOP_LEFT, 0, 0);

        info_detail_label_ = CreateText(info_body_, "", &phone_font_12, rodakos_theme_text_secondary());
        lv_obj_set_width(info_detail_label_, 276);
        lv_label_set_long_mode(info_detail_label_, LV_LABEL_LONG_WRAP);
        lv_obj_align(info_detail_label_, LV_ALIGN_TOP_LEFT, 0, 30);
    } else {
        lv_obj_clear_flag(info_body_, LV_OBJ_FLAG_HIDDEN);
    }

    view_mode_ = ViewMode::kInfo;
    lv_label_set_text(info_title_label_, entry.name.c_str());

    std::string detail = "Type: File\nSize: " + FormatSize(entry.size) +
                         "\nModified: " + FormatTime(entry.modified_time) +
                         "\n\nPath:\n" + entry.path;
    lv_label_set_text(info_detail_label_, detail.c_str());
}

void FileManagerApp::NavigateBack() {
    if (view_mode_ == ViewMode::kPreview || view_mode_ == ViewMode::kInfo) {
        ShowListView();
        return;
    }

    if (current_path_ != "/") {
        if (LoadDirectory(ParentPath())) {
            RebuildList();
        }
        return;
    }

    NavigateHome();
}

void FileManagerApp::NavigateHome() {
    if (auto* indev = lv_indev_active(); indev != nullptr) {
        lv_indev_wait_release(indev);
    }
    ESP_LOGI(TAG, "Header home button returning home");
    lv_async_call(DeferReturnHome, context_);
}

void FileManagerApp::RefreshDirectory() {
    if (LoadDirectory(current_path_)) {
        RebuildList();
        ui_->ShowToastUnlocked("Refreshed");
    } else {
        RebuildList();
    }
}

std::string FileManagerApp::ParentPath() const {
    if (current_path_.empty() || current_path_ == "/") {
        return "/";
    }

    const auto pos = current_path_.find_last_of('/');
    if (pos == std::string::npos || pos == 0) {
        return "/";
    }
    return current_path_.substr(0, pos);
}

std::string FileManagerApp::DisplayPath() const {
    if (current_path_.empty() || current_path_ == "/") {
        return "/sdcard";
    }
    return "/sdcard" + current_path_;
}

void RegisterFileManagerApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "files",
        .title = "Files",
        .icon = FONT_AWESOME_SD_CARD,
        .category = PhoneAppCategory::kTools,
        .launch_mode = PhoneAppLaunchMode::kReplaceCurrent,
        .capabilities = PhoneCapability::kStorage,
        .show_on_home = true,
        .aliases = {"file", "files", "manager", "sd", "文件", "文件管理"},
        .create = []() { return std::make_unique<FileManagerApp>(); },
    });
}

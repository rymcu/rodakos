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
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace {
constexpr const char* TAG = "FileManagerApp";
constexpr lv_coord_t kStripHeight = 24;
constexpr lv_coord_t kListTop = kRodakosAppHeaderHeight + kStripHeight;
constexpr lv_coord_t kListHeight = 240 - kListTop;
constexpr lv_coord_t kRowHeight = 40;
constexpr lv_coord_t kNameWidth = 222;
constexpr uint32_t kCapacityPollMs = 300;

// Files under this folder belong to OTA, appearance and voice; the UI never offers to delete them.
constexpr const char* kSystemFolder = "/rodakos";

enum class EntryKind { kFolder, kImage, kAudio, kDocument };

struct EntryPayload {
    FileManagerApp* app = nullptr;
    size_t index = 0;
};

struct CapacityProbeArgs {
    std::shared_ptr<void> keep_alive;
    void* snapshot = nullptr;
    rodakos::FileService* files = nullptr;
};

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
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

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string Extension(const std::string& name) {
    const auto dot = name.find_last_of('.');
    return dot == std::string::npos ? std::string() : Lower(name.substr(dot + 1));
}

EntryKind KindOf(const rodakos::FileEntry& entry) {
    if (entry.is_directory) return EntryKind::kFolder;
    if (rodakos::ImageLibrary::IsSupportedImage(entry.name)) return EntryKind::kImage;
    const std::string ext = Extension(entry.name);
    if (ext == "mp3" || ext == "wav" || ext == "ogg" || ext == "flac" || ext == "m4a" || ext == "aac") {
        return EntryKind::kAudio;
    }
    return EntryKind::kDocument;
}

const char* KindIcon(EntryKind kind) {
    switch (kind) {
        case EntryKind::kFolder: return ICON_FOLDER;
        case EntryKind::kImage: return ICON_IMAGE;
        case EntryKind::kAudio: return ICON_MUSIC;
        case EntryKind::kDocument: return ICON_DOCUMENT;
    }
    return ICON_DOCUMENT;
}

lv_color_t KindColor(EntryKind kind) {
    switch (kind) {
        case EntryKind::kFolder: return rodakos_theme_primary();
        case EntryKind::kImage: return rodakos_theme_success();
        case EntryKind::kAudio: return rodakos_theme_warning();
        case EntryKind::kDocument: return rodakos_theme_text_tertiary();
    }
    return rodakos_theme_text_tertiary();
}

std::string KindText(const rodakos::FileEntry& entry) {
    const std::string ext = Extension(entry.name);
    std::string upper = ext;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    switch (KindOf(entry)) {
        case EntryKind::kImage: return upper + " 图片";
        case EntryKind::kAudio: return upper + " 音频";
        case EntryKind::kFolder: return "文件夹";
        case EntryKind::kDocument: return ext.empty() ? std::string("文件") : upper + " 文件";
    }
    return "文件";
}

std::string FormatSize(uint64_t size) {
    char buffer[32] = {};
    if (size < 1024) {
        std::snprintf(buffer, sizeof(buffer), "%u B", static_cast<unsigned>(size));
    } else if (size < 1024ULL * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(size) / 1024.0);
    } else if (size < 1024ULL * 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(size) / (1024.0 * 1024.0));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB",
                      static_cast<double>(size) / (1024.0 * 1024.0 * 1024.0));
    }
    return buffer;
}

std::string FormatTime(uint64_t epoch) {
    if (epoch == 0) return "—";
    std::time_t time_value = static_cast<std::time_t>(epoch);
    std::tm timeinfo = {};
    localtime_r(&time_value, &timeinfo);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &timeinfo);
    return buffer;
}

// FileEntry::path is the absolute VFS path ("/sdcard/music/x.mp3"); show its folder.
std::string FolderOf(const std::string& path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return "/";
    return path.substr(0, slash);
}

bool InSystemFolder(const std::string& app_path) {
    const std::string prefix = kSystemFolder;
    return app_path == prefix || app_path.rfind(prefix + "/", 0) == 0;
}

lv_obj_t* CreateText(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t* CreatePlain(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* CreateButton(lv_obj_t* parent, const char* icon, const char* text, lv_coord_t width,
                       lv_coord_t height, lv_color_t bg, lv_color_t fg) {
    auto* button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, bg, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_darken(bg, 40), LV_STATE_PRESSED);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(button, 6, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    if (icon != nullptr) CreateText(button, icon, PhoneIconFont(), fg);
    CreateText(button, text, &phone_font_14, fg);
    return button;
}

lv_obj_t* CreateHeaderButton(lv_obj_t* parent, const char* icon, lv_color_t bg, lv_color_t fg) {
    auto* button = RodakosCreateHeaderIconButton(parent, icon);
    lv_obj_set_style_bg_color(button, bg, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(button, 0), fg, 0);
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

// Centred icon + title + hint + optional button, as on the Photos and Camera error pages.
lv_obj_t* CreateStatusBlock(lv_obj_t* parent, const char* icon, const char* title, const char* hint,
                            const char* action, lv_event_cb_t action_cb, void* user_data) {
    auto* box = CreatePlain(parent);
    lv_obj_set_size(box, 320, kListHeight);
    lv_obj_set_pos(box, 0, kListTop);
    auto* glyph = CreateText(box, icon, PhoneIconFontLarge(), rodakos_theme_text_tertiary());
    lv_obj_align(glyph, LV_ALIGN_TOP_MID, 0, action != nullptr ? 18 : 34);
    auto* title_label = CreateText(box, title, &phone_font_14, rodakos_theme_text_primary());
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, action != nullptr ? 56 : 72);
    auto* hint_label = CreateText(box, hint, &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_width(hint_label, 288);
    lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint_label, LV_ALIGN_TOP_MID, 0, action != nullptr ? 80 : 96);
    if (action != nullptr) {
        auto* button = CreateButton(box, ICON_REFRESH, action, 120, 38, rodakos_theme_primary(),
                                    rodakos_theme_on_primary());
        lv_obj_align(button, LV_ALIGN_TOP_MID, 0, 112);
        lv_obj_add_event_cb(button, action_cb, LV_EVENT_CLICKED, user_data);
    }
    return box;
}

}  // namespace

bool FileManagerApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    file_service_ = context.services().file_service();
    capacity_ = std::make_shared<CapacitySnapshot>();

    LoadDirectory("/");

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    CreateUi();
    RebuildList();
    RequestCapacity();
    ESP_LOGI(TAG, "File Manager created, path=%s entries=%zu",
             current_path_.c_str(), entries_.size());
    return true;
}

void FileManagerApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_, 0);
        if (lock.locked()) {
            lv_async_call_cancel(DeferReturnHome, context_);
            if (capacity_timer_ != nullptr) {
                lv_timer_delete(capacity_timer_);
                capacity_timer_ = nullptr;
            }
            ReleaseCurrentImage();
            if (delete_layer_ != nullptr && lv_obj_is_valid(delete_layer_)) {
                lv_obj_delete(delete_layer_);
            }
            if (root_ != nullptr && lv_obj_is_valid(root_)) {
                lv_obj_delete(root_);
            }
        }
    }

    current_image_.reset();
    capacity_.reset();
    root_ = header_ = title_label_ = refresh_button_ = nullptr;
    strip_ = strip_left_label_ = strip_right_label_ = nullptr;
    list_container_ = status_box_ = nullptr;
    preview_body_ = preview_image_ = preview_top_ = preview_bottom_ = nullptr;
    preview_title_label_ = preview_size_label_ = preview_notice_ = nullptr;
    preview_error_label_ = preview_retry_button_ = nullptr;
    info_body_ = info_icon_label_ = info_title_label_ = info_path_label_ = nullptr;
    info_type_label_ = info_size_label_ = info_time_label_ = info_delete_button_ = nullptr;
    delete_layer_ = nullptr;
    capacity_timer_ = nullptr;
    entries_.clear();
    preview_entry_ = {};
    info_entry_ = {};
    current_path_ = "/";
    info_from_preview_ = false;
    preview_chrome_visible_ = true;
    directory_status_ = DirectoryStatus::kServiceUnavailable;
    context_ = nullptr;
    ui_ = nullptr;
    file_service_ = nullptr;
}

bool FileManagerApp::OnBackRequested() {
    NavigateBack();
    return true;
}

void FileManagerApp::ReleaseCurrentImage() {
    if (current_image_ == nullptr) {
        return;
    }

    const void* source = current_image_->GetImageSource();
    if (preview_image_ != nullptr && lv_obj_is_valid(preview_image_)) {
        lv_image_set_src(preview_image_, nullptr);
    }
    if (source != nullptr) {
        lv_image_cache_drop(source);
    }
    current_image_.reset();
}

void FileManagerApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    header_ = CreateAppHeader(root_, "文件", [](lv_event_t* e) {
        static_cast<FileManagerApp*>(lv_event_get_user_data(e))->NavigateBack();
    }, [](lv_event_t* e) {
        static_cast<FileManagerApp*>(lv_event_get_user_data(e))->RefreshDirectory();
    }, this, &title_label_, ICON_REFRESH, &refresh_button_);

    strip_ = CreatePlain(root_);
    lv_obj_set_size(strip_, 320, kStripHeight);
    lv_obj_set_pos(strip_, 0, kRodakosAppHeaderHeight);
    lv_obj_set_style_border_side(strip_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(strip_, rodakos_theme_divider(), 0);
    lv_obj_set_style_border_width(strip_, 1, 0);
    strip_left_label_ = CreateText(strip_, "", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_width(strip_left_label_, 220);
    lv_label_set_long_mode(strip_left_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(strip_left_label_, LV_ALIGN_LEFT_MID, 12, 0);
    strip_right_label_ = CreateText(strip_, "", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_align(strip_right_label_, LV_ALIGN_RIGHT_MID, -12, 0);

    list_container_ = CreatePlain(root_);
    lv_obj_set_size(list_container_, 320, kListHeight);
    lv_obj_set_pos(list_container_, 0, kListTop);
    lv_obj_add_flag(list_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(list_container_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(list_container_, LV_DIR_VER);
}

void FileManagerApp::UpdateHeader() {
    if (title_label_ == nullptr) return;
    if (current_path_.empty() || current_path_ == "/") {
        lv_label_set_text(title_label_, "文件");
    } else {
        lv_label_set_text(title_label_, current_path_.substr(current_path_.find_last_of('/') + 1).c_str());
    }
    lv_label_set_long_mode(title_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title_label_, 200);
    lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(title_label_);
}

void FileManagerApp::UpdateStrip() {
    if (strip_left_label_ == nullptr) return;
    const bool root = current_path_.empty() || current_path_ == "/";
    if (!root) {
        lv_label_set_text(strip_left_label_, current_path_.c_str());
    } else {
        bool known = false;
        rodakos::FileService::Capacity capacity{};
        if (capacity_ != nullptr) {
            std::lock_guard<std::mutex> lock(capacity_->mutex);
            known = capacity_->known;
            capacity = capacity_->capacity;
        }
        if (known && capacity.total_bytes > 0) {
            lv_label_set_text_fmt(strip_left_label_, "SD 卡 · 剩余 %s", FormatSize(capacity.free_bytes).c_str());
        } else {
            lv_label_set_text(strip_left_label_, "SD 卡");
        }
    }
    if (directory_status_ == DirectoryStatus::kReady) {
        lv_label_set_text_fmt(strip_right_label_, "%u 项", static_cast<unsigned>(entries_.size()));
    } else {
        lv_label_set_text(strip_right_label_, "");
    }
}

void FileManagerApp::RebuildList() {
    if (list_container_ == nullptr) {
        return;
    }

    lv_obj_clean(list_container_);
    if (status_box_ != nullptr && lv_obj_is_valid(status_box_)) lv_obj_delete(status_box_);
    status_box_ = nullptr;
    ReleaseCurrentImage();
    view_mode_ = ViewMode::kList;
    info_from_preview_ = false;

    SetHidden(preview_body_, true);
    SetHidden(info_body_, true);
    SetHidden(header_, false);
    SetHidden(strip_, false);
    SetHidden(refresh_button_, false);
    SetHidden(list_container_, false);
    UpdateHeader();
    UpdateStrip();

    if (directory_status_ != DirectoryStatus::kReady || entries_.empty()) {
        SetHidden(list_container_, true);
        const bool ready = directory_status_ == DirectoryStatus::kReady;
        const char* icon = directory_status_ == DirectoryStatus::kStorageUnavailable ? ICON_STORAGE : ICON_FOLDER;
        status_box_ = CreateStatusBlock(root_, icon, DirectoryTitle(), DirectoryHint(),
                                        ready ? "刷新" : "重试", [](lv_event_t* e) {
            static_cast<FileManagerApp*>(lv_event_get_user_data(e))->RefreshDirectory();
        }, this);
        return;
    }

    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];
        const EntryKind kind = KindOf(entry);
        auto* item = lv_btn_create(list_container_);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, 320, kRowHeight);
        lv_obj_set_pos(item, 0, static_cast<lv_coord_t>(i * kRowHeight));
        lv_obj_set_style_bg_color(item, rodakos_theme_bg_secondary(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

        auto* icon = CreateText(item, KindIcon(kind), PhoneIconFont(), KindColor(kind));
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 12, 0);

        auto* name = CreateText(item, entry.name.c_str(), &phone_font_14, rodakos_theme_text_primary());
        lv_obj_set_size(name, kNameWidth, lv_font_get_line_height(&phone_font_14));
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 42, 0);

        if (entry.is_directory) {
            auto* chevron = CreateText(item, ICON_CHEVRON_RIGHT, PhoneIconFont(), rodakos_theme_text_tertiary());
            lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -10, 0);
        } else {
            auto* size = CreateText(item, FormatSize(entry.size).c_str(), &phone_font_12,
                                    rodakos_theme_text_tertiary());
            lv_obj_align(size, LV_ALIGN_RIGHT_MID, -12, 0);
        }

        if (i + 1 < entries_.size()) {
            auto* divider = CreatePlain(item);
            lv_obj_set_size(divider, 278, 1);
            lv_obj_align(divider, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
            lv_obj_set_style_bg_color(divider, rodakos_theme_divider(), 0);
            lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
        }

        auto* payload = new EntryPayload{.app = this, .index = i};
        lv_obj_add_event_cb(item, OnEntryClicked, LV_EVENT_CLICKED, payload);
        lv_obj_add_event_cb(item, OnEntryPayloadDelete, LV_EVENT_DELETE, payload);
    }
}

bool FileManagerApp::LoadDirectory(const std::string& path) {
    // The path can alias current_path_; retain the attempted folder for Retry and Back.
    current_path_ = path.empty() ? "/" : path;
    entries_.clear();
    file_service_ = context_->services().file_service();
    if (file_service_ == nullptr) {
        directory_status_ = DirectoryStatus::kServiceUnavailable;
        return false;
    }
    if (!file_service_->IsMounted() && !file_service_->Init()) {
        directory_status_ = DirectoryStatus::kStorageUnavailable;
        return false;
    }

    std::vector<rodakos::FileEntry> entries;
    errno = 0;
    if (!file_service_->ListDirectory(current_path_, entries)) {
        const int error = errno;
        if (!file_service_->IsMounted() || error == ENODEV) {
            directory_status_ = DirectoryStatus::kStorageUnavailable;
        } else if (error == ENOENT || error == ENOTDIR) {
            directory_status_ = DirectoryStatus::kMissing;
        } else {
            directory_status_ = DirectoryStatus::kReadFailed;
        }
        return false;
    }

    entries_ = std::move(entries);
    directory_status_ = DirectoryStatus::kReady;
    ESP_LOGI(TAG, "Loaded directory %s (%zu entries)", current_path_.c_str(), entries_.size());
    return true;
}

const char* FileManagerApp::DirectoryTitle() const {
    switch (directory_status_) {
        case DirectoryStatus::kServiceUnavailable: return "存储服务不可用";
        case DirectoryStatus::kStorageUnavailable: return "读不到 SD 卡";
        case DirectoryStatus::kMissing: return "目录不存在";
        case DirectoryStatus::kReadFailed: return "无法读取目录";
        case DirectoryStatus::kReady: return "空文件夹";
    }
    return "无法读取目录";
}

const char* FileManagerApp::DirectoryHint() const {
    switch (directory_status_) {
        case DirectoryStatus::kServiceUnavailable: return "请重启设备后再试";
        case DirectoryStatus::kStorageUnavailable: return "请插入或重新插拔 SD 卡";
        case DirectoryStatus::kMissing: return "这个文件夹可能已被移走或删除";
        case DirectoryStatus::kReadFailed: return "读取时出错，请重试";
        case DirectoryStatus::kReady: return "这个目录里没有文件";
    }
    return "读取时出错，请重试";
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
        LoadDirectory(JoinPath(current_path_, entry.name));
        RebuildList();
        return;
    }

    if (rodakos::ImageLibrary::IsSupportedImage(entry.name)) {
        ShowImagePreview(entry);
    } else {
        info_from_preview_ = false;
        ShowFileInfo(entry);
    }
}

void FileManagerApp::ShowImagePreview(const rodakos::FileEntry& entry) {
    SetHidden(list_container_, true);
    SetHidden(status_box_, true);
    SetHidden(info_body_, true);
    SetHidden(header_, true);
    SetHidden(strip_, true);

    if (preview_body_ == nullptr) {
        // Full screen, black, like Photos: the header and path strip no longer take 72 px.
        preview_body_ = CreatePlain(root_);
        lv_obj_set_size(preview_body_, 320, 240);
        lv_obj_set_pos(preview_body_, 0, 0);
        lv_obj_set_style_bg_color(preview_body_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(preview_body_, LV_OPA_COVER, 0);
        lv_obj_add_flag(preview_body_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(preview_body_, [](lv_event_t* e) {
            static_cast<FileManagerApp*>(lv_event_get_user_data(e))->TogglePreviewChrome();
        }, LV_EVENT_CLICKED, this);

        preview_image_ = lv_image_create(preview_body_);
        lv_obj_set_size(preview_image_, 320, 240);
        lv_obj_center(preview_image_);
        lv_image_set_inner_align(preview_image_, LV_IMAGE_ALIGN_CENTER);
        lv_obj_clear_flag(preview_image_, LV_OBJ_FLAG_CLICKABLE);

        preview_notice_ = CreatePlain(preview_body_);
        lv_obj_set_size(preview_notice_, 320, 140);
        lv_obj_align(preview_notice_, LV_ALIGN_CENTER, 0, 10);
        auto* notice_icon = CreateText(preview_notice_, ICON_IMAGE, PhoneIconFontLarge(), lv_color_white());
        lv_obj_set_style_text_opa(notice_icon, LV_OPA_60, 0);
        lv_obj_align(notice_icon, LV_ALIGN_TOP_MID, 0, 0);
        auto* notice_title = CreateText(preview_notice_, "打不开这张图片", &phone_font_14, lv_color_white());
        lv_obj_align(notice_title, LV_ALIGN_TOP_MID, 0, 38);
        preview_error_label_ = CreateText(preview_notice_, "", &phone_font_12, lv_color_white());
        lv_obj_set_style_text_opa(preview_error_label_, LV_OPA_70, 0);
        lv_obj_align(preview_error_label_, LV_ALIGN_TOP_MID, 0, 62);
        preview_retry_button_ = CreateButton(preview_notice_, ICON_REFRESH, "重试", 120, 38,
                                             rodakos_theme_primary(), rodakos_theme_on_primary());
        lv_obj_align(preview_retry_button_, LV_ALIGN_TOP_MID, 0, 94);
        lv_obj_add_event_cb(preview_retry_button_, [](lv_event_t* e) {
            auto* self = static_cast<FileManagerApp*>(lv_event_get_user_data(e));
            self->ShowImagePreview(self->preview_entry_);
            lv_event_stop_bubbling(e);
        }, LV_EVENT_CLICKED, this);

        preview_top_ = CreatePlain(preview_body_);
        lv_obj_set_size(preview_top_, 320, 40);
        lv_obj_set_pos(preview_top_, 0, 0);
        lv_obj_set_style_bg_color(preview_top_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(preview_top_, LV_OPA_50, 0);
        auto* back = CreateHeaderButton(preview_top_, ICON_ARROW_LEFT, lv_color_white(), lv_color_white());
        lv_obj_set_style_bg_opa(back, LV_OPA_20, 0);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_add_event_cb(back, [](lv_event_t* e) {
            static_cast<FileManagerApp*>(lv_event_get_user_data(e))->NavigateBack();
            lv_event_stop_bubbling(e);
        }, LV_EVENT_CLICKED, this);
        auto* info = CreateHeaderButton(preview_top_, ICON_INFO, lv_color_white(), lv_color_white());
        lv_obj_set_style_bg_opa(info, LV_OPA_20, 0);
        lv_obj_align(info, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_add_event_cb(info, [](lv_event_t* e) {
            auto* self = static_cast<FileManagerApp*>(lv_event_get_user_data(e));
            self->info_from_preview_ = true;
            self->ShowFileInfo(self->preview_entry_);
            lv_event_stop_bubbling(e);
        }, LV_EVENT_CLICKED, this);

        preview_bottom_ = CreatePlain(preview_body_);
        lv_obj_set_size(preview_bottom_, 320, 32);
        lv_obj_align(preview_bottom_, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(preview_bottom_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(preview_bottom_, LV_OPA_50, 0);
        preview_title_label_ = CreateText(preview_bottom_, "", &phone_font_12, lv_color_white());
        lv_obj_set_width(preview_title_label_, 220);
        lv_label_set_long_mode(preview_title_label_, LV_LABEL_LONG_DOT);
        lv_obj_align(preview_title_label_, LV_ALIGN_LEFT_MID, 12, 0);
        preview_size_label_ = CreateText(preview_bottom_, "", &phone_font_12, lv_color_white());
        lv_obj_set_style_text_opa(preview_size_label_, LV_OPA_70, 0);
        lv_obj_align(preview_size_label_, LV_ALIGN_RIGHT_MID, -12, 0);
    } else {
        lv_obj_clear_flag(preview_body_, LV_OBJ_FLAG_HIDDEN);
    }

    view_mode_ = ViewMode::kPreview;
    preview_entry_ = entry;
    preview_chrome_visible_ = true;
    ReleaseCurrentImage();
    lv_label_set_text(preview_title_label_, entry.name.c_str());
    lv_label_set_text(preview_size_label_, FormatSize(entry.size).c_str());
    SetHidden(preview_top_, false);
    SetHidden(preview_bottom_, false);
    SetHidden(preview_notice_, true);
    SetHidden(preview_image_, false);

    file_service_ = context_->services().file_service();
    if (file_service_ == nullptr) {
        ShowPreviewError("存储服务不可用");
        return;
    }
    if (!file_service_->IsMounted() && !file_service_->Init()) {
        ShowPreviewError("读不到 SD 卡");
        return;
    }
    auto result = rodakos::ImageLibrary::LoadImageForDisplayDetailed(entry.path);
    current_image_ = std::move(result.image);
    if (current_image_ == nullptr) {
        using Status = rodakos::ImageLibrary::ImageLoadStatus;
        switch (result.status) {
            case Status::kUnsupported: ShowPreviewError("不支持这种图片格式"); break;
            case Status::kReadFailed: ShowPreviewError("无法读取这张图片"); break;
            case Status::kInsufficientMemory: ShowPreviewError("图片太大，内存不够"); break;
            default: ShowPreviewError("文件已损坏"); break;
        }
        return;
    }

    const void* source = current_image_->GetImageSource();
    lv_image_header_t header = {};
    if (lv_image_decoder_get_info(source, &header) != LV_RESULT_OK || header.w == 0 || header.h == 0) {
        ShowPreviewError("文件已损坏");
        return;
    }

    const int32_t scale_w = (320 * LV_SCALE_NONE) / static_cast<int32_t>(header.w);
    const int32_t scale_h = (240 * LV_SCALE_NONE) / static_cast<int32_t>(header.h);
    const int32_t scale = std::max<int32_t>(1, std::min(scale_w, scale_h));

    auto image = std::move(current_image_);
    lv_image_set_src(preview_image_, nullptr);
    current_image_ = std::move(image);
    lv_obj_set_size(preview_image_, header.w, header.h);
    lv_image_set_scale(preview_image_, static_cast<uint32_t>(scale));
    lv_image_set_src(preview_image_, source);
    lv_obj_center(preview_image_);

    ESP_LOGI(TAG, "Preview image %s (%ux%u, scale=%d)",
             entry.path.c_str(),
             static_cast<unsigned>(header.w),
             static_cast<unsigned>(header.h),
             static_cast<int>(scale));
}

void FileManagerApp::TogglePreviewChrome() {
    if (view_mode_ != ViewMode::kPreview || current_image_ == nullptr) return;
    preview_chrome_visible_ = !preview_chrome_visible_;
    SetHidden(preview_top_, !preview_chrome_visible_);
    SetHidden(preview_bottom_, !preview_chrome_visible_);
}

void FileManagerApp::ShowFileInfo(const rodakos::FileEntry& entry) {
    SetHidden(list_container_, true);
    SetHidden(status_box_, true);
    SetHidden(preview_body_, true);
    SetHidden(strip_, true);
    SetHidden(header_, false);
    SetHidden(refresh_button_, true);
    ReleaseCurrentImage();
    lv_label_set_text(title_label_, "文件详情");

    if (info_body_ == nullptr) {
        info_body_ = CreatePlain(root_);
        lv_obj_set_size(info_body_, 320, 200);
        lv_obj_set_pos(info_body_, 0, kRodakosAppHeaderHeight);

        auto* hero = CreatePlain(info_body_);
        lv_obj_set_size(hero, 300, 52);
        lv_obj_set_pos(hero, 10, 6);
        lv_obj_set_style_bg_color(hero, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_bg_opa(hero, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(hero, 10, 0);
        info_icon_label_ = CreateText(hero, ICON_DOCUMENT, PhoneIconFont(), rodakos_theme_text_tertiary());
        lv_obj_align(info_icon_label_, LV_ALIGN_LEFT_MID, 12, 0);
        info_title_label_ = CreateText(hero, "", &phone_font_14, rodakos_theme_text_primary());
        lv_obj_set_size(info_title_label_, 240, lv_font_get_line_height(&phone_font_14));
        lv_label_set_long_mode(info_title_label_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(info_title_label_, 44, 6);
        info_path_label_ = CreateText(hero, "", &phone_font_12, rodakos_theme_text_tertiary());
        lv_obj_set_width(info_path_label_, 240);
        lv_label_set_long_mode(info_path_label_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(info_path_label_, 44, 30);

        auto* table = CreatePlain(info_body_);
        lv_obj_set_size(table, 300, 84);
        lv_obj_set_pos(table, 10, 64);
        lv_obj_set_style_bg_color(table, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_bg_opa(table, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(table, 8, 0);
        const char* keys[] = {"类型", "大小", "修改时间"};
        lv_obj_t** values[] = {&info_type_label_, &info_size_label_, &info_time_label_};
        for (int i = 0; i < 3; ++i) {
            auto* key = CreateText(table, keys[i], &phone_font_14, rodakos_theme_text_secondary());
            lv_obj_set_pos(key, 12, 4 + i * 28);
            *values[i] = CreateText(table, "", &phone_font_14, rodakos_theme_text_primary());
            lv_obj_set_width(*values[i], 190);
            lv_label_set_long_mode(*values[i], LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_align(*values[i], LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(*values[i], LV_ALIGN_TOP_RIGHT, -12, 4 + i * 28);
            if (i > 0) {
                auto* divider = CreatePlain(table);
                lv_obj_set_size(divider, 300, 1);
                lv_obj_set_pos(divider, 0, i * 28);
                lv_obj_set_style_bg_color(divider, rodakos_theme_bg_tertiary(), 0);
                lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
            }
        }

        info_delete_button_ = lv_btn_create(info_body_);
        lv_obj_remove_style_all(info_delete_button_);
        lv_obj_set_size(info_delete_button_, 300, 38);
        lv_obj_set_pos(info_delete_button_, 10, 156);
        lv_obj_set_style_radius(info_delete_button_, 8, 0);
        lv_obj_set_style_border_color(info_delete_button_, rodakos_theme_error(), 0);
        lv_obj_set_style_border_width(info_delete_button_, 1, 0);
        lv_obj_set_style_bg_color(info_delete_button_, rodakos_theme_error(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(info_delete_button_, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_set_flex_flow(info_delete_button_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(info_delete_button_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(info_delete_button_, 6, 0);
        CreateText(info_delete_button_, ICON_TRASH, PhoneIconFont(), rodakos_theme_error());
        CreateText(info_delete_button_, "删除文件", &phone_font_14, rodakos_theme_error());
        lv_obj_add_event_cb(info_delete_button_, [](lv_event_t* e) {
            static_cast<FileManagerApp*>(lv_event_get_user_data(e))->AskDelete();
        }, LV_EVENT_CLICKED, this);
    } else {
        lv_obj_clear_flag(info_body_, LV_OBJ_FLAG_HIDDEN);
    }

    view_mode_ = ViewMode::kInfo;
    info_entry_ = entry;
    const EntryKind kind = KindOf(entry);
    lv_label_set_text(info_icon_label_, KindIcon(kind));
    lv_obj_set_style_text_color(info_icon_label_, KindColor(kind), 0);
    lv_label_set_text(info_title_label_, entry.name.c_str());
    lv_label_set_text(info_path_label_, FolderOf(entry.path).c_str());
    lv_label_set_text(info_type_label_, KindText(entry).c_str());
    lv_label_set_text(info_size_label_, FormatSize(entry.size).c_str());
    lv_label_set_text(info_time_label_, FormatTime(entry.modified_time).c_str());
    SetHidden(info_delete_button_, InSystemFolder(JoinPath(current_path_, entry.name)));
}

void FileManagerApp::AskDelete() {
    if (view_mode_ != ViewMode::kInfo || delete_layer_ != nullptr) return;

    delete_layer_ = CreatePlain(lv_layer_top());
    lv_obj_set_size(delete_layer_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(delete_layer_, rodakos_theme_overlay(), 0);
    lv_obj_set_style_bg_opa(delete_layer_, LV_OPA_70, 0);
    lv_obj_add_flag(delete_layer_, LV_OBJ_FLAG_CLICKABLE);

    auto* box = CreatePlain(delete_layer_);
    lv_obj_set_size(box, 296, 136);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_border_color(box, rodakos_theme_border(), 0);
    lv_obj_set_style_border_width(box, 1, 0);

    auto* title = CreateText(box, "删除这个文件？", &phone_font_18, rodakos_theme_text_primary());
    lv_obj_set_pos(title, 14, 12);
    auto* message = CreateText(box, ("删除后无法恢复：" + info_entry_.name).c_str(), &phone_font_12,
                               rodakos_theme_text_secondary());
    lv_obj_set_size(message, 266, 36);
    lv_label_set_long_mode(message, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(message, 14, 42);

    auto* cancel = CreateButton(box, nullptr, "取消", 128, 38, rodakos_theme_bg_tertiary(),
                                rodakos_theme_text_primary());
    lv_obj_set_pos(cancel, 14, 86);
    lv_obj_add_event_cb(cancel, [](lv_event_t* e) {
        static_cast<FileManagerApp*>(lv_event_get_user_data(e))->CloseDeleteDialog();
    }, LV_EVENT_CLICKED, this);
    auto* confirm = CreateButton(box, nullptr, "删除", 128, 38, rodakos_theme_error(),
                                 rodakos_theme_on_error());
    lv_obj_set_pos(confirm, 154, 86);
    lv_obj_add_event_cb(confirm, [](lv_event_t* e) {
        static_cast<FileManagerApp*>(lv_event_get_user_data(e))->ConfirmDelete();
    }, LV_EVENT_CLICKED, this);
}

void FileManagerApp::CloseDeleteDialog() {
    if (delete_layer_ != nullptr && lv_obj_is_valid(delete_layer_)) {
        // Deleting from inside one of its own button callbacks is not safe; defer it.
        lv_obj_delete_async(delete_layer_);
    }
    delete_layer_ = nullptr;
}

void FileManagerApp::ConfirmDelete() {
    CloseDeleteDialog();
    const std::string app_path = JoinPath(current_path_, info_entry_.name);
    if (InSystemFolder(app_path)) return;
    file_service_ = context_->services().file_service();
    if (file_service_ == nullptr) {
        ui_->ShowToastUnlocked("存储服务不可用");
        return;
    }
    // DeleteFile checks write leases itself (WritePathConflicts). Wrapping it in
    // WithWriteLease for the same path would hold that lease and make it fail with EBUSY.
    errno = 0;
    if (!file_service_->DeleteFile(app_path)) {
        const int error = errno;
        ESP_LOGW(TAG, "Delete %s failed: errno=%d", app_path.c_str(), error);
        ui_->ShowToastUnlocked(error == EBUSY ? "文件正在使用，稍后再删" : "删除失败");
        return;
    }
    ESP_LOGI(TAG, "Deleted %s", app_path.c_str());
    LoadDirectory(current_path_);
    RebuildList();
    RequestCapacity();
    ui_->ShowToastUnlocked("已删除");
}

void FileManagerApp::NavigateBack() {
    if (delete_layer_ != nullptr) {
        CloseDeleteDialog();
        return;
    }
    if (view_mode_ == ViewMode::kInfo && info_from_preview_) {
        info_from_preview_ = false;
        SetHidden(info_body_, true);
        ShowImagePreview(preview_entry_);
        return;
    }
    if (view_mode_ == ViewMode::kPreview || view_mode_ == ViewMode::kInfo) {
        ShowListView();
        return;
    }

    if (current_path_ != "/") {
        LoadDirectory(ParentPath());
        RebuildList();
        return;
    }

    NavigateHome();
}

void FileManagerApp::NavigateHome() {
    ESP_LOGI(TAG, "Returning home");
    lv_async_call_cancel(DeferReturnHome, context_);
    if (lv_async_call(DeferReturnHome, context_) != LV_RESULT_OK) {
        ui_->ShowToastUnlocked("返回桌面失败，请重试");
    }
}

void FileManagerApp::ShowPreviewError(const char* message) {
    ReleaseCurrentImage();
    SetHidden(preview_image_, true);
    lv_label_set_text(preview_error_label_, message);
    SetHidden(preview_notice_, false);
    SetHidden(preview_top_, false);
    SetHidden(preview_bottom_, true);
    preview_chrome_visible_ = true;
}

void FileManagerApp::RefreshDirectory() {
    if (LoadDirectory(current_path_)) {
        RebuildList();
        RequestCapacity();
        ui_->ShowToastUnlocked("已刷新");
    } else {
        RebuildList();
    }
}

void FileManagerApp::RequestCapacity() {
    if (capacity_ == nullptr || file_service_ == nullptr || directory_status_ != DirectoryStatus::kReady) return;
    bool expected = false;
    if (!capacity_->probing.compare_exchange_strong(expected, true)) return;
    auto* args = new (std::nothrow) CapacityProbeArgs{capacity_, capacity_.get(), file_service_};
    auto task = [](void* arg) {
        auto* probe = static_cast<CapacityProbeArgs*>(arg);
        auto* snapshot = static_cast<CapacitySnapshot*>(probe->snapshot);
        rodakos::FileService::Capacity capacity{};
        const bool known = probe->files->IsMounted() && probe->files->GetCapacity(capacity);
        {
            std::lock_guard<std::mutex> lock(snapshot->mutex);
            snapshot->known = known;
            snapshot->capacity = capacity;
        }
        snapshot->probing.store(false);
        delete probe;
        vTaskDelete(nullptr);
    };
    if (args == nullptr || xTaskCreate(task, "files_cap", 4096, args, 2, nullptr) != pdPASS) {
        delete args;
        capacity_->probing.store(false);
        return;
    }
    if (capacity_timer_ == nullptr) {
        capacity_timer_ = lv_timer_create([](lv_timer_t* timer) {
            static_cast<FileManagerApp*>(lv_timer_get_user_data(timer))->PollCapacity();
        }, kCapacityPollMs, this);
    }
    PollCapacity();
}

void FileManagerApp::PollCapacity() {
    if (capacity_ == nullptr) return;
    if (view_mode_ == ViewMode::kList) UpdateStrip();
    if (!capacity_->probing.load() && capacity_timer_ != nullptr) {
        lv_timer_delete(capacity_timer_);
        capacity_timer_ = nullptr;
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

void RegisterFileManagerApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "files",
        .title = "Files",
        .icon = ICON_FOLDER,
        .category = PhoneAppCategory::kTools,
        .icon_tint = PhoneAppIconTint::kNeutral,
        .capabilities = PhoneCapability::kStorage,
        .show_on_home = true,
        .aliases = {"file", "files", "manager", "sd", "文件", "文件管理"},
        .create = []() { return std::make_unique<FileManagerApp>(); },
    });
}

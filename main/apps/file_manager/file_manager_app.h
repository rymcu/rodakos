#pragma once

#include "phone_os/phone_app.h"
#include "phone_ui/image_library.h"
#include "rodakos_adapters/file_service.h"

#include <lvgl.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

class FileManagerApp final : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override {}
    void OnPause() override {}
    void OnDestroy() override;
    bool OnBackRequested() override;

    void OpenEntry(size_t index);

private:
    enum class ViewMode {
        kList,
        kPreview,
        kInfo,
    };

    enum class DirectoryStatus {
        kReady,
        kServiceUnavailable,
        kStorageUnavailable,
        kMissing,
        kReadFailed,
    };

    // Free space is read by a short task (f_getfree can take seconds on a slow card); the list
    // shows whatever the last probe wrote. The task keeps its own reference after OnDestroy.
    struct CapacitySnapshot {
        std::mutex mutex;
        bool known = false;
        rodakos::FileService::Capacity capacity{};
        std::atomic<bool> probing{false};
    };

    void CreateUi();
    void RebuildList();
    void UpdateHeader();
    void UpdateStrip();
    bool LoadDirectory(const std::string& path);
    void ShowListView();
    void ShowImagePreview(const rodakos::FileEntry& entry);
    void ShowPreviewError(const char* message);
    void TogglePreviewChrome();
    void ShowFileInfo(const rodakos::FileEntry& entry);
    void AskDelete();
    void CloseDeleteDialog();
    void ConfirmDelete();
    void NavigateBack();
    void NavigateHome();
    void RefreshDirectory();
    void ReleaseCurrentImage();
    void RequestCapacity();
    void PollCapacity();
    const char* DirectoryTitle() const;
    const char* DirectoryHint() const;

    std::string ParentPath() const;

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::FileService* file_service_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* header_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* refresh_button_ = nullptr;
    lv_obj_t* strip_ = nullptr;
    lv_obj_t* strip_left_label_ = nullptr;
    lv_obj_t* strip_right_label_ = nullptr;
    lv_obj_t* list_container_ = nullptr;
    lv_obj_t* status_box_ = nullptr;
    lv_obj_t* preview_body_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* preview_top_ = nullptr;
    lv_obj_t* preview_bottom_ = nullptr;
    lv_obj_t* preview_title_label_ = nullptr;
    lv_obj_t* preview_size_label_ = nullptr;
    lv_obj_t* preview_notice_ = nullptr;
    lv_obj_t* preview_error_label_ = nullptr;
    lv_obj_t* preview_retry_button_ = nullptr;
    lv_obj_t* info_body_ = nullptr;
    lv_obj_t* info_icon_label_ = nullptr;
    lv_obj_t* info_title_label_ = nullptr;
    lv_obj_t* info_path_label_ = nullptr;
    lv_obj_t* info_type_label_ = nullptr;
    lv_obj_t* info_size_label_ = nullptr;
    lv_obj_t* info_time_label_ = nullptr;
    lv_obj_t* info_delete_button_ = nullptr;
    lv_obj_t* delete_layer_ = nullptr;
    lv_timer_t* capacity_timer_ = nullptr;

    std::shared_ptr<rodakos::LvglImage> current_image_;
    std::shared_ptr<CapacitySnapshot> capacity_;
    std::vector<rodakos::FileEntry> entries_;
    rodakos::FileEntry preview_entry_{};
    rodakos::FileEntry info_entry_{};
    std::string current_path_ = "/";
    ViewMode view_mode_ = ViewMode::kList;
    // The info page was opened from a full-screen preview; back returns there.
    bool info_from_preview_ = false;
    bool preview_chrome_visible_ = true;
    DirectoryStatus directory_status_ = DirectoryStatus::kServiceUnavailable;
};

void RegisterFileManagerApp(PhoneAppRegistry& registry);

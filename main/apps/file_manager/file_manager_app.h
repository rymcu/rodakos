#pragma once

#include "phone_os/phone_app.h"
#include "phone_ui/image_library.h"
#include "rodakos_adapters/file_service.h"

#include <lvgl.h>

#include <memory>
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

    void CreateUi();
    void RebuildList();
    bool LoadDirectory(const std::string& path);
    void ShowListView();
    void ShowImagePreview(const rodakos::FileEntry& entry);
    void ShowPreviewError(const char* message);
    void ShowFileInfo(const rodakos::FileEntry& entry);
    void NavigateBack();
    void NavigateHome();
    void RefreshDirectory();
    void ReleaseCurrentImage();
    const char* DirectoryMessage() const;

    std::string ParentPath() const;
    std::string DisplayPath() const;

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::FileService* file_service_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* path_label_ = nullptr;
    lv_obj_t* list_container_ = nullptr;
    lv_obj_t* preview_body_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* preview_title_label_ = nullptr;
    lv_obj_t* preview_error_label_ = nullptr;
    lv_obj_t* preview_retry_button_ = nullptr;
    lv_obj_t* info_body_ = nullptr;
    lv_obj_t* info_title_label_ = nullptr;
    lv_obj_t* info_detail_label_ = nullptr;

    std::shared_ptr<rodakos::LvglImage> current_image_;
    std::vector<rodakos::FileEntry> entries_;
    rodakos::FileEntry preview_entry_{};
    std::string current_path_ = "/";
    ViewMode view_mode_ = ViewMode::kList;
    DirectoryStatus directory_status_ = DirectoryStatus::kServiceUnavailable;
};

void RegisterFileManagerApp(PhoneAppRegistry& registry);

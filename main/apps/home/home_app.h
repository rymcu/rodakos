#pragma once

#include "apps/home/home_layout_store.h"
#include "phone_os/phone_app.h"
#include "phone_ui/soft_keyboard.h"

#include <lvgl.h>

#include <optional>
#include <string>
#include <vector>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

class HomeApp final : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override {}
    void OnPause() override {}
    void OnDestroy() override;
    bool OnThemeChanged(PhoneAppContext& context) override;
    bool OnHomeRequested() override;
    void UpdateClock();

private:
    struct DeferredEditPayload;
    struct DeferredLaunchPayload;
    struct TilePayload;

    enum class CollectionKind {
        kNone,
        kFolder,
        kAllApps,
    };

    struct CollectionState {
        CollectionKind kind = CollectionKind::kNone;
        std::string folder_id;
    };

    enum class LayoutEditTargetKind {
        kRootItem,
        kFolderApp,
    };

    struct LayoutEditTarget {
        LayoutEditTargetKind kind = LayoutEditTargetKind::kRootItem;
        rodakos::HomeRootItemKey root_item;
        std::string folder_id;
        std::string app_id;
    };

    enum class HomeEditorPage {
        kOverview,
        kDestination,
        kDissolveConfirm,
    };

    enum class FolderNameMode {
        kNone,
        kCreate,
        kRename,
    };

    enum class DeferredEditAction {
        kOpen,
        kCloseCollection,
        kCancel,
        kSave,
        kOpenDestination,
        kCloseEditorSubview,
        kOpenCreateName,
        kOpenRenameName,
        kCloseFolderName,
        kApplyFolderName,
        kMoveToFolder,
        kMoveOut,
        kOpenDissolveConfirm,
        kDissolve,
    };

    enum class TileAction {
        kLaunchApp,
        kOpenFolder,
        kOpenAllApps,
        kCloseCollection,
        kMovePrevious,
        kMoveNext,
        kOpenDestination,
        kSelectDestination,
        kCreateFolder,
        kRenameFolder,
        kMoveOut,
        kOpenDissolveConfirm,
        kConfirmDissolve,
        kCloseEditorSubview,
        kApplyFolderName,
        kCancelFolderName,
        kCancelEditing,
        kSaveEditing,
    };

    static void AppButtonEvent(lv_event_t* event);
    static void AppButtonDeleteEvent(lv_event_t* event);
    static void TileviewEvent(lv_event_t* event);
    static void RunDeferredEdit(void* data);
    static void RunDeferredLaunch(void* data);
    static void RunDeferredPageWindow(void* data);

    bool CreateUi(PhoneAppContext& context);
    bool LoadLayout(PhoneAppContext& context);
    void DestroyUi();
    void ResetUiPointers();
    void QueueLaunch(const std::string& app_id);
    void CancelPendingLaunch();
    void QueuePageWindowRefresh();
    void CancelPendingPageWindowRefresh();
    size_t ActivePageIndex() const;
    bool PopulateHomePage(size_t page_index);
    bool RefreshHomePageWindow(size_t active_page);
    void UpdateActivePageDirections(size_t active_page);
    void QueueEditAction(
        DeferredEditAction action,
        std::optional<LayoutEditTarget> target = std::nullopt,
        std::string value = {});
    void CancelPendingEditAction();
    void UpdatePageIndicator();
    void BindTileAction(
        lv_obj_t* object,
        TileAction action,
        std::string id = {},
        std::optional<LayoutEditTarget> editable_target = std::nullopt);
    void ActivateTile(TileAction action, const std::string& id);
    void OpenFolder(const std::string& folder_id);
    void OpenAllApps();
    void OpenCollection(const std::string& title,
                        const std::vector<rodakos::HomeLayoutItem>& items,
                        CollectionState state);
    void AddCollectionRow(lv_obj_t* parent, const rodakos::HomeLayoutItem& item);
    void CloseCollection();
    void OpenLayoutEditor(const LayoutEditTarget& target);
    void CreateLayoutEditorUi();
    void CreateLayoutEditorOverviewUi();
    void CreateLayoutDestinationUi();
    void CreateLayoutDissolveUi();
    void CloseLayoutEditor();
    void ResetLayoutEditorPointers();
    bool HasEditingTarget() const;
    void MoveLayoutDraft(rodakos::HomeMoveDirection direction);
    void MoveLayoutDraftToFolder(const std::string& folder_id);
    void MoveLayoutDraftOut();
    void DissolveLayoutDraftFolder();
    void OpenFolderNameDialog(FolderNameMode mode);
    void CloseFolderNameDialog();
    void ApplyFolderName(std::string name);
    void SetEditorActionError(std::string message);
    void SaveLayoutDraft();
    void UpdateLayoutEditor();
    void ShowLayoutSaveFailure(rodakos::HomeLayoutSaveStatus status);
    bool RebuildUi();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* footer_ = nullptr;
    lv_obj_t* tileview_ = nullptr;
    lv_obj_t* collection_view_ = nullptr;
    lv_obj_t* layout_editor_ = nullptr;
    lv_obj_t* editor_position_label_ = nullptr;
    lv_obj_t* editor_error_label_ = nullptr;
    lv_obj_t* editor_previous_button_ = nullptr;
    lv_obj_t* editor_next_button_ = nullptr;
    lv_obj_t* editor_done_button_ = nullptr;
    lv_obj_t* editor_primary_action_ = nullptr;
    lv_obj_t* editor_secondary_action_ = nullptr;
    lv_obj_t* folder_name_dialog_ = nullptr;
    lv_obj_t* folder_name_textarea_ = nullptr;
    lv_obj_t* folder_name_error_label_ = nullptr;
    lv_obj_t* page_indicator_ = nullptr;
    lv_obj_t* clock_label_ = nullptr;
    lv_obj_t* status_cluster_ = nullptr;
    lv_obj_t* wifi_label_ = nullptr;
    lv_timer_t* clock_timer_ = nullptr;
    DeferredEditPayload* pending_edit_ = nullptr;
    DeferredLaunchPayload* pending_launch_ = nullptr;
    bool edit_action_pending_ = false;
    bool launch_pending_ = false;
    bool page_window_refresh_pending_ = false;
    size_t pending_page_window_ = 0;
    bool editor_locked_ = false;
    std::optional<LayoutEditTarget> editing_target_;
    std::optional<rodakos::HomeRootItemKey> editor_focus_item_;
    std::optional<rodakos::HomeLayoutSaveStatus> editor_error_status_;
    std::string editor_action_error_;
    CollectionState collection_state_;
    HomeEditorPage editor_page_ = HomeEditorPage::kOverview;
    FolderNameMode folder_name_mode_ = FolderNameMode::kNone;
    rodakos::HomeLayout draft_layout_;
    std::vector<lv_obj_t*> page_tiles_;
    std::vector<bool> page_populated_;
    rodakos::HomeLayoutStore layout_store_;
    rodakos::HomeLayout layout_;
    rodakos::HomeLayoutProjection projection_;
    bool layout_write_allowed_ = false;
    SoftKeyboard soft_keyboard_;
};

void RegisterHomeApp(PhoneAppRegistry& registry);

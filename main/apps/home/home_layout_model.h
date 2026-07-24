#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rodakos {

inline constexpr uint32_t kHomeLayoutSchemaVersion = 1;
inline constexpr size_t kHomeLayoutMaxJsonBytes = 3072;
inline constexpr size_t kHomeFolderMaxApps = 12;
inline constexpr size_t kHomeFolderNameMaxBytes = 24;
inline constexpr size_t kHomeFolderIdGenerationAttempts = 32;
inline constexpr size_t kHomeAppsPerPage = 12;
inline constexpr size_t kHomeManagedRootCapacity = 96;

enum class HomeLayoutItemType {
    kApp,
    kFolder,
};

struct HomeLayoutItem {
    HomeLayoutItemType type = HomeLayoutItemType::kApp;
    std::string id;
    std::string name;
    std::vector<std::string> apps;

    static HomeLayoutItem App(std::string app_id);
    static HomeLayoutItem Folder(std::string folder_id,
                                 std::string folder_name,
                                 std::vector<std::string> app_ids);
};

bool operator==(const HomeLayoutItem& lhs, const HomeLayoutItem& rhs);
bool operator!=(const HomeLayoutItem& lhs, const HomeLayoutItem& rhs);

struct HomeLayout {
    uint32_t version = kHomeLayoutSchemaVersion;
    uint32_t revision = 0;
    std::vector<HomeLayoutItem> items;
};

bool operator==(const HomeLayout& lhs, const HomeLayout& rhs);
bool operator!=(const HomeLayout& lhs, const HomeLayout& rhs);

enum class HomeLayoutValidationStatus {
    kOk,
    kUnsupportedVersion,
    kInvalidAppId,
    kInvalidAppShape,
    kInvalidFolderId,
    kInvalidFolderName,
    kEmptyFolder,
    kFolderOverflow,
    kDuplicateApp,
    kDuplicateFolder,
    kInvalidItemType,
};

bool IsValidHomeUtf8(std::string_view value);
bool IsValidHomeFolderId(std::string_view value);
bool IsValidHomeFolderName(std::string_view value);
HomeLayoutValidationStatus ValidateHomeLayout(const HomeLayout& layout);

HomeLayout MakeDefaultHomeLayout(const std::vector<std::string>& visible_app_ids);

struct HomeAppIdMigration {
    uint32_t version = 0;
    std::string old_id;
    std::string new_id;
};

HomeLayout ApplyHomeAppIdMigrations(
    const HomeLayout& saved,
    const std::vector<HomeAppIdMigration>& migrations);

struct HomeLayoutReconcileResult {
    HomeLayout layout;
    bool changed = false;
};

struct HomeLayoutProjection {
    std::vector<HomeLayoutItem> managed_items;
    std::vector<HomeLayoutItem> overflow_items;

    bool has_all_apps() const { return !overflow_items.empty(); }
    size_t root_slot_count() const {
        return managed_items.size() + (has_all_apps() ? size_t{1} : size_t{0});
    }
};

HomeLayoutProjection ProjectHomeLayout(const HomeLayout& layout);

size_t HomeLayoutPageCount(size_t root_slot_count);
size_t ClampHomeLayoutPage(size_t root_slot_count, size_t requested_page);

struct HomePageRenderWindow {
    size_t first_page = 0;
    size_t page_count = 0;

    bool Contains(size_t page) const {
        return page >= first_page && page - first_page < page_count;
    }
};

HomePageRenderWindow ResolveHomePageRenderWindow(
    size_t total_pages, size_t active_page);

struct HomePageRenderPlan {
    HomePageRenderWindow window;
    std::array<size_t, 3> populate_order = {};
    size_t populate_count = 0;
};

HomePageRenderPlan ResolveHomePageRenderPlan(
    size_t total_pages, size_t active_page);

struct HomeRootItemKey {
    HomeLayoutItemType type = HomeLayoutItemType::kApp;
    std::string id;
};

bool operator==(const HomeRootItemKey& lhs, const HomeRootItemKey& rhs);
bool operator!=(const HomeRootItemKey& lhs, const HomeRootItemKey& rhs);

struct HomePageAnchor {
    std::optional<HomeRootItemKey> first_visible_item;
    size_t fallback_page = 0;
};

HomePageAnchor CaptureHomePageAnchor(
    const HomeLayoutProjection& projection, size_t page);
size_t ResolveHomePage(
    const HomeLayoutProjection& projection, const HomePageAnchor& anchor);

class HomePageSession {
public:
    void Capture(const HomeLayoutProjection& projection, size_t page);
    void Focus(const HomeLayoutProjection& projection, const HomeRootItemKey& item);
    size_t Restore(const HomeLayoutProjection& projection) const;

private:
    HomePageAnchor anchor_;
};

enum class HomeMoveDirection {
    kPrevious,
    kNext,
};

enum class HomeLayoutEditStatus {
    kApplied,
    kNoChange,
    kInvalidModel,
    kInvalidRequest,
    kItemNotFound,
    kFolderIdConflict,
    kFolderFull,
};

struct HomeLayoutEditResult {
    HomeLayoutEditStatus status = HomeLayoutEditStatus::kInvalidModel;
    std::optional<HomeLayout> candidate;
    size_t destination_index = 0;
};

HomeLayoutEditResult MoveHomeManagedRootItem(
    const HomeLayout& layout,
    const HomeRootItemKey& item,
    HomeMoveDirection direction);

std::optional<size_t> FindHomeRootItem(
    const HomeLayout& layout, const HomeRootItemKey& item);

std::optional<std::string> GenerateUniqueHomeFolderId(
    const HomeLayout& layout,
    const std::function<uint32_t()>& entropy,
    size_t max_attempts = kHomeFolderIdGenerationAttempts);

HomeLayoutEditResult CreateHomeFolder(
    const HomeLayout& layout,
    std::string folder_id,
    std::string folder_name,
    const std::vector<HomeRootItemKey>& root_apps);
HomeLayoutEditResult RenameHomeFolder(
    const HomeLayout& layout,
    const HomeRootItemKey& folder,
    std::string folder_name);
HomeLayoutEditResult MoveHomeRootAppIntoFolder(
    const HomeLayout& layout,
    const HomeRootItemKey& app,
    const HomeRootItemKey& folder);
HomeLayoutEditResult MoveHomeFolderApp(
    const HomeLayout& layout,
    const HomeRootItemKey& folder,
    std::string_view app_id,
    HomeMoveDirection direction);
HomeLayoutEditResult MoveHomeFolderAppToRoot(
    const HomeLayout& layout,
    const HomeRootItemKey& folder,
    std::string_view app_id);
HomeLayoutEditResult MoveHomeFolderAppIntoFolder(
    const HomeLayout& layout,
    const HomeRootItemKey& source_folder,
    std::string_view app_id,
    const HomeRootItemKey& destination_folder);
HomeLayoutEditResult DissolveHomeFolder(
    const HomeLayout& layout, const HomeRootItemKey& folder);

bool CanMoveHomeFolderApp(
    const HomeLayout& layout,
    const HomeRootItemKey& folder,
    std::string_view app_id,
    HomeMoveDirection direction);

std::optional<size_t> FindHomeManagedRootItem(
    const HomeLayoutProjection& projection, const HomeRootItemKey& item);
bool CanMoveHomeManagedRootItem(
    const HomeLayoutProjection& projection,
    const HomeRootItemKey& item,
    HomeMoveDirection direction);

struct HomeLayoutEditorState {
    std::optional<size_t> position;
    size_t position_count = 0;
    bool can_previous = false;
    bool can_next = false;
    bool can_open_destination = false;
};

HomeLayoutEditorState ResolveHomeRootEditorState(
    const HomeLayout& layout, const HomeRootItemKey& item);
HomeLayoutEditorState ResolveHomeFolderAppEditorState(
    const HomeLayout& layout,
    const HomeRootItemKey& folder,
    std::string_view app_id);

HomeLayoutReconcileResult ReconcileHomeLayout(
    const HomeLayout& saved,
    const std::vector<std::string>& visible_app_ids,
    const std::vector<HomeAppIdMigration>& migrations = {});

class HomeLaunchGuard {
public:
    void BeginPress() { long_pressed_ = false; }
    void MarkLongPress() { long_pressed_ = true; }
    bool ShouldLaunchShortClick() const { return !long_pressed_; }

private:
    bool long_pressed_ = false;
};

}  // namespace rodakos

#include "apps/home/home_layout_model.h"

#include <algorithm>
#include <cstdio>
#include <inttypes.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace rodakos {
namespace {

bool IsContinuation(unsigned char value) {
    return value >= 0x80 && value <= 0xBF;
}

bool IsValidAppId(std::string_view value) {
    return !value.empty() && IsValidHomeUtf8(value);
}

bool IsValidMoveDirection(HomeMoveDirection direction) {
    return direction == HomeMoveDirection::kPrevious ||
           direction == HomeMoveDirection::kNext;
}

HomeLayoutEditResult EditFailure(HomeLayoutEditStatus status,
                                 size_t destination_index = 0) {
    HomeLayoutEditResult result;
    result.status = status;
    result.destination_index = destination_index;
    return result;
}

HomeLayoutEditResult AppliedEdit(HomeLayout candidate, size_t destination_index) {
    if (ValidateHomeLayout(candidate) != HomeLayoutValidationStatus::kOk) {
        return EditFailure(HomeLayoutEditStatus::kInvalidModel);
    }
    HomeLayoutEditResult result;
    result.status = HomeLayoutEditStatus::kApplied;
    result.candidate = std::move(candidate);
    result.destination_index = destination_index;
    return result;
}

}  // namespace

HomeLayoutItem HomeLayoutItem::App(std::string app_id) {
    HomeLayoutItem item;
    item.type = HomeLayoutItemType::kApp;
    item.id = std::move(app_id);
    return item;
}

HomeLayoutItem HomeLayoutItem::Folder(std::string folder_id,
                                      std::string folder_name,
                                      std::vector<std::string> app_ids) {
    HomeLayoutItem item;
    item.type = HomeLayoutItemType::kFolder;
    item.id = std::move(folder_id);
    item.name = std::move(folder_name);
    item.apps = std::move(app_ids);
    return item;
}

bool operator==(const HomeLayoutItem& lhs, const HomeLayoutItem& rhs) {
    return lhs.type == rhs.type && lhs.id == rhs.id && lhs.name == rhs.name && lhs.apps == rhs.apps;
}

bool operator!=(const HomeLayoutItem& lhs, const HomeLayoutItem& rhs) {
    return !(lhs == rhs);
}

bool operator==(const HomeLayout& lhs, const HomeLayout& rhs) {
    return lhs.version == rhs.version && lhs.revision == rhs.revision && lhs.items == rhs.items;
}

bool operator!=(const HomeLayout& lhs, const HomeLayout& rhs) {
    return !(lhs == rhs);
}

bool IsValidHomeUtf8(std::string_view value) {
    size_t index = 0;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first == 0) {
            return false;
        }
        if (first <= 0x7F) {
            ++index;
            continue;
        }

        if (first >= 0xC2 && first <= 0xDF) {
            if (index + 1 >= value.size() ||
                !IsContinuation(static_cast<unsigned char>(value[index + 1]))) {
                return false;
            }
            index += 2;
            continue;
        }

        if (first >= 0xE0 && first <= 0xEF) {
            if (index + 2 >= value.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[index + 1]);
            const auto third = static_cast<unsigned char>(value[index + 2]);
            const bool valid_second = first == 0xE0
                                          ? second >= 0xA0 && second <= 0xBF
                                      : first == 0xED
                                          ? second >= 0x80 && second <= 0x9F
                                          : IsContinuation(second);
            if (!valid_second || !IsContinuation(third)) {
                return false;
            }
            index += 3;
            continue;
        }

        if (first >= 0xF0 && first <= 0xF4) {
            if (index + 3 >= value.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[index + 1]);
            const auto third = static_cast<unsigned char>(value[index + 2]);
            const auto fourth = static_cast<unsigned char>(value[index + 3]);
            const bool valid_second = first == 0xF0
                                          ? second >= 0x90 && second <= 0xBF
                                      : first == 0xF4
                                          ? second >= 0x80 && second <= 0x8F
                                          : IsContinuation(second);
            if (!valid_second || !IsContinuation(third) || !IsContinuation(fourth)) {
                return false;
            }
            index += 4;
            continue;
        }

        return false;
    }
    return true;
}

bool IsValidHomeFolderId(std::string_view value) {
    if (value.size() != 10 || value[0] != 'f' || value[1] != '_') {
        return false;
    }
    return std::all_of(value.begin() + 2, value.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}

bool IsValidHomeFolderName(std::string_view value) {
    return value.size() <= kHomeFolderNameMaxBytes && IsValidHomeUtf8(value);
}

HomeLayoutValidationStatus ValidateHomeLayout(const HomeLayout& layout) {
    if (layout.version != kHomeLayoutSchemaVersion) {
        return HomeLayoutValidationStatus::kUnsupportedVersion;
    }

    std::unordered_set<std::string> app_ids;
    std::unordered_set<std::string> folder_ids;
    for (const auto& item : layout.items) {
        switch (item.type) {
            case HomeLayoutItemType::kApp:
                if (!IsValidAppId(item.id)) {
                    return HomeLayoutValidationStatus::kInvalidAppId;
                }
                if (!item.name.empty() || !item.apps.empty()) {
                    return HomeLayoutValidationStatus::kInvalidAppShape;
                }
                if (!app_ids.insert(item.id).second) {
                    return HomeLayoutValidationStatus::kDuplicateApp;
                }
                break;

            case HomeLayoutItemType::kFolder:
                if (!IsValidHomeFolderId(item.id)) {
                    return HomeLayoutValidationStatus::kInvalidFolderId;
                }
                if (!IsValidHomeFolderName(item.name)) {
                    return HomeLayoutValidationStatus::kInvalidFolderName;
                }
                if (item.apps.empty()) {
                    return HomeLayoutValidationStatus::kEmptyFolder;
                }
                if (item.apps.size() > kHomeFolderMaxApps) {
                    return HomeLayoutValidationStatus::kFolderOverflow;
                }
                if (!folder_ids.insert(item.id).second) {
                    return HomeLayoutValidationStatus::kDuplicateFolder;
                }
                for (const auto& app_id : item.apps) {
                    if (!IsValidAppId(app_id)) {
                        return HomeLayoutValidationStatus::kInvalidAppId;
                    }
                    if (!app_ids.insert(app_id).second) {
                        return HomeLayoutValidationStatus::kDuplicateApp;
                    }
                }
                break;

            default:
                return HomeLayoutValidationStatus::kInvalidItemType;
        }
    }
    return HomeLayoutValidationStatus::kOk;
}

HomeLayout MakeDefaultHomeLayout(const std::vector<std::string>& visible_app_ids) {
    HomeLayout layout;
    std::unordered_set<std::string> seen;
    for (const auto& app_id : visible_app_ids) {
        if (IsValidAppId(app_id) && seen.insert(app_id).second) {
            layout.items.push_back(HomeLayoutItem::App(app_id));
        }
    }
    return layout;
}

HomeLayout ApplyHomeAppIdMigrations(
    const HomeLayout& saved,
    const std::vector<HomeAppIdMigration>& migrations) {
    HomeLayout migrated = saved;
    std::unordered_map<std::string, std::string> mapping;
    std::unordered_set<std::string> ambiguous_sources;
    for (const auto& migration : migrations) {
        if (migration.version == 0 || !IsValidAppId(migration.old_id) ||
            !IsValidAppId(migration.new_id) || migration.old_id == migration.new_id) {
            continue;
        }
        const auto [it, inserted] = mapping.emplace(migration.old_id, migration.new_id);
        if (!inserted && it->second != migration.new_id) {
            ambiguous_sources.insert(migration.old_id);
        }
    }
    for (const auto& source : ambiguous_sources) {
        mapping.erase(source);
    }
    if (mapping.empty() && ambiguous_sources.empty()) {
        return migrated;
    }

    auto migrate_id = [&](std::string& app_id) {
        const std::string original = app_id;
        std::unordered_set<std::string> visited;
        while (true) {
            if (ambiguous_sources.find(app_id) != ambiguous_sources.end()) {
                app_id = original;
                return;
            }
            const auto next = mapping.find(app_id);
            if (next == mapping.end()) {
                return;
            }
            if (!visited.insert(app_id).second) {
                app_id = original;
                return;
            }
            app_id = next->second;
        }
    };
    for (auto& item : migrated.items) {
        if (item.type == HomeLayoutItemType::kApp) {
            migrate_id(item.id);
        } else if (item.type == HomeLayoutItemType::kFolder) {
            for (auto& app_id : item.apps) {
                migrate_id(app_id);
            }
        }
    }
    return migrated;
}

HomeLayoutProjection ProjectHomeLayout(const HomeLayout& layout) {
    HomeLayoutProjection projection;
    if (layout.items.size() <= kHomeManagedRootCapacity) {
        projection.managed_items = layout.items;
        return projection;
    }

    const size_t managed_count = kHomeManagedRootCapacity - 1;
    projection.managed_items.assign(layout.items.begin(),
                                    layout.items.begin() + managed_count);
    projection.overflow_items.assign(layout.items.begin() + managed_count,
                                     layout.items.end());
    return projection;
}

size_t HomeLayoutPageCount(size_t root_slot_count) {
    return root_slot_count == 0
               ? size_t{1}
               : (root_slot_count + kHomeAppsPerPage - 1) / kHomeAppsPerPage;
}

size_t ClampHomeLayoutPage(size_t root_slot_count, size_t requested_page) {
    return std::min(requested_page, HomeLayoutPageCount(root_slot_count) - 1);
}

HomePageRenderWindow ResolveHomePageRenderWindow(
    size_t total_pages, size_t active_page) {
    HomePageRenderWindow window;
    if (total_pages == 0) {
        return window;
    }

    const size_t active = std::min(active_page, total_pages - 1);
    window.first_page = active > 0 ? active - 1 : 0;
    const size_t last_page = std::min(active + 1, total_pages - 1);
    window.page_count = last_page - window.first_page + 1;
    return window;
}

HomePageRenderPlan ResolveHomePageRenderPlan(
    size_t total_pages, size_t active_page) {
    HomePageRenderPlan plan;
    plan.window = ResolveHomePageRenderWindow(total_pages, active_page);
    if (plan.window.page_count == 0) {
        return plan;
    }

    const size_t active = std::min(active_page, total_pages - 1);
    plan.populate_order[plan.populate_count++] = active;
    if (active > 0) {
        plan.populate_order[plan.populate_count++] = active - 1;
    }
    if (active + 1 < total_pages) {
        plan.populate_order[plan.populate_count++] = active + 1;
    }
    return plan;
}

bool operator==(const HomeRootItemKey& lhs, const HomeRootItemKey& rhs) {
    return lhs.type == rhs.type && lhs.id == rhs.id;
}

bool operator!=(const HomeRootItemKey& lhs, const HomeRootItemKey& rhs) {
    return !(lhs == rhs);
}

HomePageAnchor CaptureHomePageAnchor(
    const HomeLayoutProjection& projection, size_t page) {
    HomePageAnchor anchor;
    anchor.fallback_page = ClampHomeLayoutPage(projection.root_slot_count(), page);
    const size_t first_item = anchor.fallback_page * kHomeAppsPerPage;
    if (first_item < projection.managed_items.size()) {
        const auto& item = projection.managed_items[first_item];
        anchor.first_visible_item = HomeRootItemKey{item.type, item.id};
    }
    return anchor;
}

size_t ResolveHomePage(
    const HomeLayoutProjection& projection, const HomePageAnchor& anchor) {
    if (anchor.first_visible_item.has_value()) {
        const auto managed = std::find_if(
            projection.managed_items.begin(), projection.managed_items.end(),
            [&](const HomeLayoutItem& item) {
                return item.type == anchor.first_visible_item->type &&
                       item.id == anchor.first_visible_item->id;
            });
        if (managed != projection.managed_items.end()) {
            return static_cast<size_t>(managed - projection.managed_items.begin()) /
                   kHomeAppsPerPage;
        }
        const auto overflow = std::find_if(
            projection.overflow_items.begin(), projection.overflow_items.end(),
            [&](const HomeLayoutItem& item) {
                return item.type == anchor.first_visible_item->type &&
                       item.id == anchor.first_visible_item->id;
            });
        if (overflow != projection.overflow_items.end() && projection.has_all_apps()) {
            return (projection.root_slot_count() - 1) / kHomeAppsPerPage;
        }
    }
    return ClampHomeLayoutPage(projection.root_slot_count(), anchor.fallback_page);
}

void HomePageSession::Capture(const HomeLayoutProjection& projection, size_t page) {
    anchor_ = CaptureHomePageAnchor(projection, page);
}

void HomePageSession::Focus(
    const HomeLayoutProjection& projection, const HomeRootItemKey& item) {
    anchor_.first_visible_item = item;
    const auto managed = FindHomeManagedRootItem(projection, item);
    if (managed.has_value()) {
        anchor_.fallback_page = *managed / kHomeAppsPerPage;
        return;
    }
    const auto overflow = std::find_if(
        projection.overflow_items.begin(), projection.overflow_items.end(),
        [&](const HomeLayoutItem& candidate) {
            return candidate.type == item.type && candidate.id == item.id;
        });
    if (overflow != projection.overflow_items.end() && projection.has_all_apps()) {
        anchor_.fallback_page = (projection.root_slot_count() - 1) / kHomeAppsPerPage;
    }
}

size_t HomePageSession::Restore(const HomeLayoutProjection& projection) const {
    return ResolveHomePage(projection, anchor_);
}

HomeLayoutEditResult MoveHomeManagedRootItem(
    const HomeLayout& layout,
    const HomeRootItemKey& item,
    HomeMoveDirection direction) {
    HomeLayoutEditResult result;

    if (ValidateHomeLayout(layout) != HomeLayoutValidationStatus::kOk) {
        result.status = HomeLayoutEditStatus::kInvalidModel;
        return result;
    }
    if (!IsValidMoveDirection(direction)) {
        result.status = HomeLayoutEditStatus::kInvalidRequest;
        return result;
    }

    const auto source = std::find_if(
        layout.items.begin(), layout.items.end(), [&](const HomeLayoutItem& candidate) {
            return candidate.type == item.type && candidate.id == item.id;
        });
    if (source == layout.items.end()) {
        result.status = HomeLayoutEditStatus::kItemNotFound;
        return result;
    }

    const size_t source_index = static_cast<size_t>(source - layout.items.begin());
    const HomeLayoutProjection projection = ProjectHomeLayout(layout);
    if (!CanMoveHomeManagedRootItem(projection, item, direction)) {
        result.status = HomeLayoutEditStatus::kNoChange;
        result.destination_index = source_index;
        return result;
    }

    const size_t destination_index = direction == HomeMoveDirection::kPrevious
                                         ? source_index - 1
                                         : source_index + 1;
    HomeLayout candidate = layout;
    std::swap(candidate.items[source_index], candidate.items[destination_index]);
    if (ValidateHomeLayout(candidate) != HomeLayoutValidationStatus::kOk) {
        result.status = HomeLayoutEditStatus::kInvalidModel;
        return result;
    }

    result.status = HomeLayoutEditStatus::kApplied;
    result.candidate = std::move(candidate);
    result.destination_index = destination_index;
    return result;
}

std::optional<size_t> FindHomeRootItem(
    const HomeLayout& layout, const HomeRootItemKey& item) {
    const auto match = std::find_if(
        layout.items.begin(), layout.items.end(), [&](const HomeLayoutItem& candidate) {
            return candidate.type == item.type && candidate.id == item.id;
        });
    if (match == layout.items.end()) {
        return std::nullopt;
    }
    return static_cast<size_t>(match - layout.items.begin());
}

std::optional<std::string> GenerateUniqueHomeFolderId(
    const HomeLayout& layout,
    const std::function<uint32_t()>& entropy,
    size_t max_attempts) {
    if (ValidateHomeLayout(layout) != HomeLayoutValidationStatus::kOk || !entropy ||
        max_attempts == 0) {
        return std::nullopt;
    }

    std::unordered_set<std::string> folder_ids;
    for (const auto& item : layout.items) {
        if (item.type == HomeLayoutItemType::kFolder) {
            folder_ids.insert(item.id);
        }
    }
    for (size_t attempt = 0; attempt < max_attempts; ++attempt) {
        char buffer[11] = {};
        std::snprintf(buffer, sizeof(buffer), "f_%08" PRIx32, entropy());
        std::string candidate(buffer);
        if (folder_ids.find(candidate) == folder_ids.end()) {
            return candidate;
        }
    }
    return std::nullopt;
}

HomeLayoutEditResult CreateHomeFolder(
    const HomeLayout& layout,
    std::string folder_id,
    std::string folder_name,
    const std::vector<HomeRootItemKey>& root_apps) {
    if (ValidateHomeLayout(layout) != HomeLayoutValidationStatus::kOk) {
        return EditFailure(HomeLayoutEditStatus::kInvalidModel);
    }
    if (!IsValidHomeFolderId(folder_id) || !IsValidHomeFolderName(folder_name) ||
        root_apps.empty() || root_apps.size() > kHomeFolderMaxApps) {
        return EditFailure(HomeLayoutEditStatus::kInvalidRequest);
    }
    if (std::any_of(layout.items.begin(), layout.items.end(), [&](const HomeLayoutItem& item) {
            return item.type == HomeLayoutItemType::kFolder && item.id == folder_id;
        })) {
        return EditFailure(HomeLayoutEditStatus::kFolderIdConflict);
    }

    std::vector<size_t> source_indices;
    source_indices.reserve(root_apps.size());
    for (const auto& app : root_apps) {
        if (app.type != HomeLayoutItemType::kApp) {
            return EditFailure(HomeLayoutEditStatus::kInvalidRequest);
        }
        const auto source = FindHomeRootItem(layout, app);
        if (!source.has_value()) {
            return EditFailure(HomeLayoutEditStatus::kItemNotFound);
        }
        if (std::find(source_indices.begin(), source_indices.end(), *source) !=
            source_indices.end()) {
            return EditFailure(HomeLayoutEditStatus::kInvalidRequest);
        }
        source_indices.push_back(*source);
    }
    std::sort(source_indices.begin(), source_indices.end());

    std::vector<std::string> members;
    members.reserve(source_indices.size());
    for (const size_t source : source_indices) {
        members.push_back(layout.items[source].id);
    }

    const size_t destination_index = source_indices.front();
    HomeLayout candidate = layout;
    std::vector<HomeLayoutItem> items;
    items.reserve(layout.items.size() - source_indices.size() + 1);
    for (size_t index = 0; index < layout.items.size(); ++index) {
        if (index == destination_index) {
            items.push_back(HomeLayoutItem::Folder(
                std::move(folder_id), std::move(folder_name), std::move(members)));
        }
        if (!std::binary_search(source_indices.begin(), source_indices.end(), index)) {
            items.push_back(layout.items[index]);
        }
    }
    candidate.items = std::move(items);
    return AppliedEdit(std::move(candidate), destination_index);
}

HomeLayoutEditResult RenameHomeFolder(
    const HomeLayout& layout,
    const HomeRootItemKey& folder,
    std::string folder_name) {
    if (ValidateHomeLayout(layout) != HomeLayoutValidationStatus::kOk) {
        return EditFailure(HomeLayoutEditStatus::kInvalidModel);
    }
    if (folder.type != HomeLayoutItemType::kFolder ||
        !IsValidHomeFolderName(folder_name)) {
        return EditFailure(HomeLayoutEditStatus::kInvalidRequest);
    }
    const auto folder_index = FindHomeRootItem(layout, folder);
    if (!folder_index.has_value()) {
        return EditFailure(HomeLayoutEditStatus::kItemNotFound);
    }
    if (layout.items[*folder_index].name == folder_name) {
        return EditFailure(HomeLayoutEditStatus::kNoChange, *folder_index);
    }

    HomeLayout candidate = layout;
    candidate.items[*folder_index].name = std::move(folder_name);
    return AppliedEdit(std::move(candidate), *folder_index);
}

HomeLayoutEditResult MoveHomeRootAppIntoFolder(
    const HomeLayout& layout,
    const HomeRootItemKey& app,
    const HomeRootItemKey& folder) {
    if (ValidateHomeLayout(layout) != HomeLayoutValidationStatus::kOk) {
        return EditFailure(HomeLayoutEditStatus::kInvalidModel);
    }
    if (app.type != HomeLayoutItemType::kApp ||
        folder.type != HomeLayoutItemType::kFolder) {
        return EditFailure(HomeLayoutEditStatus::kInvalidRequest);
    }
    const auto app_index = FindHomeRootItem(layout, app);
    const auto folder_index = FindHomeRootItem(layout, folder);
    if (!app_index.has_value() || !folder_index.has_value()) {
        return EditFailure(HomeLayoutEditStatus::kItemNotFound);
    }
    if (layout.items[*folder_index].apps.size() >= kHomeFolderMaxApps) {
        return EditFailure(HomeLayoutEditStatus::kFolderFull);
    }

    HomeLayout candidate = layout;
    candidate.items[*folder_index].apps.push_back(app.id);
    candidate.items.erase(candidate.items.begin() + static_cast<std::ptrdiff_t>(*app_index));
    const size_t destination_index =
        *folder_index - (*app_index < *folder_index ? size_t{1} : size_t{0});
    return AppliedEdit(std::move(candidate), destination_index);
}

bool CanMoveHomeFolderApp(
    const HomeLayout& layout,
    const HomeRootItemKey& folder,
    std::string_view app_id,
    HomeMoveDirection direction) {
    if (!IsValidMoveDirection(direction) ||
        ValidateHomeLayout(layout) != HomeLayoutValidationStatus::kOk ||
        folder.type != HomeLayoutItemType::kFolder) {
        return false;
    }
    const auto folder_index = FindHomeRootItem(layout, folder);
    if (!folder_index.has_value()) {
        return false;
    }
    const auto& members = layout.items[*folder_index].apps;
    const auto member = std::find(members.begin(), members.end(), app_id);
    if (member == members.end()) {
        return false;
    }
    const size_t member_index = static_cast<size_t>(member - members.begin());
    return direction == HomeMoveDirection::kPrevious
               ? member_index > 0
               : member_index + 1 < members.size();
}

HomeLayoutEditResult MoveHomeFolderApp(
    const HomeLayout& layout,
    const HomeRootItemKey& folder,
    std::string_view app_id,
    HomeMoveDirection direction) {
    if (ValidateHomeLayout(layout) != HomeLayoutValidationStatus::kOk) {
        return EditFailure(HomeLayoutEditStatus::kInvalidModel);
    }
    if (!IsValidMoveDirection(direction) ||
        folder.type != HomeLayoutItemType::kFolder || app_id.empty()) {
        return EditFailure(HomeLayoutEditStatus::kInvalidRequest);
    }
    const auto folder_index = FindHomeRootItem(layout, folder);
    if (!folder_index.has_value()) {
        return EditFailure(HomeLayoutEditStatus::kItemNotFound);
    }
    const auto& members = layout.items[*folder_index].apps;
    const auto member = std::find(members.begin(), members.end(), app_id);
    if (member == members.end()) {
        return EditFailure(HomeLayoutEditStatus::kItemNotFound);
    }
    const size_t member_index = static_cast<size_t>(member - members.begin());
    if (!CanMoveHomeFolderApp(layout, folder, app_id, direction)) {
        return EditFailure(HomeLayoutEditStatus::kNoChange, member_index);
    }
    const size_t destination_index = direction == HomeMoveDirection::kPrevious
                                         ? member_index - 1
                                         : member_index + 1;
    HomeLayout candidate = layout;
    std::swap(candidate.items[*folder_index].apps[member_index],
              candidate.items[*folder_index].apps[destination_index]);
    return AppliedEdit(std::move(candidate), destination_index);
}

HomeLayoutEditResult MoveHomeFolderAppToRoot(
    const HomeLayout& layout,
    const HomeRootItemKey& folder,
    std::string_view app_id) {
    if (ValidateHomeLayout(layout) != HomeLayoutValidationStatus::kOk) {
        return EditFailure(HomeLayoutEditStatus::kInvalidModel);
    }
    if (folder.type != HomeLayoutItemType::kFolder || app_id.empty()) {
        return EditFailure(HomeLayoutEditStatus::kInvalidRequest);
    }
    const auto folder_index = FindHomeRootItem(layout, folder);
    if (!folder_index.has_value()) {
        return EditFailure(HomeLayoutEditStatus::kItemNotFound);
    }
    const auto& members = layout.items[*folder_index].apps;
    const auto member = std::find(members.begin(), members.end(), app_id);
    if (member == members.end()) {
        return EditFailure(HomeLayoutEditStatus::kItemNotFound);
    }

    HomeLayout candidate = layout;
    auto& candidate_members = candidate.items[*folder_index].apps;
    const size_t member_index = static_cast<size_t>(member - members.begin());
    if (candidate_members.size() == 1) {
        candidate.items[*folder_index] = HomeLayoutItem::App(std::string(app_id));
        return AppliedEdit(std::move(candidate), *folder_index);
    }
    candidate_members.erase(
        candidate_members.begin() + static_cast<std::ptrdiff_t>(member_index));
    const size_t destination_index = *folder_index + 1;
    candidate.items.insert(
        candidate.items.begin() + static_cast<std::ptrdiff_t>(destination_index),
        HomeLayoutItem::App(std::string(app_id)));
    return AppliedEdit(std::move(candidate), destination_index);
}

HomeLayoutEditResult MoveHomeFolderAppIntoFolder(
    const HomeLayout& layout,
    const HomeRootItemKey& source_folder,
    std::string_view app_id,
    const HomeRootItemKey& destination_folder) {
    if (ValidateHomeLayout(layout) != HomeLayoutValidationStatus::kOk) {
        return EditFailure(HomeLayoutEditStatus::kInvalidModel);
    }
    if (source_folder.type != HomeLayoutItemType::kFolder ||
        destination_folder.type != HomeLayoutItemType::kFolder ||
        source_folder == destination_folder || app_id.empty()) {
        return EditFailure(HomeLayoutEditStatus::kInvalidRequest);
    }
    const auto source_index = FindHomeRootItem(layout, source_folder);
    const auto destination_index = FindHomeRootItem(layout, destination_folder);
    if (!source_index.has_value() || !destination_index.has_value()) {
        return EditFailure(HomeLayoutEditStatus::kItemNotFound);
    }
    const auto& source_members = layout.items[*source_index].apps;
    const auto source_member = std::find(source_members.begin(), source_members.end(), app_id);
    if (source_member == source_members.end()) {
        return EditFailure(HomeLayoutEditStatus::kItemNotFound);
    }
    if (layout.items[*destination_index].apps.size() >= kHomeFolderMaxApps) {
        return EditFailure(HomeLayoutEditStatus::kFolderFull);
    }

    HomeLayout candidate = layout;
    const size_t member_index =
        static_cast<size_t>(source_member - source_members.begin());
    candidate.items[*source_index].apps.erase(
        candidate.items[*source_index].apps.begin() +
        static_cast<std::ptrdiff_t>(member_index));
    size_t final_destination = *destination_index;
    if (candidate.items[*source_index].apps.empty()) {
        candidate.items.erase(
            candidate.items.begin() + static_cast<std::ptrdiff_t>(*source_index));
        if (*source_index < *destination_index) {
            --final_destination;
        }
    }
    candidate.items[final_destination].apps.push_back(std::string(app_id));
    return AppliedEdit(std::move(candidate), final_destination);
}

HomeLayoutEditResult DissolveHomeFolder(
    const HomeLayout& layout, const HomeRootItemKey& folder) {
    if (ValidateHomeLayout(layout) != HomeLayoutValidationStatus::kOk) {
        return EditFailure(HomeLayoutEditStatus::kInvalidModel);
    }
    if (folder.type != HomeLayoutItemType::kFolder) {
        return EditFailure(HomeLayoutEditStatus::kInvalidRequest);
    }
    const auto folder_index = FindHomeRootItem(layout, folder);
    if (!folder_index.has_value()) {
        return EditFailure(HomeLayoutEditStatus::kItemNotFound);
    }

    std::vector<HomeLayoutItem> members;
    members.reserve(layout.items[*folder_index].apps.size());
    for (const auto& app_id : layout.items[*folder_index].apps) {
        members.push_back(HomeLayoutItem::App(app_id));
    }
    HomeLayout candidate = layout;
    candidate.items.erase(
        candidate.items.begin() + static_cast<std::ptrdiff_t>(*folder_index));
    candidate.items.insert(
        candidate.items.begin() + static_cast<std::ptrdiff_t>(*folder_index),
        members.begin(), members.end());
    return AppliedEdit(std::move(candidate), *folder_index);
}

std::optional<size_t> FindHomeManagedRootItem(
    const HomeLayoutProjection& projection, const HomeRootItemKey& item) {
    const auto match = std::find_if(
        projection.managed_items.begin(), projection.managed_items.end(),
        [&](const HomeLayoutItem& candidate) {
            return candidate.type == item.type && candidate.id == item.id;
        });
    if (match == projection.managed_items.end()) {
        return std::nullopt;
    }
    return static_cast<size_t>(match - projection.managed_items.begin());
}

bool CanMoveHomeManagedRootItem(
    const HomeLayoutProjection& projection,
    const HomeRootItemKey& item,
    HomeMoveDirection direction) {
    if (!IsValidMoveDirection(direction)) {
        return false;
    }
    const auto index = FindHomeManagedRootItem(projection, item);
    if (!index.has_value()) {
        return false;
    }
    return direction == HomeMoveDirection::kPrevious
               ? *index > 0
               : *index + 1 < projection.managed_items.size();
}

HomeLayoutEditorState ResolveHomeRootEditorState(
    const HomeLayout& layout, const HomeRootItemKey& item) {
    HomeLayoutEditorState state;
    if (ValidateHomeLayout(layout) != HomeLayoutValidationStatus::kOk) {
        return state;
    }

    const HomeLayoutProjection projection = ProjectHomeLayout(layout);
    const auto managed_position = FindHomeManagedRootItem(projection, item);
    if (managed_position.has_value()) {
        state.position = managed_position;
        state.position_count = projection.managed_items.size();
        state.can_previous = CanMoveHomeManagedRootItem(
            projection, item, HomeMoveDirection::kPrevious);
        state.can_next = CanMoveHomeManagedRootItem(
            projection, item, HomeMoveDirection::kNext);
        state.can_open_destination = item.type == HomeLayoutItemType::kApp;
        return state;
    }

    state.position = FindHomeRootItem(layout, item);
    if (state.position.has_value()) {
        state.position_count = layout.items.size();
    }
    return state;
}

HomeLayoutEditorState ResolveHomeFolderAppEditorState(
    const HomeLayout& layout,
    const HomeRootItemKey& folder,
    std::string_view app_id) {
    HomeLayoutEditorState state;
    if (ValidateHomeLayout(layout) != HomeLayoutValidationStatus::kOk ||
        folder.type != HomeLayoutItemType::kFolder) {
        return state;
    }

    const auto folder_index = FindHomeRootItem(layout, folder);
    if (folder_index.has_value()) {
        const auto& members = layout.items[*folder_index].apps;
        const auto member = std::find(members.begin(), members.end(), app_id);
        if (member != members.end()) {
            state.position = static_cast<size_t>(member - members.begin());
            state.position_count = members.size();
        }
    }
    if (!state.position.has_value()) {
        return state;
    }

    state.can_previous = CanMoveHomeFolderApp(
        layout, folder, app_id, HomeMoveDirection::kPrevious);
    state.can_next = CanMoveHomeFolderApp(
        layout, folder, app_id, HomeMoveDirection::kNext);
    const HomeLayoutProjection projection = ProjectHomeLayout(layout);
    state.can_open_destination = std::any_of(
        projection.managed_items.begin(), projection.managed_items.end(),
        [&](const HomeLayoutItem& item) {
            return item.type == HomeLayoutItemType::kFolder &&
                   item.id != folder.id &&
                   item.apps.size() < kHomeFolderMaxApps;
        });
    return state;
}

HomeLayoutReconcileResult ReconcileHomeLayout(
    const HomeLayout& saved,
    const std::vector<std::string>& visible_app_ids,
    const std::vector<HomeAppIdMigration>& migrations) {
    const HomeLayout migrated = ApplyHomeAppIdMigrations(saved, migrations);
    HomeLayout resolved;
    resolved.revision = migrated.revision;

    std::unordered_set<std::string> visible(visible_app_ids.begin(), visible_app_ids.end());
    std::unordered_set<std::string> seen_apps;
    std::unordered_set<std::string> seen_folders;

    auto append_root_app = [&](const std::string& app_id) {
        if (IsValidAppId(app_id) && visible.find(app_id) != visible.end() &&
            seen_apps.insert(app_id).second) {
            resolved.items.push_back(HomeLayoutItem::App(app_id));
        }
    };

    for (const auto& item : migrated.items) {
        if (item.type == HomeLayoutItemType::kApp) {
            append_root_app(item.id);
            continue;
        }
        if (item.type != HomeLayoutItemType::kFolder) {
            continue;
        }

        const bool folder_shape_valid =
            IsValidHomeFolderId(item.id) && IsValidHomeFolderName(item.name);
        const bool keep_folder = folder_shape_valid && seen_folders.insert(item.id).second;
        if (!keep_folder) {
            for (const auto& app_id : item.apps) {
                append_root_app(app_id);
            }
            continue;
        }

        std::vector<std::string> folder_apps;
        std::vector<std::string> overflow_apps;
        for (const auto& app_id : item.apps) {
            if (visible.find(app_id) == visible.end() || !seen_apps.insert(app_id).second) {
                continue;
            }
            if (folder_apps.size() < kHomeFolderMaxApps) {
                folder_apps.push_back(app_id);
            } else {
                overflow_apps.push_back(app_id);
            }
        }

        if (!folder_apps.empty()) {
            resolved.items.push_back(HomeLayoutItem::Folder(item.id, item.name, std::move(folder_apps)));
        }
        for (const auto& app_id : overflow_apps) {
            resolved.items.push_back(HomeLayoutItem::App(app_id));
        }
    }

    for (const auto& app_id : visible_app_ids) {
        append_root_app(app_id);
    }

    const bool changed = resolved != saved;
    HomeLayoutReconcileResult result;
    result.layout = std::move(resolved);
    result.changed = changed;
    return result;
}

}  // namespace rodakos

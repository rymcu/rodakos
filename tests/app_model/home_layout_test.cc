#include "test_framework.h"

#include "apps/home/home_layout_codec.h"
#include "apps/home/home_layout_model.h"
#include "apps/home/home_layout_store.h"
#include "settings.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using rodakos::HomeLayout;
using rodakos::HomeLayoutItem;

std::vector<std::string> AppIds(size_t count, const std::string& prefix = "app") {
    std::vector<std::string> result;
    result.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        result.push_back(prefix + std::to_string(index));
    }
    return result;
}

std::vector<std::string> RootAppIds(const HomeLayout& layout) {
    std::vector<std::string> result;
    for (const auto& item : layout.items) {
        if (item.type == rodakos::HomeLayoutItemType::kApp) {
            result.push_back(item.id);
        }
    }
    return result;
}

std::vector<std::string> AllAppIds(const HomeLayout& layout) {
    std::vector<std::string> result;
    for (const auto& item : layout.items) {
        if (item.type == rodakos::HomeLayoutItemType::kApp) {
            result.push_back(item.id);
        } else if (item.type == rodakos::HomeLayoutItemType::kFolder) {
            result.insert(result.end(), item.apps.begin(), item.apps.end());
        }
    }
    return result;
}

void CheckAppliedEdit(const HomeLayout& original,
                      const rodakos::HomeLayoutEditResult& result) {
    RODAK_CHECK_EQ(result.status, rodakos::HomeLayoutEditStatus::kApplied);
    RODAK_CHECK(result.candidate.has_value());
    RODAK_CHECK_EQ(result.candidate->revision, original.revision);
    RODAK_CHECK_EQ(rodakos::ValidateHomeLayout(*result.candidate),
                   rodakos::HomeLayoutValidationStatus::kOk);
    auto before = AllAppIds(original);
    auto after = AllAppIds(*result.candidate);
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    RODAK_CHECK_EQ(after, before);
}

std::string EncodeOrFail(const HomeLayout& layout) {
    const auto encoded = rodakos::EncodeHomeLayout(layout);
    RODAK_CHECK_EQ(encoded.status, rodakos::HomeLayoutEncodeStatus::kOk);
    return encoded.json;
}

}  // namespace

RODAK_TEST("Home layout validates canonical IDs folders and UTF-8 boundaries") {
    HomeLayout layout;
    layout.items = {
        HomeLayoutItem::App("settings"),
        HomeLayoutItem::Folder("f_7a31c4e2", std::string(24, 'a'), {"files"}),
    };
    RODAK_CHECK_EQ(rodakos::ValidateHomeLayout(layout),
                   rodakos::HomeLayoutValidationStatus::kOk);

    layout.items[1].name.push_back('b');
    RODAK_CHECK_EQ(rodakos::ValidateHomeLayout(layout),
                   rodakos::HomeLayoutValidationStatus::kInvalidFolderName);
    layout.items[1].name = "tools";
    layout.items[1].id = "f_7A31c4e2";
    RODAK_CHECK_EQ(rodakos::ValidateHomeLayout(layout),
                   rodakos::HomeLayoutValidationStatus::kInvalidFolderId);

    RODAK_CHECK(rodakos::IsValidHomeUtf8("plain"));
    RODAK_CHECK(rodakos::IsValidHomeUtf8("\xE5\xB7\xA5\xE5\x85\xB7"));
    RODAK_CHECK_FALSE(rodakos::IsValidHomeUtf8("\xE4\xB8"));
    RODAK_CHECK_FALSE(rodakos::IsValidHomeUtf8("\xC0\xAF"));
    RODAK_CHECK_FALSE(rodakos::IsValidHomeUtf8("\xED\xA0\x80"));
}

RODAK_TEST("Home layout codec round-trips a deterministic compact document") {
    HomeLayout layout;
    layout.revision = 4;
    layout.items = {
        HomeLayoutItem::App("settings"),
        HomeLayoutItem::Folder("f_7a31c4e2", "\xE5\xB7\xA5\xE5\x85\xB7",
                               {"files", "gyro", "system"}),
    };

    const auto encoded = rodakos::EncodeHomeLayout(layout);
    RODAK_CHECK_EQ(encoded.status, rodakos::HomeLayoutEncodeStatus::kOk);
    RODAK_CHECK(encoded.json.size() <= rodakos::kHomeLayoutMaxJsonBytes);

    const auto decoded = rodakos::DecodeHomeLayout(encoded.json);
    RODAK_CHECK_EQ(decoded.status, rodakos::HomeLayoutDecodeStatus::kOk);
    RODAK_CHECK_EQ(decoded.layout, layout);
    RODAK_CHECK_EQ(rodakos::EncodeHomeLayout(decoded.layout).json, encoded.json);
}

RODAK_TEST("Home layout codec rejects malformed documents but preserves repairable entries") {
    const std::vector<std::string> malformed = {
        "{",
        "[]",
        "{\"v\":1,\"items\":[]}",
        "{\"v\":1,\"rev\":-1,\"items\":[]}",
        "{\"v\":1,\"rev\":1.5,\"items\":[]}",
        "{\"v\":1,\"v\":1,\"rev\":0,\"items\":[]}",
        "{\"v\":1,\"rev\":0,\"items\":[],\"extra\":true}",
        "{\"v\":1,\"rev\":0,\"items\":[{\"type\":\"widget\",\"id\":\"x\"}]}",
        "{\"v\":1,\"rev\":0,\"items\":[{\"type\":\"app\",\"id\":\"a\",\"apps\":[\"b\"]}]}",
        "{\"v\":1,\"rev\":0,\"items\":[{\"type\":\"folder\",\"id\":\"bad\",\"name\":\"x\",\"apps\":[]}]}",
        "{\"v\":1,\"rev\":0,\"items\":[{\"type\":\"app\",\"id\":\"settings\\u0000evil\"}]}",
        "{\"v\":1,\"rev\":0,\"items\":[]} trailing",
    };
    for (const auto& json : malformed) {
        RODAK_CHECK_EQ(rodakos::DecodeHomeLayout(json).status,
                       rodakos::HomeLayoutDecodeStatus::kMalformed);
    }

    const auto repairable = rodakos::DecodeHomeLayout(
        "{\"v\":1,\"rev\":2,\"items\":["
        "{\"type\":\"app\",\"id\":\"settings\"},"
        "{\"type\":\"app\",\"id\":\"settings\"},"
        "{\"type\":\"folder\",\"id\":\"f_00000001\",\"name\":\"Tools\","
        "\"apps\":[\"files\",\"files\"]}]}");
    RODAK_CHECK_EQ(repairable.status, rodakos::HomeLayoutDecodeStatus::kOk);
    RODAK_CHECK_EQ(repairable.layout.items.size(), size_t{3});

    const auto escaped_literal = rodakos::DecodeHomeLayout(
        "{\"v\":1,\"rev\":0,\"items\":[{\"type\":\"app\",\"id\":\"a\\\\u0000\"}]}");
    RODAK_CHECK_EQ(escaped_literal.status, rodakos::HomeLayoutDecodeStatus::kOk);
    RODAK_CHECK_EQ(escaped_literal.layout.items[0].id, std::string("a\\u0000"));
}

RODAK_TEST("Home layout codec rejects unknown versions and enforces the byte limit before parsing") {
    const auto future = rodakos::DecodeHomeLayout("{\"v\":2,\"future\":true}");
    RODAK_CHECK_EQ(future.status, rodakos::HomeLayoutDecodeStatus::kUnsupportedVersion);
    RODAK_CHECK_EQ(future.source_version, uint32_t{2});
    RODAK_CHECK_EQ(
        rodakos::DecodeHomeLayout("{\"v\":2,\"future\":\"\\u0000\"}").status,
        rodakos::HomeLayoutDecodeStatus::kUnsupportedVersion);

    std::string exact_limit = "{\"v\":1,\"rev\":0,\"items\":[]}";
    exact_limit.append(rodakos::kHomeLayoutMaxJsonBytes - exact_limit.size(), ' ');
    RODAK_CHECK_EQ(exact_limit.size(), rodakos::kHomeLayoutMaxJsonBytes);
    RODAK_CHECK_EQ(rodakos::DecodeHomeLayout(exact_limit).status,
                   rodakos::HomeLayoutDecodeStatus::kOk);
    exact_limit.push_back(' ');
    RODAK_CHECK_EQ(rodakos::DecodeHomeLayout(exact_limit).status,
                   rodakos::HomeLayoutDecodeStatus::kTooLarge);

    HomeLayout oversized;
    for (size_t index = 0; index < 100; ++index) {
        oversized.items.push_back(HomeLayoutItem::App(
            "application-with-a-long-canonical-id-" + std::to_string(index)));
    }
    RODAK_CHECK_EQ(rodakos::EncodeHomeLayout(oversized).status,
                   rodakos::HomeLayoutEncodeStatus::kTooLarge);
}

RODAK_TEST("Home reconciliation uses exact IDs and appends new Registry entries deterministically") {
    HomeLayout saved;
    saved.revision = 7;
    saved.items = {
        HomeLayoutItem::App("A"),
        HomeLayoutItem::App("old-id"),
        HomeLayoutItem::App("hidden"),
        HomeLayoutItem::App("A"),
    };

    const auto result = rodakos::ReconcileHomeLayout(saved, {"a", "new-id", "A"});
    RODAK_CHECK(result.changed);
    RODAK_CHECK_EQ(result.layout.revision, uint32_t{7});
    RODAK_CHECK_EQ(RootAppIds(result.layout),
                   (std::vector<std::string>{"A", "a", "new-id"}));
    RODAK_CHECK_EQ(rodakos::ReconcileHomeLayout(result.layout, {"a", "new-id", "A"}).layout,
                   result.layout);
}

RODAK_TEST("Home reconciliation resolves an idempotent explicit ID migration graph") {
    HomeLayout saved;
    saved.items = {
        HomeLayoutItem::App("old-camera"),
        HomeLayoutItem::Folder("f_00000001", "Tools", {"old-files", "camera"}),
        HomeLayoutItem::App("legacy-camera"),
    };
    const std::vector<rodakos::HomeAppIdMigration> migrations = {
        {3, "legacy-camera", "camera"},
        {2, "files-v1", "files"},
        {1, "old-files", "files-v1"},
        {1, "old-camera", "camera"},
    };

    const auto result = rodakos::ReconcileHomeLayout(
        saved, {"camera", "files", "settings"}, migrations);
    RODAK_CHECK_EQ(result.layout.items.size(), size_t{3});
    RODAK_CHECK_EQ(result.layout.items[0], HomeLayoutItem::App("camera"));
    RODAK_CHECK_EQ(result.layout.items[1].type, rodakos::HomeLayoutItemType::kFolder);
    RODAK_CHECK_EQ(result.layout.items[1].apps, (std::vector<std::string>{"files"}));
    RODAK_CHECK_EQ(result.layout.items[2], HomeLayoutItem::App("settings"));

    HomeLayout chain;
    chain.items = {HomeLayoutItem::App("a")};
    const std::vector<rodakos::HomeAppIdMigration> same_version_chain = {
        {1, "b", "c"},
        {1, "a", "b"},
    };
    const HomeLayout migrated =
        rodakos::ApplyHomeAppIdMigrations(chain, same_version_chain);
    RODAK_CHECK_EQ(migrated.items[0].id, std::string("c"));
    RODAK_CHECK_EQ(rodakos::ApplyHomeAppIdMigrations(migrated, same_version_chain), migrated);

    const std::vector<rodakos::HomeAppIdMigration> reverse_version_chain = {
        {1, "b", "c"},
        {2, "a", "b"},
    };
    const HomeLayout globally_migrated =
        rodakos::ApplyHomeAppIdMigrations(chain, reverse_version_chain);
    RODAK_CHECK_EQ(globally_migrated.items[0].id, std::string("c"));
    RODAK_CHECK_EQ(rodakos::ApplyHomeAppIdMigrations(
                       globally_migrated, reverse_version_chain),
                   globally_migrated);

    const std::vector<rodakos::HomeAppIdMigration> invalid_group = {
        {1, "a", "b"},
        {1, "b", "a"},
        {2, "x", "y"},
        {2, "x", "z"},
    };
    HomeLayout invalid_sources;
    invalid_sources.items = {HomeLayoutItem::App("a"), HomeLayoutItem::App("x")};
    RODAK_CHECK_EQ(rodakos::ApplyHomeAppIdMigrations(invalid_sources, invalid_group),
                   invalid_sources);

    const std::vector<rodakos::HomeAppIdMigration> ambiguous_ancestor = {
        {1, "a", "x"},
        {2, "x", "y"},
        {3, "x", "z"},
    };
    RODAK_CHECK_EQ(rodakos::ApplyHomeAppIdMigrations(chain, ambiguous_ancestor), chain);
}

RODAK_TEST("Home reconciliation repairs folders without dropping overflow members") {
    const auto members = AppIds(15, "tool");
    HomeLayout saved;
    saved.items.push_back(HomeLayoutItem::Folder(
        "f_00000001", "Tools", std::vector<std::string>(members.begin(), members.begin() + 13)));
    saved.items.push_back(HomeLayoutItem::Folder("f_00000002", "Empty", {}));
    saved.items.push_back(HomeLayoutItem::Folder(
        "f_00000001", "Duplicate", {members[13], members[14], members[0]}));

    const auto result = rodakos::ReconcileHomeLayout(saved, members);
    RODAK_CHECK(result.changed);
    RODAK_CHECK_EQ(result.layout.items.size(), size_t{4});
    RODAK_CHECK_EQ(result.layout.items[0].type, rodakos::HomeLayoutItemType::kFolder);
    RODAK_CHECK_EQ(result.layout.items[0].apps.size(), rodakos::kHomeFolderMaxApps);
    RODAK_CHECK_EQ(result.layout.items[1].id, members[12]);
    RODAK_CHECK_EQ(result.layout.items[2].id, members[13]);
    RODAK_CHECK_EQ(result.layout.items[3].id, members[14]);
    RODAK_CHECK_EQ(rodakos::ValidateHomeLayout(result.layout),
                   rodakos::HomeLayoutValidationStatus::kOk);
}

RODAK_TEST("Home projection preserves 0 11 13 and 97 Registry entries within eight pages") {
    for (const size_t count : {size_t{0}, size_t{11}, size_t{13}, size_t{96}, size_t{97}}) {
        const HomeLayout layout = rodakos::MakeDefaultHomeLayout(AppIds(count));
        RODAK_CHECK_EQ(layout.items.size(), count);
        const size_t expected_pages = count == 0 ? 1 : std::min<size_t>(8, (count + 11) / 12);
        RODAK_CHECK_EQ(
            rodakos::HomeLayoutPageCount(
                rodakos::ProjectHomeLayout(layout).root_slot_count()),
            expected_pages);
        RODAK_CHECK_EQ(rodakos::ValidateHomeLayout(layout),
                       rodakos::HomeLayoutValidationStatus::kOk);
    }

    const auto overflow = rodakos::ProjectHomeLayout(
        rodakos::MakeDefaultHomeLayout(AppIds(97)));
    RODAK_CHECK_EQ(overflow.managed_items.size(), size_t{95});
    RODAK_CHECK_EQ(overflow.overflow_items.size(), size_t{2});
    RODAK_CHECK_EQ(overflow.root_slot_count(), size_t{96});
}

RODAK_TEST("Home page render window retains only the active page and its neighbors") {
    const auto empty = rodakos::ResolveHomePageRenderWindow(0, 0);
    RODAK_CHECK_EQ(empty.page_count, size_t{0});
    RODAK_CHECK_FALSE(empty.Contains(0));

    for (size_t total_pages = 1; total_pages <= 8; ++total_pages) {
        for (size_t active_page = 0; active_page < total_pages; ++active_page) {
            const auto window = rodakos::ResolveHomePageRenderWindow(
                total_pages, active_page);
            const auto plan = rodakos::ResolveHomePageRenderPlan(
                total_pages, active_page);
            RODAK_CHECK(window.Contains(active_page));
            RODAK_CHECK(window.page_count <= size_t{3});
            RODAK_CHECK(window.page_count * rodakos::kHomeAppsPerPage <= size_t{36});
            RODAK_CHECK_EQ(plan.window.first_page, window.first_page);
            RODAK_CHECK_EQ(plan.window.page_count, window.page_count);
            RODAK_CHECK_EQ(plan.populate_count, window.page_count);
            RODAK_CHECK_EQ(plan.populate_order[0], active_page);
            size_t order_index = 1;
            if (active_page > 0) {
                RODAK_CHECK(window.Contains(active_page - 1));
                RODAK_CHECK_EQ(plan.populate_order[order_index++], active_page - 1);
            }
            if (active_page + 1 < total_pages) {
                RODAK_CHECK(window.Contains(active_page + 1));
                RODAK_CHECK_EQ(plan.populate_order[order_index++], active_page + 1);
            }
            RODAK_CHECK_EQ(order_index, plan.populate_count);
            for (size_t page = 0; page < total_pages; ++page) {
                if (page + 1 < active_page || page > active_page + 1) {
                    RODAK_CHECK_FALSE(window.Contains(page));
                }
            }
        }
    }

    const auto clamped = rodakos::ResolveHomePageRenderWindow(8, 99);
    RODAK_CHECK_EQ(clamped.first_page, size_t{6});
    RODAK_CHECK_EQ(clamped.page_count, size_t{2});
    RODAK_CHECK(clamped.Contains(7));
}

RODAK_TEST("Home root moves are typed atomic adjacent edits") {
    HomeLayout layout;
    layout.revision = 9;
    layout.items = {
        HomeLayoutItem::App("settings"),
        HomeLayoutItem::App("f_00000001"),
        HomeLayoutItem::Folder("f_00000001", "Tools", {"files", "gyro"}),
        HomeLayoutItem::App("camera"),
    };
    const HomeLayout original = layout;
    const rodakos::HomeRootItemKey folder_key{
        rodakos::HomeLayoutItemType::kFolder, "f_00000001"};

    const auto moved = rodakos::MoveHomeManagedRootItem(
        layout, folder_key, rodakos::HomeMoveDirection::kPrevious);
    RODAK_CHECK_EQ(moved.status, rodakos::HomeLayoutEditStatus::kApplied);
    RODAK_CHECK(moved.candidate.has_value());
    RODAK_CHECK_EQ(moved.destination_index, size_t{1});
    RODAK_CHECK_EQ(moved.candidate->revision, layout.revision);
    RODAK_CHECK_EQ(moved.candidate->items[1].type,
                   rodakos::HomeLayoutItemType::kFolder);
    RODAK_CHECK_EQ(moved.candidate->items[1].apps,
                   (std::vector<std::string>{"files", "gyro"}));
    RODAK_CHECK_EQ(layout, original);
    RODAK_CHECK_EQ(rodakos::ValidateHomeLayout(*moved.candidate),
                   rodakos::HomeLayoutValidationStatus::kOk);

    const rodakos::HomeRootItemKey first_key{
        rodakos::HomeLayoutItemType::kApp, "settings"};
    const auto first_noop = rodakos::MoveHomeManagedRootItem(
        layout, first_key, rodakos::HomeMoveDirection::kPrevious);
    RODAK_CHECK_EQ(first_noop.status, rodakos::HomeLayoutEditStatus::kNoChange);
    RODAK_CHECK_FALSE(first_noop.candidate.has_value());

    const rodakos::HomeRootItemKey last_key{
        rodakos::HomeLayoutItemType::kApp, "camera"};
    RODAK_CHECK_EQ(
        rodakos::MoveHomeManagedRootItem(
            layout, last_key, rodakos::HomeMoveDirection::kNext).status,
        rodakos::HomeLayoutEditStatus::kNoChange);
    RODAK_CHECK_EQ(
        rodakos::MoveHomeManagedRootItem(
            layout,
            {rodakos::HomeLayoutItemType::kApp, "missing"},
            rodakos::HomeMoveDirection::kNext).status,
        rodakos::HomeLayoutEditStatus::kItemNotFound);

    HomeLayout invalid = layout;
    invalid.items.push_back(HomeLayoutItem::App("settings"));
    RODAK_CHECK_EQ(
        rodakos::MoveHomeManagedRootItem(
            invalid, first_key, rodakos::HomeMoveDirection::kNext).status,
        rodakos::HomeLayoutEditStatus::kInvalidModel);

    HomeLayout cross_page = rodakos::MakeDefaultHomeLayout(AppIds(13));
    const auto crossed = rodakos::MoveHomeManagedRootItem(
        cross_page,
        {rodakos::HomeLayoutItemType::kApp, "app11"},
        rodakos::HomeMoveDirection::kNext);
    RODAK_CHECK_EQ(crossed.status, rodakos::HomeLayoutEditStatus::kApplied);
    RODAK_CHECK_EQ(crossed.destination_index, size_t{12});
    RODAK_CHECK_EQ(crossed.candidate->items[12].id, std::string("app11"));
}

RODAK_TEST("Home managed move availability excludes the synthetic All Apps boundary") {
    const HomeLayout full_layout = rodakos::MakeDefaultHomeLayout(AppIds(96));
    const auto full = rodakos::ProjectHomeLayout(full_layout);
    RODAK_CHECK(rodakos::CanMoveHomeManagedRootItem(
        full, {rodakos::HomeLayoutItemType::kApp, "app94"},
        rodakos::HomeMoveDirection::kNext));
    RODAK_CHECK_FALSE(rodakos::CanMoveHomeManagedRootItem(
        full, {rodakos::HomeLayoutItemType::kApp, "app95"},
        rodakos::HomeMoveDirection::kNext));
    RODAK_CHECK_EQ(
        rodakos::MoveHomeManagedRootItem(
            full_layout,
            {rodakos::HomeLayoutItemType::kApp, "app94"},
            rodakos::HomeMoveDirection::kNext).status,
        rodakos::HomeLayoutEditStatus::kApplied);

    const HomeLayout overflow_layout = rodakos::MakeDefaultHomeLayout(AppIds(97));
    const auto overflow = rodakos::ProjectHomeLayout(overflow_layout);
    RODAK_CHECK_EQ(overflow.managed_items.size(), size_t{95});
    RODAK_CHECK(rodakos::CanMoveHomeManagedRootItem(
        overflow, {rodakos::HomeLayoutItemType::kApp, "app93"},
        rodakos::HomeMoveDirection::kNext));
    RODAK_CHECK_FALSE(rodakos::CanMoveHomeManagedRootItem(
        overflow, {rodakos::HomeLayoutItemType::kApp, "app94"},
        rodakos::HomeMoveDirection::kNext));
    RODAK_CHECK_FALSE(rodakos::CanMoveHomeManagedRootItem(
        overflow, {rodakos::HomeLayoutItemType::kApp, "app95"},
        rodakos::HomeMoveDirection::kPrevious));
    const auto blocked = rodakos::MoveHomeManagedRootItem(
        overflow_layout,
        {rodakos::HomeLayoutItemType::kApp, "app94"},
        rodakos::HomeMoveDirection::kNext);
    RODAK_CHECK_EQ(blocked.status, rodakos::HomeLayoutEditStatus::kNoChange);
    RODAK_CHECK_FALSE(blocked.candidate.has_value());
}

RODAK_TEST("Home move APIs reject invalid directions") {
    const auto invalid_direction = static_cast<rodakos::HomeMoveDirection>(2);
    HomeLayout layout;
    layout.items = {
        HomeLayoutItem::App("root"),
        HomeLayoutItem::Folder("f_00000001", "Tools", {"inside0", "inside1"}),
    };
    const rodakos::HomeRootItemKey root{
        rodakos::HomeLayoutItemType::kApp, "root"};
    const rodakos::HomeRootItemKey folder{
        rodakos::HomeLayoutItemType::kFolder, "f_00000001"};

    const auto root_result =
        rodakos::MoveHomeManagedRootItem(layout, root, invalid_direction);
    RODAK_CHECK_EQ(root_result.status,
                   rodakos::HomeLayoutEditStatus::kInvalidRequest);
    RODAK_CHECK_FALSE(root_result.candidate.has_value());
    RODAK_CHECK_FALSE(rodakos::CanMoveHomeManagedRootItem(
        rodakos::ProjectHomeLayout(layout), root, invalid_direction));

    const auto folder_result =
        rodakos::MoveHomeFolderApp(layout, folder, "inside0", invalid_direction);
    RODAK_CHECK_EQ(folder_result.status,
                   rodakos::HomeLayoutEditStatus::kInvalidRequest);
    RODAK_CHECK_FALSE(folder_result.candidate.has_value());
    RODAK_CHECK_FALSE(rodakos::CanMoveHomeFolderApp(
        layout, folder, "inside0", invalid_direction));
}

RODAK_TEST("Home adjacent moves preserve every root item across 0 to 100 entries") {
    for (size_t count = 0; count <= 100; ++count) {
        HomeLayout layout = rodakos::MakeDefaultHomeLayout(AppIds(count));
        layout.revision = 41;
        const HomeLayout original = layout;
        for (size_t index = 0; index < count; ++index) {
            const rodakos::HomeRootItemKey key{
                rodakos::HomeLayoutItemType::kApp, "app" + std::to_string(index)};
            for (const auto direction : {rodakos::HomeMoveDirection::kPrevious,
                                         rodakos::HomeMoveDirection::kNext}) {
                const bool can_move = rodakos::CanMoveHomeManagedRootItem(
                    rodakos::ProjectHomeLayout(layout), key, direction);
                const auto moved = rodakos::MoveHomeManagedRootItem(layout, key, direction);
                RODAK_CHECK_EQ(
                    moved.status,
                    can_move ? rodakos::HomeLayoutEditStatus::kApplied
                             : rodakos::HomeLayoutEditStatus::kNoChange);
                if (!can_move) {
                    RODAK_CHECK_FALSE(moved.candidate.has_value());
                    continue;
                }
                RODAK_CHECK(moved.candidate.has_value());
                RODAK_CHECK_EQ(moved.candidate->revision, layout.revision);
                RODAK_CHECK_EQ(rodakos::ValidateHomeLayout(*moved.candidate),
                               rodakos::HomeLayoutValidationStatus::kOk);
                auto before_ids = RootAppIds(layout);
                auto after_ids = RootAppIds(*moved.candidate);
                std::sort(before_ids.begin(), before_ids.end());
                std::sort(after_ids.begin(), after_ids.end());
                RODAK_CHECK_EQ(after_ids, before_ids);
            }
        }
        RODAK_CHECK_EQ(layout, original);
    }
}

RODAK_TEST("Home folder IDs and creation preserve typed identity and root order") {
    HomeLayout with_collision;
    with_collision.items = {
        HomeLayoutItem::Folder("f_00000000", "Existing", {"inside"}),
        HomeLayoutItem::App("f_00000000"),
        HomeLayoutItem::App("other"),
    };
    std::vector<uint32_t> entropy_words = {0, 0x00abcdef};
    size_t entropy_index = 0;
    const auto generated = rodakos::GenerateUniqueHomeFolderId(
        with_collision, [&]() { return entropy_words.at(entropy_index++); });
    RODAK_CHECK(generated.has_value());
    RODAK_CHECK_EQ(*generated, std::string("f_00abcdef"));
    RODAK_CHECK_EQ(entropy_index, size_t{2});
    RODAK_CHECK_FALSE(rodakos::GenerateUniqueHomeFolderId(
        with_collision, []() { return uint32_t{0}; }, 2).has_value());

    size_t calls = 0;
    RODAK_CHECK_FALSE(rodakos::GenerateUniqueHomeFolderId(
        with_collision, std::function<uint32_t()>{}).has_value());
    RODAK_CHECK_FALSE(rodakos::GenerateUniqueHomeFolderId(
        with_collision, [&]() {
            ++calls;
            return uint32_t{1};
        }, 0).has_value());
    RODAK_CHECK_EQ(calls, size_t{0});
    RODAK_CHECK_EQ(
        rodakos::GenerateUniqueHomeFolderId(
            with_collision, []() { return UINT32_MAX; }).value(),
        std::string("f_ffffffff"));
    RODAK_CHECK_FALSE(rodakos::GenerateUniqueHomeFolderId(
        with_collision, [&]() {
            ++calls;
            return uint32_t{0};
        }, 3).has_value());
    RODAK_CHECK_EQ(calls, size_t{3});

    HomeLayout invalid = with_collision;
    invalid.items.push_back(HomeLayoutItem::App("other"));
    size_t invalid_entropy_calls = 0;
    RODAK_CHECK_FALSE(rodakos::GenerateUniqueHomeFolderId(
        invalid, [&]() {
            ++invalid_entropy_calls;
            return uint32_t{1};
        }).has_value());
    RODAK_CHECK_EQ(invalid_entropy_calls, size_t{0});

    HomeLayout layout;
    layout.revision = 9;
    layout.items = {
        HomeLayoutItem::App("a"),
        HomeLayoutItem::App("b"),
        HomeLayoutItem::App("c"),
        HomeLayoutItem::App("d"),
    };
    const HomeLayout original = layout;
    const auto created = rodakos::CreateHomeFolder(
        layout, "f_00000001", "Tools",
        {{rodakos::HomeLayoutItemType::kApp, "d"},
         {rodakos::HomeLayoutItemType::kApp, "b"}});
    CheckAppliedEdit(layout, created);
    RODAK_CHECK_EQ(layout, original);
    RODAK_CHECK_EQ(created.destination_index, size_t{1});
    RODAK_CHECK_EQ(created.candidate->items.size(), size_t{3});
    RODAK_CHECK_EQ(created.candidate->items[0], HomeLayoutItem::App("a"));
    RODAK_CHECK_EQ(created.candidate->items[1],
                   HomeLayoutItem::Folder("f_00000001", "Tools", {"b", "d"}));
    RODAK_CHECK_EQ(created.candidate->items[2], HomeLayoutItem::App("c"));

    const auto singleton = rodakos::CreateHomeFolder(
        with_collision, "f_00000001", "Folder",
        {{rodakos::HomeLayoutItemType::kApp, "f_00000000"}});
    CheckAppliedEdit(with_collision, singleton);
    RODAK_CHECK_EQ(singleton.candidate->items[1].type,
                   rodakos::HomeLayoutItemType::kFolder);
    RODAK_CHECK_EQ(singleton.candidate->items[1].apps,
                   (std::vector<std::string>{"f_00000000"}));

    const std::vector<rodakos::HomeRootItemKey> none;
    RODAK_CHECK_EQ(
        rodakos::CreateHomeFolder(layout, "f_00000002", "Empty", none).status,
        rodakos::HomeLayoutEditStatus::kInvalidRequest);
    std::vector<rodakos::HomeRootItemKey> too_many;
    for (size_t index = 0; index < 13; ++index) {
        too_many.push_back({rodakos::HomeLayoutItemType::kApp,
                            "app" + std::to_string(index)});
    }
    RODAK_CHECK_EQ(
        rodakos::CreateHomeFolder(layout, "f_00000002", "Too many", too_many).status,
        rodakos::HomeLayoutEditStatus::kInvalidRequest);
    RODAK_CHECK_EQ(
        rodakos::CreateHomeFolder(
            layout, "bad", "Bad",
            {{rodakos::HomeLayoutItemType::kApp, "a"}}).status,
        rodakos::HomeLayoutEditStatus::kInvalidRequest);
    RODAK_CHECK_EQ(
        rodakos::CreateHomeFolder(
            layout, "f_00000002", std::string("\xE5\xB7", 2),
            {{rodakos::HomeLayoutItemType::kApp, "a"}}).status,
        rodakos::HomeLayoutEditStatus::kInvalidRequest);
    const auto duplicate_selection = rodakos::CreateHomeFolder(
        layout, "f_00000002", "Duplicate",
        {{rodakos::HomeLayoutItemType::kApp, "a"},
         {rodakos::HomeLayoutItemType::kApp, "a"}});
    RODAK_CHECK_EQ(duplicate_selection.status,
                   rodakos::HomeLayoutEditStatus::kInvalidRequest);
    RODAK_CHECK_FALSE(duplicate_selection.candidate.has_value());
    RODAK_CHECK_EQ(
        rodakos::CreateHomeFolder(
            with_collision, "f_00000000", "Conflict",
            {{rodakos::HomeLayoutItemType::kApp, "other"}}).status,
        rodakos::HomeLayoutEditStatus::kFolderIdConflict);
}

RODAK_TEST("Home folder rename enforces UTF-8 byte boundaries without changing revision") {
    HomeLayout layout;
    layout.revision = 17;
    layout.items = {
        HomeLayoutItem::Folder("f_00000001", "Tools", {"a", "b"}),
        HomeLayoutItem::App("c"),
    };
    const rodakos::HomeRootItemKey folder{
        rodakos::HomeLayoutItemType::kFolder, "f_00000001"};
    const std::string name_24 =
        "\xE5\xB7\xA5\xE5\x85\xB7\xE5\xB7\xA5\xE5\x85\xB7"
        "\xE5\xB7\xA5\xE5\x85\xB7\xE5\xB7\xA5\xE5\x85\xB7";
    RODAK_CHECK_EQ(name_24.size(), size_t{24});
    const auto renamed = rodakos::RenameHomeFolder(layout, folder, name_24);
    CheckAppliedEdit(layout, renamed);
    RODAK_CHECK_EQ(renamed.candidate->items[0].name, name_24);

    const auto same = rodakos::RenameHomeFolder(*renamed.candidate, folder, name_24);
    RODAK_CHECK_EQ(same.status, rodakos::HomeLayoutEditStatus::kNoChange);
    RODAK_CHECK_FALSE(same.candidate.has_value());
    RODAK_CHECK_EQ(
        rodakos::RenameHomeFolder(layout, folder, name_24 + "x").status,
        rodakos::HomeLayoutEditStatus::kInvalidRequest);
    RODAK_CHECK_EQ(
        rodakos::RenameHomeFolder(layout, folder, std::string("\xE4\xB8", 2)).status,
        rodakos::HomeLayoutEditStatus::kInvalidRequest);

    const auto empty = rodakos::RenameHomeFolder(layout, folder, "");
    CheckAppliedEdit(layout, empty);
    RODAK_CHECK(empty.candidate->items[0].name.empty());
    RODAK_CHECK(rodakos::IsValidHomeFolderName(""));
}

RODAK_TEST("Home folder member moves are atomic ordered and capacity bounded") {
    HomeLayout layout;
    layout.revision = 23;
    layout.items = {
        HomeLayoutItem::App("f_00000001"),
        HomeLayoutItem::Folder("f_00000001", "Source", {"a", "b", "c"}),
        HomeLayoutItem::App("root"),
        HomeLayoutItem::Folder("f_00000002", "Target", {"d"}),
    };
    const rodakos::HomeRootItemKey source_folder{
        rodakos::HomeLayoutItemType::kFolder, "f_00000001"};
    const rodakos::HomeRootItemKey target_folder{
        rodakos::HomeLayoutItemType::kFolder, "f_00000002"};

    const auto typed_move_in = rodakos::MoveHomeRootAppIntoFolder(
        layout,
        {rodakos::HomeLayoutItemType::kApp, "f_00000001"},
        source_folder);
    CheckAppliedEdit(layout, typed_move_in);
    RODAK_CHECK_EQ(typed_move_in.destination_index, size_t{0});
    RODAK_CHECK_EQ(typed_move_in.candidate->items[0].apps,
                   (std::vector<std::string>{"a", "b", "c", "f_00000001"}));

    RODAK_CHECK(rodakos::CanMoveHomeFolderApp(
        layout, source_folder, "b", rodakos::HomeMoveDirection::kPrevious));
    const auto reordered = rodakos::MoveHomeFolderApp(
        layout, source_folder, "b", rodakos::HomeMoveDirection::kPrevious);
    CheckAppliedEdit(layout, reordered);
    RODAK_CHECK_EQ(reordered.candidate->items[1].apps,
                   (std::vector<std::string>{"b", "a", "c"}));
    const auto boundary = rodakos::MoveHomeFolderApp(
        layout, source_folder, "a", rodakos::HomeMoveDirection::kPrevious);
    RODAK_CHECK_EQ(boundary.status, rodakos::HomeLayoutEditStatus::kNoChange);
    RODAK_CHECK_FALSE(boundary.candidate.has_value());

    const auto moved_between = rodakos::MoveHomeFolderAppIntoFolder(
        layout, source_folder, "b", target_folder);
    CheckAppliedEdit(layout, moved_between);
    RODAK_CHECK_EQ(moved_between.candidate->items[1].apps,
                   (std::vector<std::string>{"a", "c"}));
    RODAK_CHECK_EQ(moved_between.candidate->items[3].apps,
                   (std::vector<std::string>{"d", "b"}));

    const auto moved_out = rodakos::MoveHomeFolderAppToRoot(layout, source_folder, "b");
    CheckAppliedEdit(layout, moved_out);
    RODAK_CHECK_EQ(moved_out.destination_index, size_t{2});
    RODAK_CHECK_EQ(moved_out.candidate->items[1].apps,
                   (std::vector<std::string>{"a", "c"}));
    RODAK_CHECK_EQ(moved_out.candidate->items[2], HomeLayoutItem::App("b"));

    HomeLayout singleton;
    singleton.items = {
        HomeLayoutItem::App("before"),
        HomeLayoutItem::Folder("f_00000003", "One", {"only"}),
        HomeLayoutItem::App("after"),
    };
    const auto singleton_out = rodakos::MoveHomeFolderAppToRoot(
        singleton,
        {rodakos::HomeLayoutItemType::kFolder, "f_00000003"}, "only");
    CheckAppliedEdit(singleton, singleton_out);
    RODAK_CHECK_EQ(singleton_out.candidate->items,
                   (std::vector<HomeLayoutItem>{HomeLayoutItem::App("before"),
                                                HomeLayoutItem::App("only"),
                                                HomeLayoutItem::App("after")}));

    HomeLayout full = layout;
    full.items[3].apps = AppIds(12, "full");
    const auto full_result = rodakos::MoveHomeRootAppIntoFolder(
        full, {rodakos::HomeLayoutItemType::kApp, "root"}, target_folder);
    RODAK_CHECK_EQ(full_result.status, rodakos::HomeLayoutEditStatus::kFolderFull);
    RODAK_CHECK_FALSE(full_result.candidate.has_value());
}

RODAK_TEST("Home singleton folder moves repair destination indices atomically") {
    const rodakos::HomeRootItemKey source{
        rodakos::HomeLayoutItemType::kFolder, "f_00000001"};
    const rodakos::HomeRootItemKey destination{
        rodakos::HomeLayoutItemType::kFolder, "f_00000002"};

    HomeLayout source_before;
    source_before.items = {
        HomeLayoutItem::Folder("f_00000001", "Source", {"only"}),
        HomeLayoutItem::App("middle"),
        HomeLayoutItem::Folder("f_00000002", "Target", {"target"}),
    };
    const HomeLayout source_before_original = source_before;
    const auto moved_forward = rodakos::MoveHomeFolderAppIntoFolder(
        source_before, source, "only", destination);
    CheckAppliedEdit(source_before, moved_forward);
    RODAK_CHECK_EQ(source_before, source_before_original);
    RODAK_CHECK_EQ(moved_forward.destination_index, size_t{1});
    RODAK_CHECK_EQ(
        moved_forward.candidate->items,
        (std::vector<HomeLayoutItem>{
            HomeLayoutItem::App("middle"),
            HomeLayoutItem::Folder("f_00000002", "Target", {"target", "only"})}));

    HomeLayout source_after;
    source_after.items = {
        HomeLayoutItem::Folder("f_00000002", "Target", {"target"}),
        HomeLayoutItem::App("middle"),
        HomeLayoutItem::Folder("f_00000001", "Source", {"only"}),
    };
    const auto moved_backward = rodakos::MoveHomeFolderAppIntoFolder(
        source_after, source, "only", destination);
    CheckAppliedEdit(source_after, moved_backward);
    RODAK_CHECK_EQ(moved_backward.destination_index, size_t{0});
    RODAK_CHECK_EQ(
        moved_backward.candidate->items,
        (std::vector<HomeLayoutItem>{
            HomeLayoutItem::Folder("f_00000002", "Target", {"target", "only"}),
            HomeLayoutItem::App("middle")}));

    HomeLayout full = source_before;
    full.items[2].apps = AppIds(12, "full");
    const HomeLayout full_original = full;
    const auto rejected = rodakos::MoveHomeFolderAppIntoFolder(
        full, source, "only", destination);
    RODAK_CHECK_EQ(rejected.status, rodakos::HomeLayoutEditStatus::kFolderFull);
    RODAK_CHECK_FALSE(rejected.candidate.has_value());
    RODAK_CHECK_EQ(full, full_original);
}

RODAK_TEST("Home folder dissolve crosses the All Apps projection without losing order") {
    HomeLayout layout;
    layout.revision = 31;
    layout.items.push_back(
        HomeLayoutItem::Folder("f_00000001", "Pair", {"inside0", "inside1"}));
    for (const auto& app_id : AppIds(95)) {
        layout.items.push_back(HomeLayoutItem::App(app_id));
    }
    RODAK_CHECK_EQ(layout.items.size(), size_t{96});
    RODAK_CHECK_FALSE(rodakos::ProjectHomeLayout(layout).has_all_apps());

    const auto dissolved = rodakos::DissolveHomeFolder(
        layout, {rodakos::HomeLayoutItemType::kFolder, "f_00000001"});
    CheckAppliedEdit(layout, dissolved);
    RODAK_CHECK_EQ(dissolved.candidate->items.size(), size_t{97});
    RODAK_CHECK_EQ(dissolved.candidate->items[0], HomeLayoutItem::App("inside0"));
    RODAK_CHECK_EQ(dissolved.candidate->items[1], HomeLayoutItem::App("inside1"));
    const auto overflow = rodakos::ProjectHomeLayout(*dissolved.candidate);
    RODAK_CHECK(overflow.has_all_apps());
    RODAK_CHECK_EQ(overflow.managed_items.size(), size_t{95});
    RODAK_CHECK_EQ(overflow.overflow_items,
                   (std::vector<HomeLayoutItem>{HomeLayoutItem::App("app93"),
                                                HomeLayoutItem::App("app94")}));

    const auto regrouped = rodakos::CreateHomeFolder(
        *dissolved.candidate, "f_00000002", "Tail",
        {{rodakos::HomeLayoutItemType::kApp, "app94"},
         {rodakos::HomeLayoutItemType::kApp, "app93"}});
    CheckAppliedEdit(*dissolved.candidate, regrouped);
    RODAK_CHECK_EQ(regrouped.candidate->items.size(), size_t{96});
    RODAK_CHECK_FALSE(rodakos::ProjectHomeLayout(*regrouped.candidate).has_all_apps());
    RODAK_CHECK_EQ(regrouped.candidate->items.back().apps,
                   (std::vector<std::string>{"app93", "app94"}));

    HomeLayout singleton;
    singleton.items = {HomeLayoutItem::Folder("f_00000003", "One", {"only"})};
    const auto one = rodakos::DissolveHomeFolder(
        singleton, {rodakos::HomeLayoutItemType::kFolder, "f_00000003"});
    CheckAppliedEdit(singleton, one);
    RODAK_CHECK_EQ(one.candidate->items,
                   (std::vector<HomeLayoutItem>{HomeLayoutItem::App("only")}));

    HomeLayout tail_folder;
    for (const auto& app_id : AppIds(95)) {
        tail_folder.items.push_back(HomeLayoutItem::App(app_id));
    }
    tail_folder.items.push_back(
        HomeLayoutItem::Folder("f_00000004", "Tail", {"tail0", "tail1"}));
    const rodakos::HomeRootItemKey tail_key{
        rodakos::HomeLayoutItemType::kFolder, "f_00000004"};
    const auto tail_dissolved = rodakos::DissolveHomeFolder(tail_folder, tail_key);
    CheckAppliedEdit(tail_folder, tail_dissolved);
    const auto tail_projection = rodakos::ProjectHomeLayout(*tail_dissolved.candidate);
    RODAK_CHECK_EQ(tail_projection.overflow_items,
                   (std::vector<HomeLayoutItem>{HomeLayoutItem::App("tail0"),
                                                HomeLayoutItem::App("tail1")}));
    const auto tail_editor = rodakos::ResolveHomeRootEditorState(
        *tail_dissolved.candidate,
        {rodakos::HomeLayoutItemType::kApp, "tail0"});
    RODAK_CHECK_EQ(tail_editor.position, std::optional<size_t>{95});
    RODAK_CHECK_EQ(tail_editor.position_count, size_t{97});
    RODAK_CHECK_FALSE(tail_editor.can_previous);
    RODAK_CHECK_FALSE(tail_editor.can_next);
    RODAK_CHECK_FALSE(tail_editor.can_open_destination);
    rodakos::HomePageSession tail_session;
    tail_session.Focus(
        tail_projection,
        {rodakos::HomeLayoutItemType::kApp, "tail0"});
    RODAK_CHECK_EQ(tail_session.Restore(tail_projection), size_t{7});

    const auto tail_moved_out = rodakos::MoveHomeFolderAppToRoot(
        tail_folder, tail_key, "tail1");
    CheckAppliedEdit(tail_folder, tail_moved_out);
    const auto move_out_projection = rodakos::ProjectHomeLayout(*tail_moved_out.candidate);
    RODAK_CHECK_EQ(move_out_projection.overflow_items,
                   (std::vector<HomeLayoutItem>{
                       HomeLayoutItem::Folder("f_00000004", "Tail", {"tail0"}),
                       HomeLayoutItem::App("tail1")}));
    const auto move_out_editor = rodakos::ResolveHomeRootEditorState(
        *tail_moved_out.candidate,
        {rodakos::HomeLayoutItemType::kApp, "tail1"});
    RODAK_CHECK_EQ(move_out_editor.position, std::optional<size_t>{96});
    RODAK_CHECK_EQ(move_out_editor.position_count, size_t{97});
    RODAK_CHECK_FALSE(move_out_editor.can_previous);
    RODAK_CHECK_FALSE(move_out_editor.can_next);
    RODAK_CHECK_FALSE(move_out_editor.can_open_destination);
}

RODAK_TEST("Home page anchors survive recreation movement overflow and shrink") {
    HomeLayout layout = rodakos::MakeDefaultHomeLayout(AppIds(13));
    auto projection = rodakos::ProjectHomeLayout(layout);
    rodakos::HomePageSession session;
    session.Capture(projection, 1);
    RODAK_CHECK_EQ(session.Restore(projection), size_t{1});
    rodakos::HomePageSession focused_session;
    const rodakos::HomeRootItemKey focused_key{
        rodakos::HomeLayoutItemType::kApp, "app12"};
    focused_session.Focus(projection, focused_key);
    RODAK_CHECK_EQ(focused_session.Restore(projection), size_t{1});

    const auto moved = rodakos::MoveHomeManagedRootItem(
        layout,
        {rodakos::HomeLayoutItemType::kApp, "app12"},
        rodakos::HomeMoveDirection::kPrevious);
    RODAK_CHECK(moved.candidate.has_value());
    projection = rodakos::ProjectHomeLayout(*moved.candidate);
    RODAK_CHECK_EQ(session.Restore(projection), size_t{0});
    focused_session.Focus(projection, focused_key);
    RODAK_CHECK_EQ(focused_session.Restore(projection), size_t{0});

    const auto overflow = rodakos::ProjectHomeLayout(
        rodakos::MakeDefaultHomeLayout(AppIds(97)));
    rodakos::HomePageAnchor overflow_anchor;
    overflow_anchor.first_visible_item = rodakos::HomeRootItemKey{
        rodakos::HomeLayoutItemType::kApp, "app95"};
    overflow_anchor.fallback_page = 0;
    RODAK_CHECK_EQ(rodakos::ResolveHomePage(overflow, overflow_anchor), size_t{7});

    const auto one_page = rodakos::ProjectHomeLayout(
        rodakos::MakeDefaultHomeLayout(AppIds(11)));
    RODAK_CHECK_EQ(session.Restore(one_page), size_t{0});
    RODAK_CHECK_EQ(rodakos::HomePageSession().Restore(overflow), size_t{0});
    RODAK_CHECK_EQ(rodakos::HomeLayoutPageCount(0), size_t{1});
    RODAK_CHECK_EQ(rodakos::ClampHomeLayoutPage(13, 99), size_t{1});
}

RODAK_TEST("Home OTA reconciliation is RAM-only and keeps saved order where possible") {
    rodakos_test::ResetSettingsState();
    HomeLayout saved;
    saved.revision = 9;
    saved.items = {
        HomeLayoutItem::App("c"),
        HomeLayoutItem::App("a"),
        HomeLayoutItem::App("b"),
    };
    rodakos_test::SetCommittedSetting("home", "layout", EncodeOrFail(saved));

    rodakos::HomeLayoutStore store;
    const auto loaded = store.Load({"b", "c", "d"});
    RODAK_CHECK_EQ(loaded.status, rodakos::HomeLayoutLoadStatus::kLoaded);
    RODAK_CHECK(loaded.reconciled);
    RODAK_CHECK_EQ(RootAppIds(loaded.layout),
                   (std::vector<std::string>{"c", "b", "d"}));
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::GetCommittedSetting("home", "layout"), EncodeOrFail(saved));
}

RODAK_TEST("Home store saves explicitly and a new session restores the committed revision") {
    rodakos_test::ResetSettingsState();
    rodakos::HomeLayoutStore first_store;
    const auto initial = first_store.Load({"settings", "files", "gyro"});
    RODAK_CHECK_EQ(initial.status, rodakos::HomeLayoutLoadStatus::kMissing);
    RODAK_CHECK(initial.write_allowed);

    HomeLayout edited;
    edited.items = {
        HomeLayoutItem::Folder("f_00000001", "Tools", {"files", "gyro"}),
        HomeLayoutItem::App("settings"),
    };
    const auto saved = first_store.Save(edited);
    RODAK_CHECK_EQ(saved.status, rodakos::HomeLayoutSaveStatus::kSaved);
    RODAK_CHECK(saved.layout.has_value());
    RODAK_CHECK_EQ(saved.layout->revision, uint32_t{1});
    RODAK_CHECK_EQ(edited.revision, uint32_t{0});
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().destructor_commit_calls, 0);

    rodakos::HomeLayoutStore second_store;
    const auto restored = second_store.Load({"settings", "files", "gyro"});
    RODAK_CHECK_EQ(restored.status, rodakos::HomeLayoutLoadStatus::kLoaded);
    RODAK_CHECK_EQ(restored.layout, *saved.layout);
}

RODAK_TEST("Home discrete moves save once and persist across sessions") {
    rodakos_test::ResetSettingsState();
    rodakos::HomeLayoutStore store;
    const auto loaded = store.Load(AppIds(13));
    const rodakos::HomeRootItemKey key{
        rodakos::HomeLayoutItemType::kApp, "app11"};

    const auto moved = rodakos::MoveHomeManagedRootItem(
        loaded.layout, key, rodakos::HomeMoveDirection::kNext);
    RODAK_CHECK_EQ(moved.status, rodakos::HomeLayoutEditStatus::kApplied);
    RODAK_CHECK(moved.candidate.has_value());
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 0);

    const auto saved = store.Save(*moved.candidate);
    RODAK_CHECK_EQ(saved.status, rodakos::HomeLayoutSaveStatus::kSaved);
    RODAK_CHECK(saved.layout.has_value());
    RODAK_CHECK_EQ(saved.layout->revision, uint32_t{1});
    RODAK_CHECK_EQ(saved.layout->items[12].id, std::string("app11"));
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 1);

    rodakos::HomeLayoutStore restored_store;
    const auto restored = restored_store.Load(AppIds(13));
    RODAK_CHECK_EQ(restored.layout, *saved.layout);

    const auto no_change = rodakos::MoveHomeManagedRootItem(
        restored.layout,
        {rodakos::HomeLayoutItemType::kApp, "app0"},
        rodakos::HomeMoveDirection::kPrevious);
    RODAK_CHECK_EQ(no_change.status, rodakos::HomeLayoutEditStatus::kNoChange);
    RODAK_CHECK_FALSE(no_change.candidate.has_value());
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 1);
}

RODAK_TEST("Home multi-step folder draft commits once and restores exactly") {
    rodakos_test::ResetSettingsState();
    rodakos::HomeLayoutStore store;
    const auto loaded = store.Load({"a", "b", "c", "d"});
    RODAK_CHECK_EQ(loaded.status, rodakos::HomeLayoutLoadStatus::kMissing);

    const auto created = rodakos::CreateHomeFolder(
        loaded.layout, "f_00000001", "Folder",
        {{rodakos::HomeLayoutItemType::kApp, "a"}});
    const rodakos::HomeRootItemKey folder{
        rodakos::HomeLayoutItemType::kFolder, "f_00000001"};
    const auto renamed = rodakos::RenameHomeFolder(*created.candidate, folder, "Tools");
    const auto added = rodakos::MoveHomeRootAppIntoFolder(
        *renamed.candidate,
        {rodakos::HomeLayoutItemType::kApp, "b"}, folder);
    const auto reordered = rodakos::MoveHomeFolderApp(
        *added.candidate, folder, "b", rodakos::HomeMoveDirection::kPrevious);
    CheckAppliedEdit(*added.candidate, reordered);

    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 0);
    const auto saved = store.Save(*reordered.candidate);
    RODAK_CHECK_EQ(saved.status, rodakos::HomeLayoutSaveStatus::kSaved);
    RODAK_CHECK(saved.layout.has_value());
    RODAK_CHECK_EQ(saved.layout->revision, uint32_t{1});
    RODAK_CHECK_EQ(saved.layout->items[0],
                   HomeLayoutItem::Folder("f_00000001", "Tools", {"b", "a"}));
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 1);

    rodakos::HomeLayoutStore restored_store;
    const auto restored = restored_store.Load({"a", "b", "c", "d"});
    RODAK_CHECK_EQ(restored.layout, *saved.layout);
}

RODAK_TEST("Home compare failures are distinct and edit lock policy is host tested") {
    using Status = rodakos::HomeLayoutSaveStatus;
    for (const auto status : {Status::kNotLoaded, Status::kReadOnly,
                              Status::kResetNotAllowed, Status::kConflict,
                              Status::kCompareError, Status::kStaleRevision,
                              Status::kRevisionOverflow, Status::kWriteError,
                              Status::kWriteUncertain, Status::kCommitError}) {
        RODAK_CHECK(rodakos::HomeLayoutSaveLocksEditing(status));
    }
    for (const auto status : {Status::kSaved, Status::kInvalidModel,
                              Status::kTooLarge, Status::kEncodeError}) {
        RODAK_CHECK_FALSE(rodakos::HomeLayoutSaveLocksEditing(status));
    }

    rodakos_test::ResetSettingsState();
    rodakos::HomeLayoutStore store;
    const auto loaded = store.Load({"a", "b"});
    rodakos_test::SettingsState().read_override = SettingsStringReadStatus::kError;
    const auto failed = store.Save(loaded.layout);
    RODAK_CHECK_EQ(failed.status, Status::kCompareError);
    RODAK_CHECK_FALSE(failed.layout.has_value());
    RODAK_CHECK_FALSE(store.write_allowed());
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 0);
}

RODAK_TEST("Home concurrent edit sessions reject the second writer") {
    rodakos_test::ResetSettingsState();
    rodakos::HomeLayoutStore first_store;
    rodakos::HomeLayoutStore second_store;
    const auto first = first_store.Load({"a", "b", "c"});
    const auto second = second_store.Load({"a", "b", "c"});

    const auto first_move = rodakos::MoveHomeManagedRootItem(
        first.layout,
        {rodakos::HomeLayoutItemType::kApp, "a"},
        rodakos::HomeMoveDirection::kNext);
    const auto second_move = rodakos::MoveHomeManagedRootItem(
        second.layout,
        {rodakos::HomeLayoutItemType::kApp, "c"},
        rodakos::HomeMoveDirection::kPrevious);
    rodakos_test::SettingsState().read_delay_ms = 50;

    std::mutex start_mutex;
    std::condition_variable start_condition;
    int ready = 0;
    bool start = false;
    rodakos::HomeLayoutSaveResult first_result;
    rodakos::HomeLayoutSaveResult second_result;
    auto run_save = [&](rodakos::HomeLayoutStore& store,
                        const HomeLayout& candidate,
                        rodakos::HomeLayoutSaveResult& result) {
        {
            std::unique_lock<std::mutex> lock(start_mutex);
            ++ready;
            start_condition.notify_all();
            start_condition.wait(lock, [&]() { return start; });
        }
        result = store.Save(candidate);
    };

    std::thread first_thread(
        run_save, std::ref(first_store), std::cref(*first_move.candidate),
        std::ref(first_result));
    std::thread second_thread(
        run_save, std::ref(second_store), std::cref(*second_move.candidate),
        std::ref(second_result));
    {
        std::unique_lock<std::mutex> lock(start_mutex);
        start_condition.wait(lock, [&]() { return ready == 2; });
        start = true;
    }
    start_condition.notify_all();
    first_thread.join();
    second_thread.join();
    rodakos_test::SettingsState().read_delay_ms = 0;

    const bool first_saved = first_result.status == rodakos::HomeLayoutSaveStatus::kSaved;
    const bool second_saved = second_result.status == rodakos::HomeLayoutSaveStatus::kSaved;
    RODAK_CHECK(first_saved != second_saved);
    const auto& saved = first_saved ? first_result : second_result;
    const auto& conflict = first_saved ? second_result : first_result;
    RODAK_CHECK(saved.layout.has_value());
    RODAK_CHECK_EQ(conflict.status, rodakos::HomeLayoutSaveStatus::kConflict);
    RODAK_CHECK_FALSE(conflict.layout.has_value());
    RODAK_CHECK_FALSE((first_saved ? second_store : first_store).write_allowed());
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 1);

    rodakos::HomeLayoutStore verifier;
    const auto persisted = verifier.Load({"a", "b", "c"});
    RODAK_CHECK_EQ(persisted.layout, *saved.layout);
}

RODAK_TEST("Home store commit failure freezes writes without claiming Flash rollback") {
    rodakos_test::ResetSettingsState();
    HomeLayout original = rodakos::MakeDefaultHomeLayout({"a", "b"});
    rodakos_test::SetCommittedSetting("home", "layout", EncodeOrFail(original));
    const std::string original_json = rodakos_test::GetCommittedSetting("home", "layout");

    rodakos::HomeLayoutStore store;
    const auto loaded = store.Load({"a", "b"});
    HomeLayout edited = loaded.layout;
    std::swap(edited.items[0], edited.items[1]);
    rodakos_test::SettingsState().commit_result = false;

    const auto failed = store.Save(edited);
    RODAK_CHECK_EQ(failed.status, rodakos::HomeLayoutSaveStatus::kCommitError);
    RODAK_CHECK_FALSE(failed.layout.has_value());
    RODAK_CHECK_NE(rodakos_test::GetCommittedSetting("home", "layout"), original_json);
    const auto persisted =
        rodakos::DecodeHomeLayout(rodakos_test::GetCommittedSetting("home", "layout"));
    RODAK_CHECK_EQ(persisted.status, rodakos::HomeLayoutDecodeStatus::kOk);
    RODAK_CHECK_EQ(persisted.layout.revision, uint32_t{1});
    RODAK_CHECK_EQ(RootAppIds(persisted.layout), (std::vector<std::string>{"b", "a"}));
    RODAK_CHECK_EQ(edited.revision, loaded.layout.revision);
    RODAK_CHECK_FALSE(store.write_allowed());
    RODAK_CHECK_EQ(store.Save(edited).status, rodakos::HomeLayoutSaveStatus::kReadOnly);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().destructor_commit_calls, 0);
}

RODAK_TEST("Home store rejects write failure invalid models and oversized documents before commit") {
    rodakos_test::ResetSettingsState();
    rodakos::HomeLayoutStore store;
    const auto loaded = store.Load({"a", "b"});

    HomeLayout invalid = loaded.layout;
    invalid.items.push_back(HomeLayoutItem::App("a"));
    RODAK_CHECK_EQ(store.Save(invalid).status, rodakos::HomeLayoutSaveStatus::kInvalidModel);

    HomeLayout oversized;
    for (size_t index = 0; index < 100; ++index) {
        oversized.items.push_back(HomeLayoutItem::App(
            "application-with-a-long-canonical-id-" + std::to_string(index)));
    }
    RODAK_CHECK_EQ(store.Save(oversized).status, rodakos::HomeLayoutSaveStatus::kTooLarge);

    rodakos_test::SettingsState().write_status = SettingsStringWriteStatus::kError;
    RODAK_CHECK_EQ(store.Save(loaded.layout).status, rodakos::HomeLayoutSaveStatus::kWriteError);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
}

RODAK_TEST("Home store detects stale revisions and a future-schema write conflict") {
    rodakos_test::ResetSettingsState();
    rodakos::HomeLayoutStore store;
    const auto initial = store.Load({"a", "b"});
    const auto saved = store.Save(initial.layout);
    RODAK_CHECK_EQ(saved.status, rodakos::HomeLayoutSaveStatus::kSaved);
    RODAK_CHECK_EQ(store.Save(initial.layout).status,
                   rodakos::HomeLayoutSaveStatus::kStaleRevision);

    rodakos_test::ResetSettingsState();
    rodakos::HomeLayoutStore conflicting_store;
    const auto missing = conflicting_store.Load({"a"});
    rodakos_test::SetCommittedSetting("home", "layout", "{\"v\":2,\"future\":true}");
    RODAK_CHECK_EQ(conflicting_store.Save(missing.layout).status,
                   rodakos::HomeLayoutSaveStatus::kConflict);
    RODAK_CHECK_FALSE(conflicting_store.write_allowed());
    RODAK_CHECK_EQ(rodakos_test::GetCommittedSetting("home", "layout"),
                   std::string("{\"v\":2,\"future\":true}"));
}

RODAK_TEST("Home store reports an uncertain NVS removal and freezes the session") {
    rodakos_test::ResetSettingsState();
    rodakos::HomeLayoutStore store;
    const auto initial = store.Load({"a"});
    rodakos_test::SettingsState().write_status = SettingsStringWriteStatus::kRemoveFailed;

    RODAK_CHECK_EQ(store.Save(initial.layout).status,
                   rodakos::HomeLayoutSaveStatus::kWriteUncertain);
    RODAK_CHECK_FALSE(store.write_allowed());
    const auto persisted =
        rodakos::DecodeHomeLayout(rodakos_test::GetCommittedSetting("home", "layout"));
    RODAK_CHECK_EQ(persisted.status, rodakos::HomeLayoutDecodeStatus::kOk);
    RODAK_CHECK_EQ(persisted.layout.revision, uint32_t{1});
}

RODAK_TEST("Home store allows explicit reset of corrupt v1 data but not future schema") {
    rodakos_test::ResetSettingsState();
    rodakos_test::SetCommittedSetting("home", "layout", "{");
    rodakos::HomeLayoutStore corrupt_store;
    const auto corrupt = corrupt_store.Load({"settings"});
    RODAK_CHECK_EQ(corrupt.status, rodakos::HomeLayoutLoadStatus::kCorrupt);
    const auto reset = corrupt_store.Reset(corrupt.layout);
    RODAK_CHECK_EQ(reset.status, rodakos::HomeLayoutSaveStatus::kSaved);
    RODAK_CHECK(reset.layout.has_value());
    RODAK_CHECK_EQ(reset.layout->revision, uint32_t{1});

    rodakos_test::ResetSettingsState();
    rodakos_test::SetCommittedSetting("home", "layout", "{\"v\":2}");
    rodakos::HomeLayoutStore future_store;
    const auto future = future_store.Load({"settings"});
    RODAK_CHECK_EQ(future.status, rodakos::HomeLayoutLoadStatus::kUnsupportedVersion);
    RODAK_CHECK_EQ(future_store.Reset(future.layout).status,
                   rodakos::HomeLayoutSaveStatus::kResetNotAllowed);

    rodakos_test::ResetSettingsState();
    rodakos_test::SettingsState().read_override = SettingsStringReadStatus::kTypeMismatch;
    rodakos::HomeLayoutStore wrong_type_store;
    const auto wrong_type = wrong_type_store.Load({"settings"});
    RODAK_CHECK_EQ(wrong_type.status, rodakos::HomeLayoutLoadStatus::kCorrupt);
    RODAK_CHECK_EQ(wrong_type_store.Reset(wrong_type.layout).status,
                   rodakos::HomeLayoutSaveStatus::kResetNotAllowed);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 0);
}

RODAK_TEST("Home store advances consecutive structural revisions through overflow reset") {
    rodakos_test::ResetSettingsState();
    HomeLayout initial = rodakos::MakeDefaultHomeLayout({"a", "b", "c"});
    initial.revision = std::numeric_limits<uint32_t>::max() - 2;
    rodakos_test::SetCommittedSetting("home", "layout", EncodeOrFail(initial));

    rodakos::HomeLayoutStore store;
    const auto loaded = store.Load({"a", "b", "c"});
    const rodakos::HomeRootItemKey folder{
        rodakos::HomeLayoutItemType::kFolder, "f_00000001"};
    const auto created = rodakos::CreateHomeFolder(
        loaded.layout, folder.id, "Folder",
        {{rodakos::HomeLayoutItemType::kApp, "a"}});
    const auto first = store.Save(*created.candidate);
    RODAK_CHECK_EQ(first.status, rodakos::HomeLayoutSaveStatus::kSaved);
    RODAK_CHECK(first.layout.has_value());
    RODAK_CHECK_EQ(first.layout->revision,
                   std::numeric_limits<uint32_t>::max() - 1);

    const auto renamed = rodakos::RenameHomeFolder(*first.layout, folder, "Tools");
    const auto second = store.Save(*renamed.candidate);
    RODAK_CHECK_EQ(second.status, rodakos::HomeLayoutSaveStatus::kSaved);
    RODAK_CHECK(second.layout.has_value());
    RODAK_CHECK_EQ(second.layout->revision,
                   std::numeric_limits<uint32_t>::max());
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 2);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 2);

    const auto added = rodakos::MoveHomeRootAppIntoFolder(
        *second.layout,
        {rodakos::HomeLayoutItemType::kApp, "b"}, folder);
    const auto overflow = store.Save(*added.candidate);
    RODAK_CHECK_EQ(overflow.status,
                   rodakos::HomeLayoutSaveStatus::kRevisionOverflow);
    RODAK_CHECK_FALSE(overflow.layout.has_value());
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 2);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 2);

    const auto reset = store.Reset(
        rodakos::MakeDefaultHomeLayout({"a", "b", "c"}));
    RODAK_CHECK_EQ(reset.status, rodakos::HomeLayoutSaveStatus::kSaved);
    RODAK_CHECK(reset.layout.has_value());
    RODAK_CHECK_EQ(reset.layout->revision, uint32_t{0});
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 3);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 3);
}

RODAK_TEST("Home store resets healthy layouts and recovers an exhausted revision") {
    rodakos_test::ResetSettingsState();
    HomeLayout customized = rodakos::MakeDefaultHomeLayout({"b", "a"});
    customized.revision = 7;
    rodakos_test::SetCommittedSetting("home", "layout", EncodeOrFail(customized));
    rodakos::HomeLayoutStore healthy_store;
    const auto healthy = healthy_store.Load({"a", "b"});
    const auto reset = healthy_store.Reset(rodakos::MakeDefaultHomeLayout({"a", "b"}));
    RODAK_CHECK_EQ(reset.status, rodakos::HomeLayoutSaveStatus::kSaved);
    RODAK_CHECK(reset.layout.has_value());
    RODAK_CHECK_EQ(reset.layout->revision, uint32_t{8});
    RODAK_CHECK_EQ(RootAppIds(*reset.layout), (std::vector<std::string>{"a", "b"}));

    rodakos_test::ResetSettingsState();
    HomeLayout exhausted = rodakos::MakeDefaultHomeLayout({"a"});
    exhausted.revision = std::numeric_limits<uint32_t>::max();
    rodakos_test::SetCommittedSetting("home", "layout", EncodeOrFail(exhausted));
    rodakos::HomeLayoutStore exhausted_store;
    const auto loaded = exhausted_store.Load({"a"});
    RODAK_CHECK_EQ(exhausted_store.Save(loaded.layout).status,
                   rodakos::HomeLayoutSaveStatus::kRevisionOverflow);
    const auto recovered = exhausted_store.Reset(rodakos::MakeDefaultHomeLayout({"a"}));
    RODAK_CHECK_EQ(recovered.status, rodakos::HomeLayoutSaveStatus::kSaved);
    RODAK_CHECK(recovered.layout.has_value());
    RODAK_CHECK_EQ(recovered.layout->revision, uint32_t{0});
}

RODAK_TEST("Home store preserves unknown oversized corrupt and unreadable values read-only") {
    struct Scenario {
        std::string value;
        std::optional<SettingsStringReadStatus> read_override;
        rodakos::HomeLayoutLoadStatus expected;
    };
    const std::vector<Scenario> scenarios = {
        {"{\"v\":2,\"future\":true}", std::nullopt,
         rodakos::HomeLayoutLoadStatus::kUnsupportedVersion},
        {std::string(rodakos::kHomeLayoutMaxJsonBytes + 1, 'x'), std::nullopt,
         rodakos::HomeLayoutLoadStatus::kTooLarge},
        {"{", std::nullopt, rodakos::HomeLayoutLoadStatus::kCorrupt},
        {"", SettingsStringReadStatus::kError, rodakos::HomeLayoutLoadStatus::kStorageError},
    };

    for (const auto& scenario : scenarios) {
        rodakos_test::ResetSettingsState();
        rodakos_test::SetCommittedSetting("home", "layout", scenario.value);
        rodakos_test::SettingsState().read_override = scenario.read_override;

        rodakos::HomeLayoutStore store;
        const auto loaded = store.Load({"settings"});
        RODAK_CHECK_EQ(loaded.status, scenario.expected);
        RODAK_CHECK_FALSE(loaded.write_allowed);
        RODAK_CHECK_EQ(store.Save(loaded.layout).status, rodakos::HomeLayoutSaveStatus::kReadOnly);
        RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 0);
        RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 0);
        RODAK_CHECK_EQ(rodakos_test::GetCommittedSetting("home", "layout"), scenario.value);
    }
}

RODAK_TEST("Home launch guard suppresses long press without poisoning the next short press") {
    rodakos::HomeLaunchGuard guard;
    guard.BeginPress();
    RODAK_CHECK(guard.ShouldLaunchShortClick());
    guard.MarkLongPress();
    RODAK_CHECK_FALSE(guard.ShouldLaunchShortClick());
    guard.BeginPress();
    RODAK_CHECK(guard.ShouldLaunchShortClick());
}

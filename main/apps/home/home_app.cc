#include "apps/home/home_app.h"

#include "apps/home/home_layout_model.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/time_service.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/rodakos_layout.h"
#include "phone_ui/rodakos_theme.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr const char* TAG = "HomeApp";

constexpr int kGridCols = 4;
constexpr int kGridRows = 3;
constexpr size_t kAppsPerPage = rodakos::kHomeAppsPerPage;

constexpr lv_coord_t kCellWidth = 64;
constexpr lv_coord_t kCellHeight = 60;
constexpr lv_coord_t kGapX = 12;
constexpr lv_coord_t kGapY = 6;
constexpr lv_coord_t kIconSize = 46;
constexpr int32_t kTileTapSlop = 10;
constexpr int32_t kTileTapSlopSquared = kTileTapSlop * kTileTapSlop;

const std::vector<rodakos::HomeAppIdMigration> kHomeAppIdMigrations;
rodakos::HomePageSession g_home_page_session;

const char* LayoutSaveFailureText(rodakos::HomeLayoutSaveStatus status) {
    switch (status) {
        case rodakos::HomeLayoutSaveStatus::kConflict:
        case rodakos::HomeLayoutSaveStatus::kStaleRevision:
            return "Layout changed. Reopen Home.";
        case rodakos::HomeLayoutSaveStatus::kCompareError:
            return "Cannot verify saved layout. Editing locked.";
        case rodakos::HomeLayoutSaveStatus::kWriteUncertain:
        case rodakos::HomeLayoutSaveStatus::kCommitError:
            return "Save result uncertain. Editing locked.";
        case rodakos::HomeLayoutSaveStatus::kReadOnly:
        case rodakos::HomeLayoutSaveStatus::kRevisionOverflow:
            return "Layout is read-only.";
        case rodakos::HomeLayoutSaveStatus::kTooLarge:
            return "Layout is too large.";
        default:
            return "Layout not saved.";
    }
}

void SetButtonEnabled(lv_obj_t* button, bool enabled) {
    if (button == nullptr) {
        return;
    }
    if (enabled) {
        lv_obj_remove_state(button, LV_STATE_DISABLED);
        lv_obj_set_style_opa(button, LV_OPA_COVER, 0);
    } else {
        lv_obj_add_state(button, LV_STATE_DISABLED);
        lv_obj_set_style_opa(button, static_cast<lv_opa_t>(96), 0);
    }
}

int IconColorIndex(const std::string& app_id) {
    uint32_t hash = 2166136261u;
    for (char ch : app_id) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 16777619u;
    }
    return static_cast<int>(hash % 8u);
}

void ClockTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<HomeApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->UpdateClock();
    }
}

}  // namespace

struct HomeApp::DeferredEditPayload {
    HomeApp* owner = nullptr;
    DeferredEditAction action = DeferredEditAction::kOpen;
    std::optional<LayoutEditTarget> target;
    std::string value;
};

struct HomeApp::DeferredLaunchPayload {
    HomeApp* owner = nullptr;
    PhoneAppContext* context = nullptr;
    std::string app_id;
};

struct HomeApp::TilePayload {
    HomeApp* owner = nullptr;
    TileAction action = TileAction::kLaunchApp;
    std::string id;
    std::optional<LayoutEditTarget> editable_target;
    rodakos::HomeLaunchGuard launch_guard;
    lv_point_t press_origin{};
    bool pointer_tracking = false;
    bool pointer_moved = false;
    bool press_cancelled = false;
    bool long_press_pending = false;
};

void HomeApp::AppButtonEvent(lv_event_t* event) {
    auto* payload = static_cast<TilePayload*>(lv_event_get_user_data(event));
    if (payload == nullptr || payload->owner == nullptr) {
        return;
    }

    auto update_pointer_movement = [&]() {
        lv_indev_t* indev = lv_event_get_indev(event);
        if (!payload->pointer_tracking || indev == nullptr ||
            lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
            return;
        }

        lv_point_t point;
        lv_indev_get_point(indev, &point);
        const int32_t delta_x = point.x - payload->press_origin.x;
        const int32_t delta_y = point.y - payload->press_origin.y;
        const bool moved_from_origin =
            delta_x * delta_x + delta_y * delta_y >= kTileTapSlopSquared;
        if (moved_from_origin || lv_indev_get_press_moved(indev) ||
            lv_indev_get_scroll_obj(indev) != nullptr) {
            payload->pointer_moved = true;
            payload->long_press_pending = false;
        }
    };

    switch (lv_event_get_code(event)) {
        case LV_EVENT_PRESSED: {
            payload->launch_guard.BeginPress();
            payload->pointer_tracking = false;
            payload->pointer_moved = false;
            payload->press_cancelled = false;
            payload->long_press_pending = false;
            lv_indev_t* indev = lv_event_get_indev(event);
            if (indev != nullptr && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
                lv_indev_get_point(indev, &payload->press_origin);
                payload->pointer_tracking = true;
            }
            break;
        }
        case LV_EVENT_PRESSING:
            update_pointer_movement();
            break;
        case LV_EVENT_LONG_PRESSED:
            update_pointer_movement();
            if (!payload->pointer_moved && !payload->press_cancelled) {
                payload->launch_guard.MarkLongPress();
                payload->long_press_pending = payload->editable_target.has_value();
            }
            break;
        case LV_EVENT_RELEASED:
            update_pointer_movement();
            payload->pointer_tracking = false;
            if (!payload->pointer_moved && !payload->press_cancelled &&
                payload->long_press_pending && payload->editable_target.has_value()) {
                payload->long_press_pending = false;
                payload->owner->QueueEditAction(
                    DeferredEditAction::kOpen, payload->editable_target);
            }
            break;
        case LV_EVENT_PRESS_LOST:
            payload->pointer_tracking = false;
            payload->pointer_moved = true;
            payload->press_cancelled = true;
            payload->long_press_pending = false;
            break;
        case LV_EVENT_SHORT_CLICKED:
            if (!payload->pointer_moved && !payload->press_cancelled &&
                payload->launch_guard.ShouldLaunchShortClick()) {
                HomeApp* owner = payload->owner;
                const TileAction action = payload->action;
                const std::string id = payload->id;
                owner->ActivateTile(action, id);
            }
            break;
        default:
            break;
    }
}

void HomeApp::AppButtonDeleteEvent(lv_event_t* event) {
    delete static_cast<TilePayload*>(lv_event_get_user_data(event));
}

void HomeApp::TileviewEvent(lv_event_t* event) {
    auto* self = static_cast<HomeApp*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->UpdatePageIndicator();
        self->QueuePageWindowRefresh();
    }
}

void HomeApp::RunDeferredPageWindow(void* data) {
    auto* self = static_cast<HomeApp*>(data);
    if (self == nullptr || !self->page_window_refresh_pending_) {
        return;
    }

    const size_t active_page = self->pending_page_window_;
    self->page_window_refresh_pending_ = false;
    if (!self->RefreshHomePageWindow(active_page) && self->ui_ != nullptr) {
        self->ui_->ShowToastUnlocked("Home page unavailable.");
    }
}

void HomeApp::RunDeferredEdit(void* data) {
    std::unique_ptr<DeferredEditPayload> payload(static_cast<DeferredEditPayload*>(data));
    if (payload == nullptr || payload->owner == nullptr) {
        return;
    }

    HomeApp* owner = payload->owner;
    if (owner->pending_edit_ != payload.get()) {
        return;
    }
    owner->pending_edit_ = nullptr;
    owner->edit_action_pending_ = false;

    switch (payload->action) {
        case DeferredEditAction::kOpen:
            if (payload->target.has_value()) {
                owner->OpenLayoutEditor(*payload->target);
            }
            break;
        case DeferredEditAction::kCloseCollection:
            owner->CloseCollection();
            break;
        case DeferredEditAction::kCancel:
            owner->CloseLayoutEditor();
            break;
        case DeferredEditAction::kSave:
            owner->SaveLayoutDraft();
            break;
        case DeferredEditAction::kOpenDestination:
            owner->editor_page_ = HomeEditorPage::kDestination;
            owner->CreateLayoutEditorUi();
            break;
        case DeferredEditAction::kCloseEditorSubview:
            owner->editor_page_ = HomeEditorPage::kOverview;
            owner->CreateLayoutEditorUi();
            break;
        case DeferredEditAction::kOpenCreateName:
            owner->OpenFolderNameDialog(FolderNameMode::kCreate);
            break;
        case DeferredEditAction::kOpenRenameName:
            owner->OpenFolderNameDialog(FolderNameMode::kRename);
            break;
        case DeferredEditAction::kCloseFolderName:
            owner->CloseFolderNameDialog();
            break;
        case DeferredEditAction::kApplyFolderName:
            owner->ApplyFolderName(std::move(payload->value));
            break;
        case DeferredEditAction::kMoveToFolder:
            owner->MoveLayoutDraftToFolder(payload->value);
            break;
        case DeferredEditAction::kMoveOut:
            owner->MoveLayoutDraftOut();
            break;
        case DeferredEditAction::kOpenDissolveConfirm:
            owner->editor_page_ = HomeEditorPage::kDissolveConfirm;
            owner->CreateLayoutEditorUi();
            break;
        case DeferredEditAction::kDissolve:
            owner->DissolveLayoutDraftFolder();
            break;
    }
}

void HomeApp::RunDeferredLaunch(void* data) {
    std::unique_ptr<DeferredLaunchPayload> payload(static_cast<DeferredLaunchPayload*>(data));
    if (payload == nullptr || payload->owner == nullptr || payload->context == nullptr) {
        return;
    }

    HomeApp* owner = payload->owner;
    if (owner->pending_launch_ != payload.get()) {
        ESP_LOGW(TAG, "Ignoring stale deferred app launch: %s", payload->app_id.c_str());
        return;
    }

    owner->pending_launch_ = nullptr;
    owner->launch_pending_ = false;

    ESP_LOGI(TAG, "Launching app from Home: %s", payload->app_id.c_str());
    if (!payload->context->navigation().Launch(payload->app_id)) {
        ESP_LOGE(TAG, "Failed to launch app from Home: %s", payload->app_id.c_str());
    }
}

bool HomeApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();

    ESP_LOGI(TAG, "HomeApp::OnCreate starting");
    if (!LoadLayout(context)) {
        ESP_LOGE(TAG, "Failed to resolve Home layout");
        return false;
    }

    // Use longer timeout during WiFi initialization
    PhoneUiLock lock(*ui_, 5000);  // 5 seconds timeout
    if (!lock.locked()) {
        ESP_LOGE(TAG, "Failed to acquire UI lock");
        return false;
    }

    return CreateUi(context);
}

bool HomeApp::CreateUi(PhoneAppContext& context) {
    // 从设置中加载并应用主题
    Settings display_settings("display", false);
    const std::string theme_name = display_settings.GetString("theme", "dark");

    rodakos_theme_init_from_name(theme_name.c_str());
    ESP_LOGI(TAG, "Theme initialized: %s", theme_name.c_str());

    TimeServiceApplySavedTimeZone();

    // 初始化布局系统（使用默认 320x240 配置）
    rodakos_layout_init(nullptr);
    ESP_LOGI(TAG, "Layout system initialized");

    // 创建布局容器（自动分区）
    lv_obj_t* header = nullptr;
    lv_obj_t* body = nullptr;
    lv_obj_t* footer = nullptr;
    root_ = rodakos_layout_create(ui_->screen(), &header, &body, &footer);

    if (root_ == nullptr || header == nullptr || body == nullptr || footer == nullptr) {
        ESP_LOGE(TAG, "Failed to create layout containers");
        return false;
    }
    body_ = body;
    footer_ = footer;
    page_tiles_.clear();
    ESP_LOGI(TAG, "Layout containers created");

    // ===== HEADER 区域：手机状态栏 =====
    lv_obj_set_style_pad_left(header, 14, 0);
    lv_obj_set_style_pad_right(header, 12, 0);

    clock_label_ = lv_label_create(header);
    lv_label_set_text(clock_label_, "00:00");
    lv_obj_set_style_text_color(clock_label_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(clock_label_, &phone_font_14, 0);
    lv_obj_align(clock_label_, LV_ALIGN_LEFT_MID, 0, 0);

    status_cluster_ = lv_obj_create(header);
    lv_obj_remove_style_all(status_cluster_);
    lv_obj_set_size(status_cluster_, 40, 22);
    lv_obj_align(status_cluster_, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_layout(status_cluster_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(status_cluster_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_cluster_, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status_cluster_, 6, 0);
    lv_obj_clear_flag(status_cluster_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(status_cluster_, LV_OBJ_FLAG_SCROLLABLE);

    wifi_label_ = lv_label_create(status_cluster_);
    lv_label_set_text(wifi_label_, FONT_AWESOME_WIFI_SLASH);
    lv_obj_set_style_text_color(wifi_label_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(wifi_label_, PhoneIconFont(), 0);

    // ===== BODY 区域 =====
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);

    tileview_ = lv_tileview_create(body);
    if (tileview_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create Home tileview");
        return false;
    }
    lv_obj_remove_style_all(tileview_);
    lv_obj_set_size(tileview_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(tileview_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(tileview_, 0, 0);
    lv_obj_set_scrollbar_mode(tileview_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(tileview_, TileviewEvent, LV_EVENT_SCROLL_END, this);

    const auto apps = context.registry().ListHomeApps();
    const size_t root_item_count = projection_.root_slot_count();
    const size_t page_count = rodakos::HomeLayoutPageCount(root_item_count);
    const size_t initial_page = g_home_page_session.Restore(projection_);
    lv_obj_t* initial_tile = nullptr;
    page_populated_.assign(page_count, false);

    ESP_LOGI(TAG, "Rendering %zu visible apps as %zu root item(s) across %zu page(s)",
             apps.size(), root_item_count, page_count);
    for (size_t page_index = 0; page_index < page_count; ++page_index) {
        lv_dir_t direction = LV_DIR_NONE;
        if (page_index > 0) {
            direction = static_cast<lv_dir_t>(direction | LV_DIR_LEFT);
        }
        if (page_index + 1 < page_count) {
            direction = static_cast<lv_dir_t>(direction | LV_DIR_RIGHT);
        }

        auto* tile = lv_tileview_add_tile(tileview_, static_cast<uint8_t>(page_index), 0, direction);
        if (tile == nullptr) {
            ESP_LOGE(TAG, "Failed to create Home tile %u", static_cast<unsigned>(page_index));
            return false;
        }
        lv_obj_set_size(tile, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        page_tiles_.push_back(tile);
        if (page_index == initial_page) {
            initial_tile = tile;
        }
    }

    if (initial_tile == nullptr && !page_tiles_.empty()) {
        initial_tile = page_tiles_.front();
    }
    lv_tileview_set_tile(tileview_, initial_tile, LV_ANIM_OFF);
    if (!RefreshHomePageWindow(initial_page)) {
        ESP_LOGE(TAG, "Failed to render the initial Home page window");
        return false;
    }

    // ===== FOOTER 区域 =====
    page_indicator_ = rodakos_layout_create_flex(footer, LV_FLEX_FLOW_ROW,
                                                  rodakos_layout_padding_medium());
    lv_obj_center(page_indicator_);

    for (size_t page_index = 0; page_index < page_count; ++page_index) {
        auto* dot = lv_obj_create(page_indicator_);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, rodakos_theme_text_tertiary(), 0);
        lv_obj_set_style_bg_opa(dot, static_cast<lv_opa_t>(128), 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    }
    UpdatePageIndicator();

    UpdateClock();
    clock_timer_ = lv_timer_create(ClockTimerCallback, 1000, this);

    if (collection_state_.kind == CollectionKind::kFolder) {
        OpenFolder(collection_state_.folder_id);
    } else if (collection_state_.kind == CollectionKind::kAllApps) {
        OpenAllApps();
    }

    if (HasEditingTarget() &&
        rodakos::ValidateHomeLayout(draft_layout_) ==
            rodakos::HomeLayoutValidationStatus::kOk) {
        CreateLayoutEditorUi();
    }

    const size_t ready_free_internal = heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t ready_largest_internal = heap_caps_get_largest_free_block(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "Home UI ready: SRAM free=%u largest=%u",
             static_cast<unsigned>(ready_free_internal),
             static_cast<unsigned>(ready_largest_internal));
    ESP_LOGI(TAG, "Phone desktop ready with %u apps on %u page(s)",
             static_cast<unsigned>(apps.size()), static_cast<unsigned>(page_count));
    return true;
}

bool HomeApp::PopulateHomePage(size_t page_index) {
    if (context_ == nullptr || page_index >= page_tiles_.size() ||
        page_index >= page_populated_.size()) {
        return false;
    }
    if (page_populated_[page_index]) {
        return true;
    }

    lv_obj_t* tile = page_tiles_[page_index];
    if (tile == nullptr || !lv_obj_is_valid(tile)) {
        return false;
    }
    auto* grid = rodakos_layout_create_grid(tile, kGridCols, kGridRows,
                                             kCellWidth, kCellHeight,
                                             kGapX, kGapY);
    if (grid == nullptr) {
        ESP_LOGE(TAG, "Failed to create Home grid on page %u",
                 static_cast<unsigned>(page_index));
        return false;
    }

    const size_t root_item_count = projection_.root_slot_count();
    const size_t first_item = page_index * kAppsPerPage;
    const size_t last_item = std::min(first_item + kAppsPerPage, root_item_count);
    for (size_t item_index = first_item; item_index < last_item; ++item_index) {
        const bool all_apps = projection_.has_all_apps() &&
                              item_index == projection_.managed_items.size();
        const rodakos::HomeLayoutItem* item =
            all_apps ? nullptr : &projection_.managed_items[item_index];
        const size_t page_item_index = item_index - first_item;
        const PhoneAppDescriptor* app = nullptr;
        if (item != nullptr && item->type == rodakos::HomeLayoutItemType::kApp) {
            app = context_->registry().FindById(item->id);
            if (app == nullptr) {
                ESP_LOGE(TAG, "Resolved Home app is no longer registered: %s",
                         item->id.c_str());
                continue;
            }
        }

        auto* btn = lv_btn_create(grid);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, kCellWidth, kCellHeight);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        rodakos_layout_grid_place(grid, btn, static_cast<int>(page_item_index), kGridCols,
                                  kCellWidth, kCellHeight, kGapX, kGapY);

        if (all_apps) {
            BindTileAction(btn, TileAction::kOpenAllApps);
        } else if (app != nullptr) {
            BindTileAction(
                btn, TileAction::kLaunchApp, app->id,
                LayoutEditTarget{
                    .kind = LayoutEditTargetKind::kRootItem,
                    .root_item = {item->type, item->id},
                    .folder_id = {},
                    .app_id = {},
                });
        } else {
            BindTileAction(
                btn, TileAction::kOpenFolder, item->id,
                LayoutEditTarget{
                    .kind = LayoutEditTargetKind::kRootItem,
                    .root_item = {item->type, item->id},
                    .folder_id = {},
                    .app_id = {},
                });
        }

        auto* icon_bg = lv_obj_create(btn);
        lv_obj_remove_style_all(icon_bg);
        lv_obj_set_size(icon_bg, kIconSize, kIconSize);
        lv_obj_align(icon_bg, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_radius(
            icon_bg, app != nullptr ? LV_RADIUS_CIRCLE : static_cast<lv_coord_t>(8), 0);
        lv_obj_set_style_bg_color(
            icon_bg,
            app != nullptr ? rodakos_theme_icon_color(IconColorIndex(app->id))
            : all_apps    ? rodakos_theme_secondary()
                           : rodakos_theme_bg_tertiary(),
            0);
        lv_obj_set_style_bg_opa(icon_bg, LV_OPA_COVER, 0);
        lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_CLICKABLE);

        if (app != nullptr) {
            auto* icon_label = lv_label_create(icon_bg);
            lv_label_set_text(icon_label, app->icon.c_str());
            lv_obj_set_style_text_color(icon_label, lv_color_white(), 0);
            lv_obj_set_style_text_font(icon_label, PhoneIconFontLarge(), 0);
            lv_obj_center(icon_label);
        } else if (all_apps) {
            auto* icon_label = lv_label_create(icon_bg);
            lv_label_set_text(icon_label, FONT_AWESOME_ARROW_RIGHT);
            lv_obj_set_style_text_color(icon_label, lv_color_white(), 0);
            lv_obj_set_style_text_font(icon_label, PhoneIconFont(), 0);
            lv_obj_center(icon_label);

            auto* count_label = lv_label_create(icon_bg);
            const std::string count = std::to_string(projection_.overflow_items.size());
            lv_label_set_text(count_label, count.c_str());
            lv_obj_set_style_text_color(count_label, lv_color_white(), 0);
            lv_obj_set_style_text_font(count_label, &phone_font_12, 0);
            lv_obj_align(count_label, LV_ALIGN_TOP_RIGHT, -3, 2);
        } else {
            const size_t preview_count = std::min<size_t>(item->apps.size(), 4);
            for (size_t preview_index = 0; preview_index < preview_count; ++preview_index) {
                auto* preview = lv_obj_create(icon_bg);
                lv_obj_remove_style_all(preview);
                lv_obj_set_size(preview, 16, 16);
                const lv_coord_t x = preview_index % 2 == 0 ? -9 : 9;
                const lv_coord_t y = preview_index < 2 ? -9 : 9;
                lv_obj_align(preview, LV_ALIGN_CENTER, x, y);
                lv_obj_set_style_radius(preview, 5, 0);
                lv_obj_set_style_bg_color(
                    preview,
                    rodakos_theme_icon_color(IconColorIndex(item->apps[preview_index])), 0);
                lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, 0);
                lv_obj_clear_flag(preview, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_clear_flag(preview, LV_OBJ_FLAG_SCROLLABLE);
            }
        }

        auto* name_label = lv_label_create(btn);
        lv_label_set_text(name_label,
                          app != nullptr ? app->title.c_str()
                          : all_apps    ? "All Apps"
                                        : item->name.c_str());
        lv_obj_set_width(name_label, kCellWidth);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(name_label, rodakos_theme_text_secondary(), 0);
        lv_obj_set_style_text_font(name_label, &phone_font_12, 0);
        lv_obj_align(name_label, LV_ALIGN_BOTTOM_MID, 0, 0);
    }

    page_populated_[page_index] = true;
    ESP_LOGI(TAG, "Rendered Home page %u with %u slot(s)",
             static_cast<unsigned>(page_index + 1),
             static_cast<unsigned>(last_item - first_item));
    return true;
}

size_t HomeApp::ActivePageIndex() const {
    if (tileview_ == nullptr || page_tiles_.empty()) {
        return 0;
    }
    lv_obj_t* active_tile = lv_tileview_get_tile_active(tileview_);
    const auto active = std::find(page_tiles_.begin(), page_tiles_.end(), active_tile);
    return active == page_tiles_.end()
               ? size_t{0}
               : static_cast<size_t>(active - page_tiles_.begin());
}

void HomeApp::UpdateActivePageDirections(size_t active_page) {
    if (tileview_ == nullptr || active_page >= page_populated_.size()) {
        return;
    }
    lv_dir_t direction = LV_DIR_NONE;
    if (active_page > 0 && page_populated_[active_page - 1]) {
        direction = static_cast<lv_dir_t>(direction | LV_DIR_LEFT);
    }
    if (active_page + 1 < page_populated_.size() &&
        page_populated_[active_page + 1]) {
        direction = static_cast<lv_dir_t>(direction | LV_DIR_RIGHT);
    }
    lv_obj_set_scroll_dir(tileview_, direction);
}

bool HomeApp::RefreshHomePageWindow(size_t active_page) {
    if (page_tiles_.empty() || page_populated_.size() != page_tiles_.size()) {
        return false;
    }
    active_page = std::min(active_page, page_tiles_.size() - 1);
    const auto plan = rodakos::ResolveHomePageRenderPlan(
        page_tiles_.size(), active_page);
    const auto& window = plan.window;

    for (size_t page = 0; page < page_tiles_.size(); ++page) {
        if (page_populated_[page] && !window.Contains(page)) {
            lv_obj_clean(page_tiles_[page]);
            page_populated_[page] = false;
        }
    }

    const auto populate = [&](size_t page) {
        if (!page_populated_[page] && !PopulateHomePage(page)) {
            ESP_LOGE(TAG, "Failed to populate Home page %u",
                     static_cast<unsigned>(page + 1));
        }
    };
    for (size_t index = 0; index < plan.populate_count; ++index) {
        populate(plan.populate_order[index]);
    }

    UpdateActivePageDirections(active_page);
    const size_t resident_pages = static_cast<size_t>(
        std::count(page_populated_.begin(), page_populated_.end(), true));
    const size_t free_internal = heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largest_internal = heap_caps_get_largest_free_block(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG,
             "Home page window %u-%u: %u/%u resident, SRAM free=%u largest=%u",
             static_cast<unsigned>(window.first_page + 1),
             static_cast<unsigned>(window.first_page + window.page_count),
             static_cast<unsigned>(resident_pages),
             static_cast<unsigned>(page_tiles_.size()),
             static_cast<unsigned>(free_internal),
             static_cast<unsigned>(largest_internal));
    return page_populated_[active_page];
}

void HomeApp::QueuePageWindowRefresh() {
    pending_page_window_ = ActivePageIndex();
    UpdateActivePageDirections(pending_page_window_);
    if (page_window_refresh_pending_) {
        return;
    }
    page_window_refresh_pending_ = true;
    if (lv_async_call(RunDeferredPageWindow, this) != LV_RESULT_OK) {
        page_window_refresh_pending_ = false;
        UpdateActivePageDirections(pending_page_window_);
        ESP_LOGE(TAG, "Failed to queue Home page window refresh");
    }
}

void HomeApp::CancelPendingPageWindowRefresh() {
    if (!page_window_refresh_pending_) {
        return;
    }
    lv_async_call_cancel(RunDeferredPageWindow, this);
    page_window_refresh_pending_ = false;
}

void HomeApp::OnDestroy() {
    if (ui_ != nullptr) {
        // App destruction cannot leave LVGL callbacks pointing at a released HomeApp.
        PhoneUiLock lock(*ui_, 0);
        if (lock.locked()) {
            DestroyUi();
        }
    }
    editing_target_.reset();
    editor_focus_item_.reset();
    editor_error_status_.reset();
    editor_action_error_.clear();
    collection_state_ = {};
    editor_page_ = HomeEditorPage::kOverview;
    folder_name_mode_ = FolderNameMode::kNone;
    draft_layout_ = {};
    editor_locked_ = false;
    context_ = nullptr;
    ui_ = nullptr;
}

bool HomeApp::OnThemeChanged(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    DestroyUi();
    return CreateUi(context);
}

bool HomeApp::OnHomeRequested() {
    if (ui_ == nullptr) {
        return false;
    }
    PhoneUiLock lock(*ui_, 0);
    if (!lock.locked()) {
        return false;
    }
    CancelPendingEditAction();
    CancelPendingLaunch();
    CloseFolderNameDialog();
    if (HasEditingTarget() || layout_editor_ != nullptr) {
        CloseLayoutEditor();
    }
    CloseCollection();
    return true;
}

bool HomeApp::LoadLayout(PhoneAppContext& context) {
    const auto apps = context.registry().ListHomeApps();
    std::vector<std::string> visible_app_ids;
    visible_app_ids.reserve(apps.size());
    for (const auto* app : apps) {
        if (app != nullptr) {
            visible_app_ids.push_back(app->id);
        }
    }

    rodakos::HomeLayoutLoadResult loaded =
        layout_store_.Load(visible_app_ids, kHomeAppIdMigrations);
    layout_ = std::move(loaded.layout);
    layout_write_allowed_ = loaded.write_allowed;
    if (rodakos::ValidateHomeLayout(layout_) != rodakos::HomeLayoutValidationStatus::kOk) {
        return false;
    }
    projection_ = rodakos::ProjectHomeLayout(layout_);
    ESP_LOGI(TAG, "Resolved Home layout with %u root item(s), writes %s",
             static_cast<unsigned>(layout_.items.size()),
             layout_write_allowed_ ? "enabled" : "disabled");
    return true;
}

void HomeApp::BindTileAction(
    lv_obj_t* object,
    TileAction action,
    std::string id,
    std::optional<LayoutEditTarget> editable_target) {
    auto* payload = new TilePayload;
    payload->owner = this;
    payload->action = action;
    payload->id = std::move(id);
    payload->editable_target = std::move(editable_target);
    lv_obj_add_event_cb(object, AppButtonEvent, LV_EVENT_PRESSED, payload);
    lv_obj_add_event_cb(object, AppButtonEvent, LV_EVENT_PRESSING, payload);
    lv_obj_add_event_cb(object, AppButtonEvent, LV_EVENT_LONG_PRESSED, payload);
    lv_obj_add_event_cb(object, AppButtonEvent, LV_EVENT_SHORT_CLICKED, payload);
    lv_obj_add_event_cb(object, AppButtonEvent, LV_EVENT_RELEASED, payload);
    lv_obj_add_event_cb(object, AppButtonEvent, LV_EVENT_PRESS_LOST, payload);
    lv_obj_add_event_cb(object, AppButtonDeleteEvent, LV_EVENT_DELETE, payload);
}

void HomeApp::ActivateTile(TileAction action, const std::string& id) {
    switch (action) {
        case TileAction::kLaunchApp:
            QueueLaunch(id);
            break;
        case TileAction::kOpenFolder:
            OpenFolder(id);
            break;
        case TileAction::kOpenAllApps:
            OpenAllApps();
            break;
        case TileAction::kCloseCollection:
            QueueEditAction(DeferredEditAction::kCloseCollection);
            break;
        case TileAction::kMovePrevious:
            MoveLayoutDraft(rodakos::HomeMoveDirection::kPrevious);
            break;
        case TileAction::kMoveNext:
            MoveLayoutDraft(rodakos::HomeMoveDirection::kNext);
            break;
        case TileAction::kOpenDestination:
            QueueEditAction(DeferredEditAction::kOpenDestination);
            break;
        case TileAction::kSelectDestination:
            QueueEditAction(DeferredEditAction::kMoveToFolder, std::nullopt, id);
            break;
        case TileAction::kCreateFolder:
            QueueEditAction(DeferredEditAction::kOpenCreateName);
            break;
        case TileAction::kRenameFolder:
            QueueEditAction(DeferredEditAction::kOpenRenameName);
            break;
        case TileAction::kMoveOut:
            QueueEditAction(DeferredEditAction::kMoveOut);
            break;
        case TileAction::kOpenDissolveConfirm:
            QueueEditAction(DeferredEditAction::kOpenDissolveConfirm);
            break;
        case TileAction::kConfirmDissolve:
            QueueEditAction(DeferredEditAction::kDissolve);
            break;
        case TileAction::kCloseEditorSubview:
            QueueEditAction(DeferredEditAction::kCloseEditorSubview);
            break;
        case TileAction::kApplyFolderName:
            QueueEditAction(
                DeferredEditAction::kApplyFolderName, std::nullopt,
                folder_name_textarea_ != nullptr
                    ? std::string(lv_textarea_get_text(folder_name_textarea_))
                    : std::string());
            break;
        case TileAction::kCancelFolderName:
            QueueEditAction(DeferredEditAction::kCloseFolderName);
            break;
        case TileAction::kCancelEditing:
            QueueEditAction(DeferredEditAction::kCancel);
            break;
        case TileAction::kSaveEditing:
            QueueEditAction(DeferredEditAction::kSave);
            break;
    }
}

void HomeApp::OpenFolder(const std::string& folder_id) {
    const auto folder = std::find_if(layout_.items.begin(), layout_.items.end(),
                                     [&](const rodakos::HomeLayoutItem& item) {
        return item.type == rodakos::HomeLayoutItemType::kFolder && item.id == folder_id;
    });
    if (folder == layout_.items.end()) {
        ESP_LOGW(TAG, "Cannot open missing Home folder: %s", folder_id.c_str());
        collection_state_ = {};
        return;
    }

    std::vector<rodakos::HomeLayoutItem> items;
    items.reserve(folder->apps.size());
    for (const auto& app_id : folder->apps) {
        items.push_back(rodakos::HomeLayoutItem::App(app_id));
    }
    OpenCollection(folder->name, items,
                   CollectionState{CollectionKind::kFolder, folder->id});
}

void HomeApp::OpenAllApps() {
    if (projection_.has_all_apps()) {
        OpenCollection("All Apps", projection_.overflow_items,
                       CollectionState{CollectionKind::kAllApps, {}});
    } else {
        collection_state_ = {};
    }
}

void HomeApp::OpenCollection(const std::string& title,
                             const std::vector<rodakos::HomeLayoutItem>& items,
                             CollectionState state) {
    if (body_ == nullptr || tileview_ == nullptr) {
        return;
    }
    if (collection_view_ != nullptr && lv_obj_is_valid(collection_view_)) {
        lv_obj_delete(collection_view_);
    }
    collection_state_ = std::move(state);

    collection_view_ = lv_obj_create(body_);
    lv_obj_remove_style_all(collection_view_);
    lv_obj_set_size(collection_view_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(collection_view_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(collection_view_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(collection_view_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(collection_view_, LV_OBJ_FLAG_SCROLLABLE);

    auto* toolbar = lv_obj_create(collection_view_);
    lv_obj_remove_style_all(toolbar);
    lv_obj_set_size(toolbar, LV_PCT(100), 40);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);

    auto* back = lv_btn_create(toolbar);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, 40, 40);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(back, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    BindTileAction(back, TileAction::kCloseCollection);

    auto* back_icon = lv_label_create(back);
    lv_label_set_text(back_icon, FONT_AWESOME_ARROW_LEFT);
    lv_obj_set_style_text_color(back_icon, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(back_icon, PhoneIconFont(), 0);
    lv_obj_center(back_icon);

    auto* title_label = lv_label_create(toolbar);
    lv_label_set_text(title_label, title.c_str());
    lv_obj_set_width(title_label, 210);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(title_label, &phone_font_14, 0);
    lv_obj_center(title_label);

    auto* count_label = lv_label_create(toolbar);
    const std::string count = std::to_string(items.size());
    lv_label_set_text(count_label, count.c_str());
    lv_obj_set_style_text_color(count_label, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(count_label, &phone_font_12, 0);
    lv_obj_align(count_label, LV_ALIGN_RIGHT_MID, -10, 0);

    auto* list = lv_obj_create(collection_view_);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), 152);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(list, 4, 0);
    lv_obj_set_style_pad_bottom(list, 4, 0);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    for (const auto& item : items) {
        AddCollectionRow(list, item);
    }

    lv_obj_add_flag(tileview_, LV_OBJ_FLAG_HIDDEN);
    if (page_indicator_ != nullptr) {
        lv_obj_add_flag(page_indicator_, LV_OBJ_FLAG_HIDDEN);
    }
}

void HomeApp::AddCollectionRow(lv_obj_t* parent, const rodakos::HomeLayoutItem& item) {
    const PhoneAppDescriptor* app = nullptr;
    if (item.type == rodakos::HomeLayoutItemType::kApp && context_ != nullptr) {
        const auto apps = context_->registry().ListHomeApps();
        const auto match = std::find_if(apps.begin(), apps.end(), [&](const auto* descriptor) {
            return descriptor != nullptr && descriptor->id == item.id;
        });
        if (match == apps.end()) {
            return;
        }
        app = *match;
    }

    auto* row = lv_btn_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 300, 44);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_bg_color(row, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    std::optional<LayoutEditTarget> editable_target;
    if (app != nullptr && collection_state_.kind == CollectionKind::kFolder) {
        editable_target = LayoutEditTarget{
            .kind = LayoutEditTargetKind::kFolderApp,
            .root_item = {},
            .folder_id = collection_state_.folder_id,
            .app_id = app->id,
        };
    }
    BindTileAction(
        row, app != nullptr ? TileAction::kLaunchApp : TileAction::kOpenFolder,
        app != nullptr ? app->id : item.id, std::move(editable_target));

    auto* icon_bg = lv_obj_create(row);
    lv_obj_remove_style_all(icon_bg);
    lv_obj_set_size(icon_bg, 28, 28);
    lv_obj_align(icon_bg, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_radius(icon_bg, app != nullptr ? LV_RADIUS_CIRCLE : 6, 0);
    lv_obj_set_style_bg_color(
        icon_bg,
        app != nullptr ? rodakos_theme_icon_color(IconColorIndex(app->id))
                       : rodakos_theme_bg_tertiary(),
        0);
    lv_obj_set_style_bg_opa(icon_bg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_SCROLLABLE);

    if (app != nullptr) {
        auto* icon = lv_label_create(icon_bg);
        lv_label_set_text(icon, app->icon.c_str());
        lv_obj_set_style_text_color(icon, lv_color_white(), 0);
        lv_obj_set_style_text_font(icon, PhoneIconFont(), 0);
        lv_obj_center(icon);
    }

    auto* label = lv_label_create(row);
    lv_label_set_text(label, app != nullptr ? app->title.c_str() : item.name.c_str());
    lv_obj_set_width(label, 220);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(label, &phone_font_14, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 44, 0);

    auto* arrow = lv_label_create(row);
    lv_label_set_text(arrow, FONT_AWESOME_ANGLE_RIGHT);
    lv_obj_set_style_text_color(arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(arrow, PhoneIconFont(), 0);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -8, 0);
}

void HomeApp::CloseCollection() {
    if (collection_view_ != nullptr && lv_obj_is_valid(collection_view_)) {
        lv_obj_delete(collection_view_);
    }
    collection_view_ = nullptr;
    collection_state_ = {};
    if (tileview_ != nullptr) {
        lv_obj_remove_flag(tileview_, LV_OBJ_FLAG_HIDDEN);
    }
    if (page_indicator_ != nullptr) {
        lv_obj_remove_flag(page_indicator_, LV_OBJ_FLAG_HIDDEN);
    }
}

void HomeApp::OpenLayoutEditor(const LayoutEditTarget& target) {
    if (!layout_write_allowed_ || !layout_store_.write_allowed()) {
        if (ui_ != nullptr) {
            ui_->ShowToastUnlocked("Home layout is read-only.");
        }
        return;
    }

    std::optional<rodakos::HomeRootItemKey> focus;
    if (target.kind == LayoutEditTargetKind::kRootItem) {
        if (!rodakos::FindHomeManagedRootItem(projection_, target.root_item).has_value()) {
            ESP_LOGW(TAG, "Cannot edit unmanaged Home item: %s",
                     target.root_item.id.c_str());
            return;
        }
        focus = target.root_item;
    } else {
        const rodakos::HomeRootItemKey folder{
            rodakos::HomeLayoutItemType::kFolder, target.folder_id};
        const auto folder_index = rodakos::FindHomeManagedRootItem(projection_, folder);
        if (!folder_index.has_value()) {
            ESP_LOGW(TAG, "Cannot edit a member of unmanaged Home folder: %s",
                     target.folder_id.c_str());
            return;
        }
        const auto& folder_item = projection_.managed_items[*folder_index];
        if (std::find(folder_item.apps.begin(), folder_item.apps.end(), target.app_id) ==
            folder_item.apps.end()) {
            ESP_LOGW(TAG, "Cannot edit missing Home folder member: %s",
                     target.app_id.c_str());
            return;
        }
        focus = folder;
    }

    editing_target_ = target;
    editor_focus_item_ = std::move(focus);
    draft_layout_ = layout_;
    editor_page_ = HomeEditorPage::kOverview;
    editor_error_status_.reset();
    editor_action_error_.clear();
    editor_locked_ = false;
    CreateLayoutEditorUi();
}

void HomeApp::CreateLayoutEditorUi() {
    if (body_ == nullptr || tileview_ == nullptr || !HasEditingTarget()) {
        return;
    }
    bool target_exists = false;
    if (editing_target_->kind == LayoutEditTargetKind::kRootItem) {
        target_exists = rodakos::FindHomeRootItem(
                            draft_layout_, editing_target_->root_item).has_value();
    } else {
        const rodakos::HomeRootItemKey folder{
            rodakos::HomeLayoutItemType::kFolder, editing_target_->folder_id};
        const auto folder_index = rodakos::FindHomeRootItem(draft_layout_, folder);
        if (folder_index.has_value()) {
            const auto& members = draft_layout_.items[*folder_index].apps;
            target_exists = std::find(
                                members.begin(), members.end(),
                                editing_target_->app_id) != members.end();
        }
    }
    if (!target_exists) {
        ESP_LOGE(TAG, "Home edit target disappeared");
        if (ui_ != nullptr) {
            ui_->ShowToastUnlocked("Home item is no longer available.");
        }
        CloseLayoutEditor();
        return;
    }
    if (layout_editor_ != nullptr && lv_obj_is_valid(layout_editor_)) {
        lv_obj_delete(layout_editor_);
    }
    ResetLayoutEditorPointers();

    layout_editor_ = lv_obj_create(body_);
    lv_obj_remove_style_all(layout_editor_);
    lv_obj_set_size(layout_editor_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(layout_editor_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(layout_editor_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(layout_editor_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(layout_editor_, LV_OBJ_FLAG_SCROLLABLE);

    switch (editor_page_) {
        case HomeEditorPage::kOverview:
            CreateLayoutEditorOverviewUi();
            break;
        case HomeEditorPage::kDestination:
            CreateLayoutDestinationUi();
            break;
        case HomeEditorPage::kDissolveConfirm:
            CreateLayoutDissolveUi();
            break;
    }

    lv_obj_add_flag(tileview_, LV_OBJ_FLAG_HIDDEN);
    if (collection_view_ != nullptr) {
        lv_obj_add_flag(collection_view_, LV_OBJ_FLAG_HIDDEN);
    }
    if (page_indicator_ != nullptr) {
        lv_obj_add_flag(page_indicator_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_move_foreground(layout_editor_);
    UpdateLayoutEditor();
}

void HomeApp::CreateLayoutEditorOverviewUi() {
    if (!HasEditingTarget() || layout_editor_ == nullptr) {
        return;
    }

    const rodakos::HomeLayoutItem* item = nullptr;
    rodakos::HomeLayoutItem member_item;
    if (editing_target_->kind == LayoutEditTargetKind::kRootItem) {
        const auto index = rodakos::FindHomeRootItem(
            draft_layout_, editing_target_->root_item);
        if (index.has_value()) {
            item = &draft_layout_.items[*index];
        }
    } else {
        const rodakos::HomeRootItemKey folder{
            rodakos::HomeLayoutItemType::kFolder, editing_target_->folder_id};
        const auto folder_index = rodakos::FindHomeRootItem(draft_layout_, folder);
        if (folder_index.has_value()) {
            const auto& members = draft_layout_.items[*folder_index].apps;
            if (std::find(members.begin(), members.end(), editing_target_->app_id) !=
                members.end()) {
                member_item = rodakos::HomeLayoutItem::App(editing_target_->app_id);
                item = &member_item;
            }
        }
    }
    if (item == nullptr) {
        return;
    }

    auto* toolbar = lv_obj_create(layout_editor_);
    lv_obj_remove_style_all(toolbar);
    lv_obj_set_size(toolbar, LV_PCT(100), 40);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);

    auto* cancel = RodakosCreateHeaderIconButton(toolbar, FONT_AWESOME_XMARK);
    lv_obj_set_size(cancel, 44, 40);
    lv_obj_align(cancel, LV_ALIGN_LEFT_MID, 4, 0);
    BindTileAction(cancel, TileAction::kCancelEditing);

    auto* mode_title = lv_label_create(toolbar);
    lv_label_set_text(mode_title, "Arrange");
    lv_obj_set_style_text_color(mode_title, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(mode_title, &phone_font_14, 0);
    lv_obj_center(mode_title);

    editor_done_button_ = RodakosCreateHeaderIconButton(toolbar, FONT_AWESOME_CHECK);
    lv_obj_set_size(editor_done_button_, 44, 40);
    lv_obj_align(editor_done_button_, LV_ALIGN_RIGHT_MID, -4, 0);
    BindTileAction(editor_done_button_, TileAction::kSaveEditing);

    auto* icon_bg = lv_obj_create(layout_editor_);
    lv_obj_remove_style_all(icon_bg);
    lv_obj_set_pos(icon_bg, 16, 42);
    lv_obj_set_size(icon_bg, 40, 40);
    lv_obj_set_style_radius(
        icon_bg,
        item->type == rodakos::HomeLayoutItemType::kApp ? LV_RADIUS_CIRCLE : 8, 0);
    lv_obj_set_style_bg_color(
        icon_bg,
        item->type == rodakos::HomeLayoutItemType::kApp
            ? rodakos_theme_icon_color(IconColorIndex(item->id))
            : rodakos_theme_bg_tertiary(),
        0);
    lv_obj_set_style_bg_opa(icon_bg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_SCROLLABLE);

    std::string item_title = item->type == rodakos::HomeLayoutItemType::kApp
                                 ? item->id
                                 : item->name;
    std::string item_type = item->type == rodakos::HomeLayoutItemType::kFolder
                                ? "Folder"
                            : editing_target_->kind == LayoutEditTargetKind::kFolderApp
                                ? "Folder App"
                                : "App";
    if (item->type == rodakos::HomeLayoutItemType::kApp && context_ != nullptr) {
        const auto apps = context_->registry().ListHomeApps();
        const auto descriptor = std::find_if(
            apps.begin(), apps.end(), [&](const PhoneAppDescriptor* app) {
                return app != nullptr && app->id == item->id;
            });
        if (descriptor != apps.end()) {
            item_title = (*descriptor)->title;
            auto* icon = lv_label_create(icon_bg);
            lv_label_set_text(icon, (*descriptor)->icon.c_str());
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_set_style_text_font(icon, PhoneIconFont(), 0);
            lv_obj_center(icon);
        }
    } else {
        const size_t preview_count = std::min<size_t>(item->apps.size(), 4);
        for (size_t preview_index = 0; preview_index < preview_count; ++preview_index) {
            auto* preview = lv_obj_create(icon_bg);
            lv_obj_remove_style_all(preview);
            lv_obj_set_size(preview, 12, 12);
            const lv_coord_t x = preview_index % 2 == 0 ? -7 : 7;
            const lv_coord_t y = preview_index < 2 ? -7 : 7;
            lv_obj_align(preview, LV_ALIGN_CENTER, x, y);
            lv_obj_set_style_radius(preview, 3, 0);
            lv_obj_set_style_bg_color(
                preview, rodakos_theme_icon_color(IconColorIndex(item->apps[preview_index])), 0);
            lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, 0);
            lv_obj_clear_flag(preview, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(preview, LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    auto* item_label = lv_label_create(layout_editor_);
    lv_label_set_text(item_label, item_title.c_str());
    lv_obj_set_pos(item_label, 68, 43);
    lv_obj_set_width(item_label, 226);
    lv_label_set_long_mode(item_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(item_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(item_label, &phone_font_14, 0);

    auto* type_label = lv_label_create(layout_editor_);
    lv_label_set_text(type_label, item_type.c_str());
    lv_obj_set_pos(type_label, 68, 65);
    lv_obj_set_style_text_color(type_label, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(type_label, &phone_font_12, 0);

    editor_position_label_ = lv_label_create(layout_editor_);
    lv_obj_set_width(editor_position_label_, 86);
    lv_obj_set_pos(editor_position_label_, 208, 65);
    lv_obj_set_style_text_align(editor_position_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(editor_position_label_, rodakos_theme_text_secondary(), 0);
    lv_obj_set_style_text_font(editor_position_label_, &phone_font_12, 0);

    auto create_button = [&](lv_coord_t x, lv_coord_t y, lv_coord_t width,
                             lv_coord_t height, const char* text,
                             TileAction action, bool destructive = false) {
        auto* button = lv_btn_create(layout_editor_);
        lv_obj_remove_style_all(button);
        lv_obj_set_pos(button, x, y);
        lv_obj_set_size(button, width, height);
        lv_obj_set_style_radius(button, 6, 0);
        lv_obj_set_style_bg_color(
            button, destructive ? rodakos_theme_error() : rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_bg_color(button, rodakos_theme_secondary(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        BindTileAction(button, action);
        auto* label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(
            label, destructive ? lv_color_white() : rodakos_theme_text_primary(), 0);
        lv_obj_set_style_text_font(
            label,
            action == TileAction::kMovePrevious || action == TileAction::kMoveNext
                ? PhoneIconFont()
                : &phone_font_12,
            0);
        lv_obj_center(label);
        return button;
    };

    editor_previous_button_ = create_button(
        16, 86, 136, 42, FONT_AWESOME_ARROW_LEFT, TileAction::kMovePrevious);
    editor_next_button_ = create_button(
        168, 86, 136, 42, FONT_AWESOME_ARROW_RIGHT, TileAction::kMoveNext);

    if (editing_target_->kind == LayoutEditTargetKind::kFolderApp) {
        editor_primary_action_ = create_button(
            16, 132, 136, 40, "Folder...", TileAction::kOpenDestination);
        editor_secondary_action_ = create_button(
            168, 132, 136, 40, "Move out", TileAction::kMoveOut);
    } else if (item->type == rodakos::HomeLayoutItemType::kFolder) {
        editor_primary_action_ = create_button(
            16, 132, 136, 40, "Rename", TileAction::kRenameFolder);
        editor_secondary_action_ = create_button(
            168, 132, 136, 40, "Dissolve", TileAction::kOpenDissolveConfirm, true);
    } else {
        editor_primary_action_ = create_button(
            16, 132, 288, 40, "Folder...", TileAction::kOpenDestination);
    }

    editor_error_label_ = lv_label_create(layout_editor_);
    lv_label_set_text(editor_error_label_, "");
    lv_obj_set_size(editor_error_label_, 296, 16);
    lv_obj_align(editor_error_label_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_label_set_long_mode(editor_error_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(editor_error_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(editor_error_label_, rodakos_theme_error(), 0);
    lv_obj_set_style_text_font(editor_error_label_, &phone_font_12, 0);
}

void HomeApp::CreateLayoutDestinationUi() {
    if (!HasEditingTarget() || layout_editor_ == nullptr) {
        return;
    }
    auto* toolbar = lv_obj_create(layout_editor_);
    lv_obj_remove_style_all(toolbar);
    lv_obj_set_size(toolbar, LV_PCT(100), 40);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);

    auto* back = RodakosCreateHeaderIconButton(toolbar, FONT_AWESOME_ARROW_LEFT);
    lv_obj_set_size(back, 44, 40);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 4, 0);
    BindTileAction(back, TileAction::kCloseEditorSubview);

    auto* title = lv_label_create(toolbar);
    lv_label_set_text(title, "Choose Folder");
    lv_obj_set_style_text_color(title, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(title, &phone_font_14, 0);
    lv_obj_center(title);

    auto* list = lv_obj_create(layout_editor_);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), 152);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(list, 4, 0);
    lv_obj_set_style_pad_bottom(list, 4, 0);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    auto add_row = [&](const std::string& id, const std::string& label,
                       const std::string& detail, TileAction action) {
        auto* row = lv_btn_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 300, 44);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_bg_color(row, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_bg_color(row, rodakos_theme_secondary(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        BindTileAction(row, action, id);
        auto* name = lv_label_create(row);
        lv_label_set_text(name, label.c_str());
        lv_obj_set_width(name, 214);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(name, rodakos_theme_text_primary(), 0);
        lv_obj_set_style_text_font(name, &phone_font_14, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, 0);
        auto* count = lv_label_create(row);
        lv_label_set_text(count, detail.c_str());
        lv_obj_set_style_text_color(count, rodakos_theme_text_tertiary(), 0);
        lv_obj_set_style_text_font(count, &phone_font_12, 0);
        lv_obj_align(count, LV_ALIGN_RIGHT_MID, -12, 0);
    };

    size_t option_count = 0;
    if (editing_target_->kind == LayoutEditTargetKind::kRootItem &&
        editing_target_->root_item.type == rodakos::HomeLayoutItemType::kApp) {
        add_row({}, "New Folder", "+", TileAction::kCreateFolder);
        ++option_count;
    }
    const auto projection = rodakos::ProjectHomeLayout(draft_layout_);
    for (const auto& item : projection.managed_items) {
        if (item.type != rodakos::HomeLayoutItemType::kFolder ||
            item.apps.size() >= rodakos::kHomeFolderMaxApps ||
            (editing_target_->kind == LayoutEditTargetKind::kFolderApp &&
             item.id == editing_target_->folder_id)) {
            continue;
        }
        add_row(item.id, item.name, std::to_string(item.apps.size()) + "/12",
                TileAction::kSelectDestination);
        ++option_count;
    }
    if (option_count == 0) {
        auto* empty = lv_label_create(list);
        lv_label_set_text(empty, "No available folders");
        lv_obj_set_width(empty, 300);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(empty, rodakos_theme_text_tertiary(), 0);
        lv_obj_set_style_text_font(empty, &phone_font_12, 0);
    }
}

void HomeApp::CreateLayoutDissolveUi() {
    if (!HasEditingTarget() || layout_editor_ == nullptr ||
        editing_target_->kind != LayoutEditTargetKind::kRootItem ||
        editing_target_->root_item.type != rodakos::HomeLayoutItemType::kFolder) {
        editor_page_ = HomeEditorPage::kOverview;
        return;
    }
    const auto folder_index = rodakos::FindHomeRootItem(
        draft_layout_, editing_target_->root_item);
    if (!folder_index.has_value()) {
        return;
    }

    auto* toolbar = lv_obj_create(layout_editor_);
    lv_obj_remove_style_all(toolbar);
    lv_obj_set_size(toolbar, LV_PCT(100), 40);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);
    auto* back = RodakosCreateHeaderIconButton(toolbar, FONT_AWESOME_ARROW_LEFT);
    lv_obj_set_size(back, 44, 40);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 4, 0);
    BindTileAction(back, TileAction::kCloseEditorSubview);
    auto* title = lv_label_create(toolbar);
    lv_label_set_text(title, "Dissolve Folder");
    lv_obj_set_style_text_color(title, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(title, &phone_font_14, 0);
    lv_obj_center(title);

    auto* message = lv_label_create(layout_editor_);
    const std::string text = "Move " +
                             std::to_string(draft_layout_.items[*folder_index].apps.size()) +
                             " apps to Home?";
    lv_label_set_text(message, text.c_str());
    lv_obj_set_width(message, 288);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(message, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(message, &phone_font_14, 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 58);

    auto create_button = [&](lv_coord_t x, const char* label, TileAction action,
                             lv_color_t color) {
        auto* button = lv_btn_create(layout_editor_);
        lv_obj_remove_style_all(button);
        lv_obj_set_pos(button, x, 108);
        lv_obj_set_size(button, 136, 44);
        lv_obj_set_style_radius(button, 6, 0);
        lv_obj_set_style_bg_color(button, color, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        BindTileAction(button, action);
        auto* text_label = lv_label_create(button);
        lv_label_set_text(text_label, label);
        lv_obj_set_style_text_color(
            text_label,
            action == TileAction::kConfirmDissolve
                ? lv_color_white()
                : rodakos_theme_text_primary(),
            0);
        lv_obj_set_style_text_font(text_label, &phone_font_12, 0);
        lv_obj_center(text_label);
    };
    create_button(16, "Back", TileAction::kCloseEditorSubview,
                  rodakos_theme_bg_secondary());
    create_button(168, "Dissolve", TileAction::kConfirmDissolve,
                  rodakos_theme_error());
}

void HomeApp::CloseLayoutEditor() {
    CloseFolderNameDialog();
    if (layout_editor_ != nullptr && lv_obj_is_valid(layout_editor_)) {
        lv_obj_delete(layout_editor_);
    }
    ResetLayoutEditorPointers();
    if (collection_view_ != nullptr && lv_obj_is_valid(collection_view_)) {
        lv_obj_remove_flag(collection_view_, LV_OBJ_FLAG_HIDDEN);
        if (tileview_ != nullptr) {
            lv_obj_add_flag(tileview_, LV_OBJ_FLAG_HIDDEN);
        }
        if (page_indicator_ != nullptr) {
            lv_obj_add_flag(page_indicator_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        if (tileview_ != nullptr) {
            lv_obj_remove_flag(tileview_, LV_OBJ_FLAG_HIDDEN);
        }
        if (page_indicator_ != nullptr) {
            lv_obj_remove_flag(page_indicator_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    editing_target_.reset();
    editor_focus_item_.reset();
    editor_error_status_.reset();
    editor_action_error_.clear();
    editor_page_ = HomeEditorPage::kOverview;
    draft_layout_ = {};
    editor_locked_ = false;
}

void HomeApp::ResetLayoutEditorPointers() {
    layout_editor_ = nullptr;
    editor_position_label_ = nullptr;
    editor_error_label_ = nullptr;
    editor_previous_button_ = nullptr;
    editor_next_button_ = nullptr;
    editor_done_button_ = nullptr;
    editor_primary_action_ = nullptr;
    editor_secondary_action_ = nullptr;
}

bool HomeApp::HasEditingTarget() const {
    return editing_target_.has_value();
}

void HomeApp::MoveLayoutDraft(rodakos::HomeMoveDirection direction) {
    if (!HasEditingTarget() || edit_action_pending_ || editor_locked_ ||
        !layout_write_allowed_) {
        return;
    }

    rodakos::HomeLayoutEditResult moved;
    if (editing_target_->kind == LayoutEditTargetKind::kRootItem) {
        moved = rodakos::MoveHomeManagedRootItem(
            draft_layout_, editing_target_->root_item, direction);
    } else {
        moved = rodakos::MoveHomeFolderApp(
            draft_layout_,
            {rodakos::HomeLayoutItemType::kFolder, editing_target_->folder_id},
            editing_target_->app_id, direction);
    }
    if (moved.status != rodakos::HomeLayoutEditStatus::kApplied ||
        !moved.candidate.has_value()) {
        return;
    }
    draft_layout_ = std::move(*moved.candidate);
    editor_error_status_.reset();
    editor_action_error_.clear();
    UpdateLayoutEditor();
}

void HomeApp::MoveLayoutDraftToFolder(const std::string& folder_id) {
    if (!HasEditingTarget() || editor_locked_ || !layout_write_allowed_) {
        return;
    }
    const rodakos::HomeRootItemKey destination{
        rodakos::HomeLayoutItemType::kFolder, folder_id};
    if (!rodakos::FindHomeManagedRootItem(
            rodakos::ProjectHomeLayout(draft_layout_), destination).has_value()) {
        SetEditorActionError("Folder is no longer available.");
        return;
    }

    rodakos::HomeLayoutEditResult moved;
    std::string app_id;
    if (editing_target_->kind == LayoutEditTargetKind::kRootItem &&
        editing_target_->root_item.type == rodakos::HomeLayoutItemType::kApp) {
        app_id = editing_target_->root_item.id;
        moved = rodakos::MoveHomeRootAppIntoFolder(
            draft_layout_, editing_target_->root_item, destination);
    } else if (editing_target_->kind == LayoutEditTargetKind::kFolderApp) {
        app_id = editing_target_->app_id;
        moved = rodakos::MoveHomeFolderAppIntoFolder(
            draft_layout_,
            {rodakos::HomeLayoutItemType::kFolder, editing_target_->folder_id},
            app_id, destination);
    } else {
        SetEditorActionError("Action unavailable.");
        return;
    }
    if (moved.status == rodakos::HomeLayoutEditStatus::kFolderFull) {
        SetEditorActionError("Folder is full.");
        return;
    }
    if (moved.status != rodakos::HomeLayoutEditStatus::kApplied ||
        !moved.candidate.has_value()) {
        SetEditorActionError("Could not move app.");
        return;
    }

    draft_layout_ = std::move(*moved.candidate);
    editing_target_ = LayoutEditTarget{
        .kind = LayoutEditTargetKind::kFolderApp,
        .root_item = {},
        .folder_id = folder_id,
        .app_id = std::move(app_id),
    };
    editor_focus_item_ = destination;
    editor_page_ = HomeEditorPage::kOverview;
    editor_error_status_.reset();
    editor_action_error_.clear();
    CreateLayoutEditorUi();
}

void HomeApp::MoveLayoutDraftOut() {
    if (!HasEditingTarget() || editing_target_->kind != LayoutEditTargetKind::kFolderApp ||
        editor_locked_ || !layout_write_allowed_) {
        return;
    }
    const std::string app_id = editing_target_->app_id;
    const auto moved = rodakos::MoveHomeFolderAppToRoot(
        draft_layout_,
        {rodakos::HomeLayoutItemType::kFolder, editing_target_->folder_id}, app_id);
    if (moved.status != rodakos::HomeLayoutEditStatus::kApplied ||
        !moved.candidate.has_value()) {
        SetEditorActionError("Could not move app to Home.");
        return;
    }
    draft_layout_ = std::move(*moved.candidate);
    const rodakos::HomeRootItemKey app{
        rodakos::HomeLayoutItemType::kApp, app_id};
    editing_target_ = LayoutEditTarget{
        .kind = LayoutEditTargetKind::kRootItem,
        .root_item = app,
        .folder_id = {},
        .app_id = {},
    };
    editor_focus_item_ = app;
    editor_page_ = HomeEditorPage::kOverview;
    editor_error_status_.reset();
    editor_action_error_.clear();
    CreateLayoutEditorUi();
}

void HomeApp::DissolveLayoutDraftFolder() {
    if (!HasEditingTarget() || editing_target_->kind != LayoutEditTargetKind::kRootItem ||
        editing_target_->root_item.type != rodakos::HomeLayoutItemType::kFolder ||
        editor_locked_ || !layout_write_allowed_) {
        return;
    }
    const auto folder_index = rodakos::FindHomeRootItem(
        draft_layout_, editing_target_->root_item);
    if (!folder_index.has_value() || draft_layout_.items[*folder_index].apps.empty()) {
        SetEditorActionError("Folder is no longer available.");
        return;
    }
    const std::string first_app = draft_layout_.items[*folder_index].apps.front();
    const auto dissolved = rodakos::DissolveHomeFolder(
        draft_layout_, editing_target_->root_item);
    if (dissolved.status != rodakos::HomeLayoutEditStatus::kApplied ||
        !dissolved.candidate.has_value()) {
        SetEditorActionError("Could not dissolve folder.");
        return;
    }
    draft_layout_ = std::move(*dissolved.candidate);
    const rodakos::HomeRootItemKey app{
        rodakos::HomeLayoutItemType::kApp, first_app};
    editing_target_ = LayoutEditTarget{
        .kind = LayoutEditTargetKind::kRootItem,
        .root_item = app,
        .folder_id = {},
        .app_id = {},
    };
    editor_focus_item_ = app;
    editor_page_ = HomeEditorPage::kOverview;
    editor_error_status_.reset();
    editor_action_error_.clear();
    CreateLayoutEditorUi();
}

void HomeApp::OpenFolderNameDialog(FolderNameMode mode) {
    if (!HasEditingTarget() || ui_ == nullptr || folder_name_dialog_ != nullptr) {
        return;
    }
    std::string initial_name = "Folder";
    if (mode == FolderNameMode::kRename) {
        if (editing_target_->kind != LayoutEditTargetKind::kRootItem ||
            editing_target_->root_item.type != rodakos::HomeLayoutItemType::kFolder) {
            return;
        }
        const auto folder_index = rodakos::FindHomeRootItem(
            draft_layout_, editing_target_->root_item);
        if (!folder_index.has_value()) {
            return;
        }
        initial_name = draft_layout_.items[*folder_index].name;
    } else if (mode != FolderNameMode::kCreate ||
               editing_target_->kind != LayoutEditTargetKind::kRootItem ||
               editing_target_->root_item.type != rodakos::HomeLayoutItemType::kApp) {
        return;
    }

    folder_name_mode_ = mode;
    folder_name_dialog_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(folder_name_dialog_);
    lv_obj_set_size(folder_name_dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(folder_name_dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(folder_name_dialog_, LV_OPA_70, 0);
    lv_obj_clear_flag(folder_name_dialog_, LV_OBJ_FLAG_SCROLLABLE);

    auto* box = lv_obj_create(folder_name_dialog_);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 304, 112);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_bg_color(box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    auto* cancel = RodakosCreateHeaderIconButton(box, FONT_AWESOME_XMARK);
    lv_obj_set_size(cancel, 40, 40);
    lv_obj_align(cancel, LV_ALIGN_TOP_LEFT, 2, 0);
    BindTileAction(cancel, TileAction::kCancelFolderName);
    auto* title = lv_label_create(box);
    lv_label_set_text(title, mode == FolderNameMode::kCreate ? "New Folder" : "Rename Folder");
    lv_obj_set_style_text_color(title, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(title, &phone_font_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
    auto* apply = RodakosCreateHeaderIconButton(box, FONT_AWESOME_CHECK);
    lv_obj_set_size(apply, 40, 40);
    lv_obj_align(apply, LV_ALIGN_TOP_RIGHT, -2, 0);
    BindTileAction(apply, TileAction::kApplyFolderName);

    folder_name_textarea_ = lv_textarea_create(box);
    lv_obj_set_size(folder_name_textarea_, 288, 40);
    lv_obj_align(folder_name_textarea_, LV_ALIGN_TOP_MID, 0, 42);
    lv_textarea_set_one_line(folder_name_textarea_, true);
    lv_textarea_set_max_length(folder_name_textarea_, rodakos::kHomeFolderNameMaxBytes);
    lv_textarea_set_text(folder_name_textarea_, initial_name.c_str());
    lv_obj_set_style_bg_color(folder_name_textarea_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(folder_name_textarea_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(folder_name_textarea_, &phone_font_14, 0);
    lv_obj_set_style_border_width(folder_name_textarea_, 0, 0);
    lv_obj_set_style_radius(folder_name_textarea_, 6, 0);

    folder_name_error_label_ = lv_label_create(box);
    lv_label_set_text(folder_name_error_label_, "");
    lv_obj_set_size(folder_name_error_label_, 288, 22);
    lv_obj_align(folder_name_error_label_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_label_set_long_mode(folder_name_error_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(folder_name_error_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(folder_name_error_label_, rodakos_theme_error(), 0);
    lv_obj_set_style_text_font(folder_name_error_label_, &phone_font_12, 0);
    lv_obj_add_flag(folder_name_error_label_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_move_foreground(folder_name_dialog_);
    soft_keyboard_.Show(folder_name_textarea_);
}

void HomeApp::CloseFolderNameDialog() {
    soft_keyboard_.Hide();
    if (folder_name_dialog_ != nullptr && lv_obj_is_valid(folder_name_dialog_)) {
        lv_obj_delete(folder_name_dialog_);
    }
    folder_name_dialog_ = nullptr;
    folder_name_textarea_ = nullptr;
    folder_name_error_label_ = nullptr;
    folder_name_mode_ = FolderNameMode::kNone;
}

void HomeApp::ApplyFolderName(std::string name) {
    if (!HasEditingTarget() || folder_name_mode_ == FolderNameMode::kNone) {
        return;
    }
    if (!rodakos::IsValidHomeFolderName(name)) {
        if (folder_name_error_label_ != nullptr) {
            lv_label_set_text(folder_name_error_label_, "Use at most 24 UTF-8 bytes.");
            lv_obj_remove_flag(folder_name_error_label_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    rodakos::HomeLayoutEditResult edited;
    std::optional<rodakos::HomeRootItemKey> next_focus;
    if (folder_name_mode_ == FolderNameMode::kCreate) {
        const auto folder_id = rodakos::GenerateUniqueHomeFolderId(
            draft_layout_, []() { return esp_random(); });
        if (!folder_id.has_value()) {
            if (folder_name_error_label_ != nullptr) {
                lv_label_set_text(folder_name_error_label_, "Could not allocate a folder ID.");
                lv_obj_remove_flag(folder_name_error_label_, LV_OBJ_FLAG_HIDDEN);
            }
            return;
        }
        edited = rodakos::CreateHomeFolder(
            draft_layout_, *folder_id, std::move(name),
            {editing_target_->root_item});
        next_focus = rodakos::HomeRootItemKey{
            rodakos::HomeLayoutItemType::kFolder, *folder_id};
    } else {
        edited = rodakos::RenameHomeFolder(
            draft_layout_, editing_target_->root_item, std::move(name));
        next_focus = editing_target_->root_item;
    }

    if (edited.status == rodakos::HomeLayoutEditStatus::kNoChange) {
        CloseFolderNameDialog();
        editor_page_ = HomeEditorPage::kOverview;
        CreateLayoutEditorUi();
        return;
    }
    if (edited.status != rodakos::HomeLayoutEditStatus::kApplied ||
        !edited.candidate.has_value() || !next_focus.has_value()) {
        if (folder_name_error_label_ != nullptr) {
            lv_label_set_text(folder_name_error_label_, "Folder name was not applied.");
            lv_obj_remove_flag(folder_name_error_label_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    draft_layout_ = std::move(*edited.candidate);
    editing_target_ = LayoutEditTarget{
        .kind = LayoutEditTargetKind::kRootItem,
        .root_item = *next_focus,
        .folder_id = {},
        .app_id = {},
    };
    editor_focus_item_ = next_focus;
    editor_error_status_.reset();
    editor_action_error_.clear();
    editor_page_ = HomeEditorPage::kOverview;
    CloseFolderNameDialog();
    CreateLayoutEditorUi();
}

void HomeApp::SetEditorActionError(std::string message) {
    editor_action_error_ = std::move(message);
    editor_error_status_.reset();
    if (editor_page_ != HomeEditorPage::kOverview) {
        editor_page_ = HomeEditorPage::kOverview;
        CreateLayoutEditorUi();
    } else {
        UpdateLayoutEditor();
    }
}

void HomeApp::SaveLayoutDraft() {
    if (!HasEditingTarget()) {
        return;
    }
    if (draft_layout_ == layout_) {
        CloseLayoutEditor();
        return;
    }
    if (editor_locked_ || !layout_write_allowed_ || !layout_store_.write_allowed()) {
        ShowLayoutSaveFailure(rodakos::HomeLayoutSaveStatus::kReadOnly);
        return;
    }

    rodakos::HomeLayoutSaveResult saved = layout_store_.Save(draft_layout_);
    layout_write_allowed_ = layout_store_.write_allowed();
    if (saved.status != rodakos::HomeLayoutSaveStatus::kSaved ||
        !saved.layout.has_value()) {
        ShowLayoutSaveFailure(saved.status);
        return;
    }

    const auto focused_item = editor_focus_item_;
    layout_ = std::move(*saved.layout);
    projection_ = rodakos::ProjectHomeLayout(layout_);
    if (focused_item.has_value()) {
        g_home_page_session.Focus(projection_, *focused_item);
    }
    collection_state_ = {};
    editing_target_.reset();
    editor_focus_item_.reset();
    editor_error_status_.reset();
    editor_action_error_.clear();
    editor_page_ = HomeEditorPage::kOverview;
    draft_layout_ = {};
    editor_locked_ = false;
    if (!RebuildUi()) {
        ESP_LOGE(TAG, "Failed to rebuild Home after saving the layout");
    }
}

void HomeApp::UpdateLayoutEditor() {
    if (!HasEditingTarget() || layout_editor_ == nullptr ||
        editor_page_ != HomeEditorPage::kOverview) {
        return;
    }
    rodakos::HomeLayoutEditorState state;
    if (editing_target_->kind == LayoutEditTargetKind::kRootItem) {
        state = rodakos::ResolveHomeRootEditorState(
            draft_layout_, editing_target_->root_item);
    } else {
        const rodakos::HomeRootItemKey folder{
            rodakos::HomeLayoutItemType::kFolder, editing_target_->folder_id};
        state = rodakos::ResolveHomeFolderAppEditorState(
            draft_layout_, folder, editing_target_->app_id);
    }

    if (!state.position.has_value()) {
        SetButtonEnabled(editor_previous_button_, false);
        SetButtonEnabled(editor_next_button_, false);
        SetButtonEnabled(editor_done_button_, false);
        SetButtonEnabled(editor_primary_action_, false);
        SetButtonEnabled(editor_secondary_action_, false);
        return;
    }
    const std::string position_text = std::to_string(*state.position + 1) + " / " +
                                      std::to_string(state.position_count);
    if (editor_position_label_ != nullptr) {
        lv_label_set_text(editor_position_label_, position_text.c_str());
    }

    const bool editing_enabled =
        layout_write_allowed_ && layout_store_.write_allowed() &&
        !editor_locked_ && !edit_action_pending_;
    SetButtonEnabled(editor_previous_button_, editing_enabled && state.can_previous);
    SetButtonEnabled(editor_next_button_, editing_enabled && state.can_next);
    SetButtonEnabled(editor_done_button_, editing_enabled);
    if (editing_target_->kind == LayoutEditTargetKind::kFolderApp ||
        editing_target_->root_item.type == rodakos::HomeLayoutItemType::kApp) {
        SetButtonEnabled(editor_primary_action_,
                         editing_enabled && state.can_open_destination);
    } else {
        SetButtonEnabled(editor_primary_action_, editing_enabled);
    }
    SetButtonEnabled(editor_secondary_action_, editing_enabled);

    if (editor_error_label_ != nullptr) {
        const char* message = nullptr;
        if (!editor_action_error_.empty()) {
            message = editor_action_error_.c_str();
        } else if (editor_error_status_.has_value()) {
            message = LayoutSaveFailureText(*editor_error_status_);
        }
        if (message != nullptr) {
            lv_label_set_text(editor_error_label_, message);
            lv_obj_remove_flag(editor_error_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(editor_error_label_, "");
            lv_obj_add_flag(editor_error_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void HomeApp::ShowLayoutSaveFailure(rodakos::HomeLayoutSaveStatus status) {
    editor_error_status_ = status;
    editor_action_error_.clear();
    const bool lock_session = rodakos::HomeLayoutSaveLocksEditing(status);
    editor_locked_ = editor_locked_ || lock_session || !layout_store_.write_allowed();
    layout_write_allowed_ = layout_store_.write_allowed() && !lock_session;
    editor_page_ = HomeEditorPage::kOverview;
    if (layout_editor_ == nullptr || !lv_obj_is_valid(layout_editor_)) {
        CreateLayoutEditorUi();
    } else {
        UpdateLayoutEditor();
    }
}

bool HomeApp::RebuildUi() {
    if (context_ == nullptr || ui_ == nullptr) {
        return false;
    }
    DestroyUi();
    return CreateUi(*context_);
}

void HomeApp::QueueEditAction(
    DeferredEditAction action,
    std::optional<LayoutEditTarget> target,
    std::string value) {
    if (edit_action_pending_) {
        return;
    }
    auto* payload = new DeferredEditPayload{
        .owner = this,
        .action = action,
        .target = std::move(target),
        .value = std::move(value),
    };
    pending_edit_ = payload;
    edit_action_pending_ = true;
    if (layout_editor_ != nullptr) {
        UpdateLayoutEditor();
    }
    if (lv_async_call(RunDeferredEdit, payload) != LV_RESULT_OK) {
        pending_edit_ = nullptr;
        edit_action_pending_ = false;
        delete payload;
        if (layout_editor_ != nullptr) {
            editor_error_status_.reset();
            UpdateLayoutEditor();
            if (editor_error_label_ != nullptr) {
                lv_label_set_text(
                    editor_error_label_,
                    action == DeferredEditAction::kCancel
                        ? "Could not close editor."
                        : "Action unavailable.");
                lv_obj_remove_flag(editor_error_label_, LV_OBJ_FLAG_HIDDEN);
            }
        } else if (ui_ != nullptr) {
            ui_->ShowToastUnlocked("Arrange unavailable.");
        }
        ESP_LOGE(TAG, "Failed to queue Home edit action");
    }
}

void HomeApp::CancelPendingEditAction() {
    if (pending_edit_ == nullptr) {
        edit_action_pending_ = false;
        return;
    }
    DeferredEditPayload* payload = pending_edit_;
    pending_edit_ = nullptr;
    edit_action_pending_ = false;
    payload->owner = nullptr;
    if (lv_async_call_cancel(RunDeferredEdit, payload) == LV_RESULT_OK) {
        delete payload;
    }
}

void HomeApp::QueueLaunch(const std::string& app_id) {
    if (launch_pending_) {
        ESP_LOGW(TAG, "Ignoring app tap while launch is pending: %s", app_id.c_str());
        return;
    }

    auto* payload = new DeferredLaunchPayload{
        .owner = this,
        .context = context_,
        .app_id = app_id,
    };
    pending_launch_ = payload;
    launch_pending_ = true;

    if (lv_async_call(RunDeferredLaunch, payload) != LV_RESULT_OK) {
        pending_launch_ = nullptr;
        launch_pending_ = false;
        delete payload;
        ESP_LOGE(TAG, "Failed to queue app launch from Home: %s", app_id.c_str());
    }
}

void HomeApp::CancelPendingLaunch() {
    if (pending_launch_ == nullptr) {
        launch_pending_ = false;
        return;
    }

    DeferredLaunchPayload* payload = pending_launch_;
    pending_launch_ = nullptr;
    launch_pending_ = false;
    payload->owner = nullptr;

    if (lv_async_call_cancel(RunDeferredLaunch, payload) == LV_RESULT_OK) {
        delete payload;
    }
}

void HomeApp::UpdatePageIndicator() {
    if (tileview_ == nullptr || page_indicator_ == nullptr) {
        return;
    }

    const size_t active_page = ActivePageIndex();
    g_home_page_session.Capture(projection_, active_page);

    const uint32_t dot_count = lv_obj_get_child_count(page_indicator_);
    for (uint32_t page_index = 0; page_index < dot_count; ++page_index) {
        lv_obj_t* dot = lv_obj_get_child(page_indicator_, static_cast<int32_t>(page_index));
        const bool active = page_index == active_page;
        lv_obj_set_size(dot, active ? 18 : 6, 6);
        lv_obj_set_style_bg_color(dot,
                                  active ? rodakos_theme_secondary() : rodakos_theme_text_tertiary(), 0);
        const lv_opa_t opacity = active ? static_cast<lv_opa_t>(LV_OPA_COVER)
                                        : static_cast<lv_opa_t>(128);
        lv_obj_set_style_bg_opa(dot, opacity, 0);
    }
}

void HomeApp::DestroyUi() {
    CancelPendingPageWindowRefresh();
    CancelPendingEditAction();
    CancelPendingLaunch();
    if (folder_name_dialog_ != nullptr || soft_keyboard_.IsVisible()) {
        CloseFolderNameDialog();
    }
    if (clock_timer_ != nullptr) {
        lv_timer_delete(clock_timer_);
        clock_timer_ = nullptr;
    }
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
    ResetUiPointers();
}

void HomeApp::ResetUiPointers() {
    root_ = nullptr;
    body_ = nullptr;
    footer_ = nullptr;
    tileview_ = nullptr;
    collection_view_ = nullptr;
    ResetLayoutEditorPointers();
    page_indicator_ = nullptr;
    clock_label_ = nullptr;
    status_cluster_ = nullptr;
    wifi_label_ = nullptr;
    page_tiles_.clear();
    page_populated_.clear();
    page_window_refresh_pending_ = false;
    pending_page_window_ = 0;
}

void HomeApp::UpdateClock() {
    if (clock_label_ == nullptr) {
        return;
    }

    std::time_t now = std::time(nullptr);
    std::tm timeinfo = {};
    localtime_r(&now, &timeinfo);

    char time_text[8] = {};
    if (timeinfo.tm_year < 120) {
        std::snprintf(time_text, sizeof(time_text), "--:--");
    } else {
        std::snprintf(time_text, sizeof(time_text), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    }

    lv_label_set_text(clock_label_, time_text);

    if (wifi_label_ != nullptr && context_ != nullptr) {
        auto* wifi = context_->services().wifi();
        const bool connected = wifi != nullptr && wifi->GetStatus() == WiFiStatus::kConnected;
        lv_label_set_text(wifi_label_, connected ? FONT_AWESOME_WIFI : FONT_AWESOME_WIFI_SLASH);
        lv_obj_set_style_text_color(wifi_label_,
                                    connected ? rodakos_theme_text_primary() : rodakos_theme_text_tertiary(), 0);
    }
}

void RegisterHomeApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "home",
        .title = "Home",
        .icon = FONT_AWESOME_HOUSE,
        .category = PhoneAppCategory::kSystem,
        .role = PhoneAppRole::kHome,
        .capabilities = PhoneCapability::kNone,
        .show_on_home = false,
        .aliases = {"desktop", "launcher"},
        .create = []() { return std::make_unique<HomeApp>(); },
    });
}

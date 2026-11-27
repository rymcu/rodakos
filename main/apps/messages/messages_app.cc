#include "apps/messages/messages_app.h"

#include "phone_os/inbox_service.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <esp_log.h>

#include <cstdio>
#include <ctime>
#include <memory>
#include <string>

namespace {
constexpr const char* TAG = "MessagesApp";
constexpr uint32_t kRefreshPeriodMs = 250;
constexpr lv_coord_t kContentTop = kRodakosAppHeaderHeight;
constexpr lv_coord_t kFilterBarHeight = 28;
constexpr lv_coord_t kFooterHeight = 50;

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        context->navigation().ReturnHome();
    }
}

lv_obj_t* CreatePlain(lv_obj_t* parent) {
    lv_obj_t* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

lv_obj_t* CreateText(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

// Local wall time; falls back to an empty string before SNTP.
std::string FormatTime(int64_t unix_seconds, bool with_date) {
    if (unix_seconds <= 0) return {};
    const std::time_t value = static_cast<std::time_t>(unix_seconds);
    std::tm local = {};
    localtime_r(&value, &local);
    const std::time_t now = std::time(nullptr);
    std::tm today = {};
    localtime_r(&now, &today);
    char text[24] = {};
    const bool same_day = local.tm_year == today.tm_year && local.tm_yday == today.tm_yday;
    if (same_day || !with_date) {
        std::strftime(text, sizeof(text), "%H:%M", &local);
    } else {
        std::strftime(text, sizeof(text), "%m-%d %H:%M", &local);
    }
    return text;
}

struct Chip {
    const char* text;
    lv_color_t color;
};

Chip ItemChip(const rodakos::InboxItem& item) {
    if (item.op_state == rodakos::InboxOpState::kPendingReconcile) {
        return {"待核对", rodakos_theme_warning()};
    }
    if (item.op_state == rodakos::InboxOpState::kSubmitting) {
        return {"同步中…", rodakos_theme_primary()};
    }
    if (item.kind == rodakos::InboxKind::kNotice) {
        return {"通知", rodakos_theme_text_tertiary()};
    }
    switch (item.todo_state) {
        case rodakos::InboxTodoState::kDone:
            return {"已完成", rodakos_theme_success()};
        case rodakos::InboxTodoState::kSnoozed:
            return {"已推迟", rodakos_theme_info()};
        case rodakos::InboxTodoState::kOpen:
        default:
            return {"待办", rodakos_theme_error()};
    }
}

lv_obj_t* CreateChip(lv_obj_t* parent, const Chip& chip) {
    lv_obj_t* label = CreateText(parent, chip.text, &phone_font_12, chip.color);
    lv_obj_set_style_bg_color(label, chip.color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_20, 0);
    lv_obj_set_style_radius(label, 4, 0);
    lv_obj_set_style_pad_hor(label, 5, 0);
    lv_obj_set_style_pad_ver(label, 1, 0);
    return label;
}

lv_obj_t* CreateActionButton(lv_obj_t* parent, const char* icon, const char* text, bool primary,
                             bool enabled) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_height(button, 34);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, primary ? rodakos_theme_primary() : rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_translate_y(button, 1, LV_STATE_PRESSED);
    lv_obj_set_layout(button, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(button, 4, 0);
    const lv_color_t foreground = primary ? rodakos_theme_on_primary() : rodakos_theme_text_primary();
    if (icon != nullptr) CreateText(button, icon, PhoneIconFont(), foreground);
    CreateText(button, text, &phone_font_14, foreground);
    if (!enabled) {
        lv_obj_add_state(button, LV_STATE_DISABLED);
        lv_obj_set_style_opa(button, LV_OPA_50, LV_STATE_DISABLED);
    }
    return button;
}

lv_obj_t* CreateFooter(lv_obj_t* parent) {
    lv_obj_t* footer = CreatePlain(parent);
    lv_obj_set_size(footer, LV_PCT(100), kFooterHeight);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(footer, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_color(footer, rodakos_theme_divider(), 0);
    lv_obj_set_style_pad_hor(footer, 10, 0);
    lv_obj_set_style_pad_ver(footer, 8, 0);
    lv_obj_set_layout(footer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(footer, 8, 0);
    return footer;
}

std::string* NewId(const std::string& id) { return new std::string(id); }

void DeleteIdOnDelete(lv_event_t* event) {
    delete static_cast<std::string*>(lv_event_get_user_data(event));
}

// Stores the notice id on the object; the string is freed with the object.
void AttachId(lv_obj_t* object, const std::string& id) {
    auto* copy = NewId(id);
    lv_obj_set_user_data(object, copy);
    lv_obj_add_event_cb(object, DeleteIdOnDelete, LV_EVENT_DELETE, copy);
}

const std::string* IdOf(lv_obj_t* object) {
    return static_cast<const std::string*>(lv_obj_get_user_data(object));
}

}  // namespace

bool MessagesApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    inbox_ = context.services().inbox();
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return false;
    CreateUi();
    if (inbox_ != nullptr) inbox_->RequestSync();
    ESP_LOGI(TAG, "Message center created");
    return true;
}

void MessagesApp::OnResume() {
    if (ui_ == nullptr) return;
    PhoneUiLock lock(*ui_);
    if (lock.locked() && root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        Refresh(true);
    }
}

void MessagesApp::OnPause() {
    if (ui_ == nullptr) return;
    PhoneUiLock lock(*ui_);
    if (lock.locked() && root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void MessagesApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) DestroyUi();
    }
    context_ = nullptr;
    ui_ = nullptr;
    inbox_ = nullptr;
}

bool MessagesApp::OnThemeChanged(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return false;
    DestroyUi();
    CreateUi();
    return true;
}

bool MessagesApp::OnBackRequested() {
    if (!selected_id_.empty()) {
        CloseDetail();
        return true;
    }
    NavigateHome();
    return true;
}

void MessagesApp::NavigateHome() {
    if (context_ == nullptr) return;
    lv_async_call_cancel(DeferReturnHome, context_);
    lv_async_call(DeferReturnHome, context_);
}

void MessagesApp::CreateUi() {
    root_ = CreatePlain(ui_->screen());
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);

    CreateAppHeader(root_, "消息中心", [](lv_event_t* event) {
        static_cast<MessagesApp*>(lv_event_get_user_data(event))->OnBackRequested();
    }, [](lv_event_t* event) {
        auto* self = static_cast<MessagesApp*>(lv_event_get_user_data(event));
        if (self->selected_id_.empty() && self->inbox_ != nullptr) {
            self->inbox_->RequestSync();
        } else {
            self->NavigateHome();
        }
    }, this, &title_label_, ICON_REFRESH);

    content_ = CreatePlain(root_);
    lv_obj_set_pos(content_, 0, kContentTop);
    lv_obj_set_size(content_, LV_PCT(100), ui_->height() - kContentTop);

    shown_revision_ = UINT32_MAX;
    Refresh(true);
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, kRefreshPeriodMs, this);
}

void MessagesApp::DestroyUi() {
    if (refresh_timer_ != nullptr) {
        lv_timer_delete(refresh_timer_);
        refresh_timer_ = nullptr;
    }
    if (context_ != nullptr) lv_async_call_cancel(DeferReturnHome, context_);
    if (root_ != nullptr && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = nullptr;
    title_label_ = nullptr;
    content_ = nullptr;
}

// Computed, not read back: a fresh container has no layout yet and reports height 0.
lv_coord_t MessagesApp::ContentHeight() const {
    return ui_ != nullptr ? ui_->height() - kContentTop : 0;
}

void MessagesApp::RefreshTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<MessagesApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) self->Refresh(false);
}

void MessagesApp::Refresh(bool force) {
    if (content_ == nullptr || context_ == nullptr) return;
    const bool share = context_->services().IsScreenShareActive();
    const uint32_t revision = inbox_ != nullptr ? inbox_->view_revision() : 0;
    if (!force && revision == shown_revision_ && share == shown_share_ &&
        snooze_menu_open_ == shown_snooze_menu_) {
        return;
    }
    // A share captures the composed screen: drop the text before the next frame is drawn.
    if (share) {
        shown_share_ = true;
        shown_revision_ = revision;
        shown_snooze_menu_ = snooze_menu_open_;
        view_ = inbox_ != nullptr ? inbox_->GetView() : rodakos::InboxView{};
        BuildPrivacyCover();
        UpdateHeader();
        return;
    }
    shown_share_ = false;
    shown_revision_ = revision;
    shown_snooze_menu_ = snooze_menu_open_;
    view_ = inbox_ != nullptr ? inbox_->GetView() : rodakos::InboxView{};
    UpdateHeader();
    if (inbox_ == nullptr) {
        BuildStatusPage("消息中心不可用", "");
        return;
    }
    if (!view_.bound) {
        BuildStatusPage("设备未绑定", "请前往设置重新绑定，解绑时已清空全部消息缓存。");
        return;
    }
    if (!selected_id_.empty()) {
        for (const auto& item : view_.items) {
            if (item.notice_id == selected_id_) {
                BuildDetail(item);
                return;
            }
        }
        // The record left the server list (another device deleted it).
        selected_id_.clear();
        snooze_menu_open_ = false;
    }
    BuildList();
}

void MessagesApp::UpdateHeader() {
    if (title_label_ == nullptr) return;
    if (!selected_id_.empty() && !shown_share_) {
        const rodakos::InboxItem* item = nullptr;
        for (const auto& candidate : view_.items) {
            if (candidate.notice_id == selected_id_) item = &candidate;
        }
        lv_label_set_text(title_label_, item != nullptr && item->kind == rodakos::InboxKind::kTodo
                                            ? "待办详情" : "通知详情");
        return;
    }
    char title[48] = {};
    if (view_.unread > 0) {
        std::snprintf(title, sizeof(title), "消息中心 (%u)",
                      static_cast<unsigned>(view_.unread > 99 ? 99 : view_.unread));
    } else {
        std::snprintf(title, sizeof(title), "%s", "消息中心");
    }
    lv_label_set_text(title_label_, title);
}

void MessagesApp::ClearContent() {
    if (content_ != nullptr) lv_obj_clean(content_);
}

void MessagesApp::BuildPrivacyCover() {
    ClearContent();
    lv_obj_t* box = CreatePlain(content_);
    lv_obj_set_size(box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 6, 0);
    CreateText(box, ICON_LOCK, PhoneIconFont(), rodakos_theme_warning());
    CreateText(box, "屏幕分享中，正文已遮蔽", &phone_font_14, rodakos_theme_text_primary());
    char counts[64] = {};
    std::snprintf(counts, sizeof(counts), "%u 条未读 · %u 项待办",
                  static_cast<unsigned>(view_.unread), static_cast<unsigned>(view_.open_todos));
    CreateText(box, counts, &phone_font_12, rodakos_theme_text_secondary());
}

void MessagesApp::BuildStatusPage(const char* title, const char* message) {
    ClearContent();
    lv_obj_t* box = CreatePlain(content_);
    lv_obj_set_size(box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_hor(box, 24, 0);
    lv_obj_set_layout(box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 6, 0);
    CreateText(box, title, &phone_font_14, rodakos_theme_text_primary());
    if (message != nullptr && message[0] != '\0') {
        lv_obj_t* label = CreateText(box, message, &phone_font_12, rodakos_theme_text_secondary());
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void MessagesApp::BuildList() {
    ClearContent();
    const lv_coord_t width = ui_->width();

    lv_obj_t* bar = CreatePlain(content_);
    lv_obj_set_size(bar, width, kFilterBarHeight);
    lv_obj_set_style_pad_hor(bar, 8, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, rodakos_theme_divider(), 0);

    uint32_t todo_count = 0;
    uint32_t notice_count = 0;
    for (const auto& item : view_.items) {
        if (item.kind == rodakos::InboxKind::kTodo) ++todo_count;
        else ++notice_count;
    }
    struct FilterChoice {
        Filter filter;
        const char* name;
        uint32_t count;
    };
    const FilterChoice choices[] = {
        {Filter::kAll, "全部", static_cast<uint32_t>(view_.items.size())},
        {Filter::kTodo, "待办", todo_count},
        {Filter::kNotice, "通知", notice_count},
    };
    lv_coord_t x = 0;
    for (const auto& choice : choices) {
        char text[32] = {};
        std::snprintf(text, sizeof(text), "%s %u", choice.name, static_cast<unsigned>(choice.count));
        lv_obj_t* button = lv_button_create(bar);
        lv_obj_remove_style_all(button);
        lv_obj_set_size(button, 62, 20);
        lv_obj_set_pos(button, x, 4);
        lv_obj_set_ext_click_area(button, 3);
        x += 66;
        const bool active = filter_ == choice.filter;
        lv_obj_set_style_radius(button, 6, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(button, active ? rodakos_theme_primary() : rodakos_theme_bg_tertiary(), 0);
        lv_obj_t* label = CreateText(button, text, &phone_font_12,
                                     active ? rodakos_theme_on_primary() : rodakos_theme_text_secondary());
        lv_obj_center(label);
        lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<uintptr_t>(choice.filter)));
        lv_obj_add_event_cb(button, [](lv_event_t* event) {
            auto* self = static_cast<MessagesApp*>(lv_event_get_user_data(event));
            auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
            self->filter_ = static_cast<Filter>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target)));
            self->Refresh(true);
        }, LV_EVENT_CLICKED, this);
    }

    char sync_text[64] = {};
    lv_color_t sync_color = rodakos_theme_text_tertiary();
    const std::string last = FormatTime(view_.last_sync_at, true);
    if (view_.syncing) {
        std::snprintf(sync_text, sizeof(sync_text), "%s", "同步中…");
        sync_color = rodakos_theme_primary();
    } else if (!view_.synced) {
        std::snprintf(sync_text, sizeof(sync_text), "%s", "尚未同步");
        sync_color = rodakos_theme_warning();
    } else if (!view_.online) {
        std::snprintf(sync_text, sizeof(sync_text), "离线 · %s", last.c_str());
        sync_color = rodakos_theme_warning();
    } else {
        std::snprintf(sync_text, sizeof(sync_text), "已同步 %s", last.c_str());
    }
    lv_obj_t* sync_label = CreateText(bar, sync_text, &phone_font_12, sync_color);
    lv_obj_align(sync_label, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* list = lv_obj_create(content_);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 0, kFilterBarHeight);
    lv_obj_set_size(list, width, ContentHeight() - kFilterBarHeight);
    lv_obj_set_style_pad_all(list, 8, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);

    size_t shown = 0;
    for (const auto& item : view_.items) {
        if ((filter_ == Filter::kTodo && item.kind != rodakos::InboxKind::kTodo) ||
            (filter_ == Filter::kNotice && item.kind != rodakos::InboxKind::kNotice)) {
            continue;
        }
        ++shown;
        lv_obj_t* row = lv_button_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 46);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_bg_color(row, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, item.op_state == rodakos::InboxOpState::kPendingReconcile
                                               ? rodakos_theme_warning() : rodakos_theme_divider(), 0);
        lv_obj_set_style_pad_hor(row, 9, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        AttachId(row, item.notice_id);
        lv_obj_add_event_cb(row, [](lv_event_t* event) {
            auto* self = static_cast<MessagesApp*>(lv_event_get_user_data(event));
            const std::string* id = IdOf(static_cast<lv_obj_t*>(lv_event_get_current_target(event)));
            if (id != nullptr) self->OpenItem(*id);
        }, LV_EVENT_CLICKED, this);

        lv_coord_t title_x = 0;
        if (item.unread) {
            lv_obj_t* dot = CreatePlain(row);
            lv_obj_set_size(dot, 6, 6);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, rodakos_theme_error(), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 0, 10);
            title_x = 10;
        }
        const bool done = item.kind == rodakos::InboxKind::kTodo &&
                          item.todo_state == rodakos::InboxTodoState::kDone;
        lv_obj_t* title = CreateText(row, item.title.c_str(), &phone_font_14,
                                     done ? rodakos_theme_text_tertiary() : rodakos_theme_text_primary());
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_width(title, 230 - title_x);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, title_x, 3);
        if (done) lv_obj_set_style_text_decor(title, LV_TEXT_DECOR_STRIKETHROUGH, 0);

        lv_obj_t* time = CreateText(row, FormatTime(item.created_at, true).c_str(), &phone_font_12,
                                    rodakos_theme_text_tertiary());
        lv_obj_align(time, LV_ALIGN_TOP_RIGHT, 0, 5);

        lv_obj_t* preview = CreateText(row, item.summary.empty() ? item.body.c_str() : item.summary.c_str(),
                                       &phone_font_12, rodakos_theme_text_secondary());
        lv_label_set_long_mode(preview, LV_LABEL_LONG_DOT);
        lv_obj_set_width(preview, 200);
        lv_obj_align(preview, LV_ALIGN_BOTTOM_LEFT, 0, -4);
        lv_obj_t* chip = CreateChip(row, ItemChip(item));
        lv_obj_align(chip, LV_ALIGN_BOTTOM_RIGHT, 0, -4);
    }
    if (shown == 0) {
        lv_obj_t* empty = CreateText(list, view_.synced ? "暂无记录" : "尚未同步，联网后自动更新",
                                     &phone_font_12, rodakos_theme_text_tertiary());
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(empty, 40, 0);
    }
}

bool MessagesApp::CanWrite(const rodakos::InboxItem& item) const {
    return view_.online && item.kind == rodakos::InboxKind::kTodo &&
           item.todo_state != rodakos::InboxTodoState::kDone &&
           item.op_state == rodakos::InboxOpState::kNone;
}

void MessagesApp::BuildDetail(const rodakos::InboxItem& item) {
    ClearContent();
    const bool todo = item.kind == rodakos::InboxKind::kTodo;
    const bool footer = todo && item.todo_state != rodakos::InboxTodoState::kDone;
    const lv_coord_t body_height = ContentHeight() - (footer ? kFooterHeight : 0);

    lv_obj_t* body = lv_obj_create(content_);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, LV_PCT(100), body_height);
    lv_obj_set_style_pad_hor(body, 12, 0);
    lv_obj_set_style_pad_ver(body, 8, 0);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_set_layout(body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    lv_obj_t* title = CreateText(body, item.title.c_str(), &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);

    std::string meta = FormatTime(item.created_at, true);
    if (todo && item.due_at > 0) {
        meta += meta.empty() ? "" : " · ";
        meta += "截止 " + FormatTime(item.due_at, true);
    }
    if (todo && item.todo_state == rodakos::InboxTodoState::kSnoozed && item.snoozed_until > 0) {
        meta += meta.empty() ? "" : " · ";
        meta += "推迟到 " + FormatTime(item.snoozed_until, false);
    }
    if (!meta.empty()) CreateText(body, meta.c_str(), &phone_font_12, rodakos_theme_text_tertiary());

    const char* note = nullptr;
    lv_color_t note_color = rodakos_theme_warning();
    if (item.op_state == rodakos::InboxOpState::kPendingReconcile) {
        note = "上次提交没有收到确认，点重试会核对服务器上的结果，不会重复执行。";
    } else if (todo && !view_.online && footer) {
        note = "当前离线：仅可查看缓存，联网后可提交完成或推迟。";
    } else if (todo && item.todo_state == rodakos::InboxTodoState::kDone) {
        note = "此待办已完成。";
        note_color = rodakos_theme_success();
    }
    if (note != nullptr) {
        lv_obj_t* box = CreateText(body, note, &phone_font_12, note_color);
        lv_obj_set_width(box, LV_PCT(100));
        lv_label_set_long_mode(box, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_bg_color(box, note_color, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_10, 0);
        lv_obj_set_style_radius(box, 6, 0);
        lv_obj_set_style_pad_all(box, 6, 0);
    }
    const std::string& text = item.body.empty() ? item.summary : item.body;
    lv_obj_t* body_label = CreateText(body, text.c_str(), &phone_font_14, rodakos_theme_text_secondary());
    lv_obj_set_width(body_label, LV_PCT(100));
    lv_label_set_long_mode(body_label, LV_LABEL_LONG_WRAP);

    if (!footer) return;
    lv_obj_t* bar = CreateFooter(content_);
    if (item.op_state == rodakos::InboxOpState::kPendingReconcile) {
        lv_obj_t* retry = CreateActionButton(bar, ICON_REFRESH, "重试核对", true, view_.online);
        lv_obj_add_event_cb(retry, [](lv_event_t* event) {
            auto* self = static_cast<MessagesApp*>(lv_event_get_user_data(event));
            if (self->inbox_ != nullptr && !self->inbox_->Retry(self->selected_id_)) {
                self->ui_->ShowToastUnlocked("暂时无法重试");
            }
        }, LV_EVENT_CLICKED, this);
        return;
    }
    if (item.op_state == rodakos::InboxOpState::kSubmitting) {
        CreateActionButton(bar, nullptr, "同步中…", false, false);
        return;
    }
    if (snooze_menu_open_) {
        for (uint32_t minutes : rodakos::kInboxSnoozeChoicesMinutes) {
            char text[32] = {};
            if (minutes == 60) std::snprintf(text, sizeof(text), "%s", "+1小时");
            else std::snprintf(text, sizeof(text), "+%u分钟", static_cast<unsigned>(minutes));
            lv_obj_t* choice = CreateActionButton(bar, nullptr, text, false, CanWrite(item));
            lv_obj_set_user_data(choice, reinterpret_cast<void*>(static_cast<uintptr_t>(minutes)));
            lv_obj_add_event_cb(choice, [](lv_event_t* event) {
                auto* self = static_cast<MessagesApp*>(lv_event_get_user_data(event));
                auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
                const auto minutes = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target)));
                self->snooze_menu_open_ = false;
                if (self->inbox_ == nullptr || !self->inbox_->Snooze(self->selected_id_, minutes)) {
                    self->ui_->ShowToastUnlocked("暂时无法推迟");
                }
                self->Refresh(true);
            }, LV_EVENT_CLICKED, this);
        }
        lv_obj_t* cancel = CreateActionButton(bar, ICON_CLOSE, "取消", false, true);
        lv_obj_set_flex_grow(cancel, 0);
        lv_obj_set_width(cancel, 64);
        lv_obj_add_event_cb(cancel, [](lv_event_t* event) {
            auto* self = static_cast<MessagesApp*>(lv_event_get_user_data(event));
            self->snooze_menu_open_ = false;
            self->Refresh(true);
        }, LV_EVENT_CLICKED, this);
        return;
    }
    const bool writable = CanWrite(item);
    lv_obj_t* complete = CreateActionButton(bar, ICON_CHECK, "完成", true, writable);
    lv_obj_add_event_cb(complete, [](lv_event_t* event) {
        auto* self = static_cast<MessagesApp*>(lv_event_get_user_data(event));
        if (self->inbox_ == nullptr || !self->inbox_->Complete(self->selected_id_)) {
            self->ui_->ShowToastUnlocked("暂时无法提交");
        }
        self->Refresh(true);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* snooze = CreateActionButton(bar, ICON_ALARM, "稍后提醒", false, writable);
    lv_obj_add_event_cb(snooze, [](lv_event_t* event) {
        auto* self = static_cast<MessagesApp*>(lv_event_get_user_data(event));
        self->snooze_menu_open_ = true;
        self->Refresh(true);
    }, LV_EVENT_CLICKED, this);
}

void MessagesApp::OpenItem(const std::string& notice_id) {
    selected_id_ = notice_id;
    snooze_menu_open_ = false;
    if (inbox_ != nullptr) inbox_->MarkRead(notice_id);
    Refresh(true);
}

void MessagesApp::CloseDetail() {
    selected_id_.clear();
    snooze_menu_open_ = false;
    Refresh(true);
}

void RegisterMessagesApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "messages",
        .title = "Messages",
        .icon = ICON_NOTIFICATIONS,
        .category = PhoneAppCategory::kSystem,
        .icon_tint = PhoneAppIconTint::kAlert,
        .capabilities = PhoneCapability::kNetwork,
        .show_on_home = true,
        .aliases = {"inbox", "notifications", "消息中心"},
        .create = []() { return std::make_unique<MessagesApp>(); },
    });
}

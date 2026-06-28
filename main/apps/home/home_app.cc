#include "apps/home/home_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/time_service.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/rodakos_layout.h"
#include "phone_ui/rodakos_theme.h"
#include "settings.h"

#include <esp_log.h>

#include <cstdio>
#include <ctime>
#include <string>

namespace {
constexpr const char* TAG = "HomeApp";

// Grid layout (4 cols x 3 rows)
// 可用 body 高度：240 - 28 - 20 = 192px
// 需要：3行 + 2个间距
// 设 cell_height = 60, gap = 6
// 3×60 + 2×6 = 192px ✓ 刚好！
constexpr int kGridCols = 4;
constexpr int kGridRows = 3;
constexpr int kMaxApps = kGridCols * kGridRows;

constexpr lv_coord_t kCellWidth = 64;
constexpr lv_coord_t kCellHeight = 60;  // 60px
constexpr lv_coord_t kGapX = 12;
constexpr lv_coord_t kGapY = 6;  // 减小垂直间距到 6px
constexpr lv_coord_t kIconSize = 46;  // 图标 46px

// 移除硬编码的颜色数组，改用主题系统
// constexpr uint32_t kIconColors[] = { ... };

struct AppButtonPayload {
    PhoneAppContext* context = nullptr;
    std::string app_id;
};

struct DeferredLaunchPayload {
    PhoneAppContext* context = nullptr;
    std::string app_id;
};

void RunDeferredLaunch(void* data) {
    auto* payload = static_cast<DeferredLaunchPayload*>(data);
    if (payload != nullptr && payload->context != nullptr) {
        lv_indev_reset(nullptr, nullptr);
        ESP_LOGI(TAG, "Launching app from Home: %s", payload->app_id.c_str());
        payload->context->navigation().Launch(payload->app_id);
    }
    delete payload;
}

void AppButtonEvent(lv_event_t* e) {
    auto* payload = static_cast<AppButtonPayload*>(lv_event_get_user_data(e));
    if (payload != nullptr && payload->context != nullptr) {
        if (auto* indev = lv_indev_active(); indev != nullptr) {
            lv_indev_wait_release(indev);
        }
        ESP_LOGI(TAG, "Home app button clicked: %s", payload->app_id.c_str());
        lv_async_call(RunDeferredLaunch, new DeferredLaunchPayload{
            .context = payload->context,
            .app_id = payload->app_id,
        });
    }
}

void AppButtonDeleteEvent(lv_event_t* e) {
    auto* payload = static_cast<AppButtonPayload*>(lv_event_get_user_data(e));
    delete payload;
}

void ClockTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<HomeApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->UpdateClock();
    }
}

}  // namespace

HomeApp::~HomeApp() {
    OnDestroy();
}

bool HomeApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();

    ESP_LOGI(TAG, "HomeApp::OnCreate starting");

    // Use longer timeout during WiFi initialization
    PhoneUiLock lock(*ui_, 5000);  // 5 seconds timeout
    if (!lock.locked()) {
        ESP_LOGE(TAG, "Failed to acquire UI lock");
        return false;
    }

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
    ESP_LOGI(TAG, "Layout containers created");

    // ===== HEADER 区域 =====
    // Clock (居中)
    clock_label_ = lv_label_create(header);
    lv_label_set_text(clock_label_, "00:00");
    lv_obj_set_style_text_color(clock_label_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(clock_label_, &phone_font_18, 0);
    lv_obj_center(clock_label_);

    // Battery (右对齐)
    battery_label_ = lv_label_create(header);
    lv_label_set_text(battery_label_, "100%");
    lv_obj_set_style_text_color(battery_label_, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(battery_label_, &phone_font_12, 0);
    lv_obj_align(battery_label_, LV_ALIGN_RIGHT_MID, -rodakos_layout_padding_medium(), 0);

    // WiFi (左对齐)
    wifi_label_ = lv_label_create(header);
    lv_label_set_text(wifi_label_, "WiFi");
    lv_obj_set_style_text_color(wifi_label_, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(wifi_label_, &phone_font_12, 0);
    lv_obj_align(wifi_label_, LV_ALIGN_LEFT_MID, rodakos_layout_padding_medium(), 0);

    // ===== BODY 区域 =====
    // 创建网格容器（自动居中）
    grid_ = rodakos_layout_create_grid(body, kGridCols, kGridRows,
                                        kCellWidth, kCellHeight,
                                        kGapX, kGapY);

    if (grid_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create grid container");
        return false;
    }
    ESP_LOGI(TAG, "Grid container created");

    // 创建应用图标
    const auto apps = context.registry().ListHomeApps();
    ESP_LOGI(TAG, "Found %zu apps to display", apps.size());
    for (size_t i = 0; i < apps.size() && i < kMaxApps; ++i) {
        const PhoneAppDescriptor& app = *apps[i];

        // 按钮容器
        auto* btn = lv_btn_create(grid_);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, kCellWidth, kCellHeight);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        // 自动放置在网格中（无需手动计算位置）
        rodakos_layout_grid_place(grid_, btn, i, kGridCols,
                                   kCellWidth, kCellHeight, kGapX, kGapY);

        auto* payload = new AppButtonPayload{.context = &context, .app_id = app.id};
        lv_obj_add_event_cb(btn, AppButtonEvent, LV_EVENT_CLICKED, payload);
        lv_obj_add_event_cb(btn, AppButtonDeleteEvent, LV_EVENT_DELETE, payload);

        // 图标圆形背景
        auto* icon_bg = lv_obj_create(btn);
        lv_obj_remove_style_all(icon_bg);
        lv_obj_set_size(icon_bg, kIconSize, kIconSize);
        lv_obj_align(icon_bg, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_radius(icon_bg, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(icon_bg, rodakos_theme_icon_color(i), 0);  // 使用主题图标色
        lv_obj_set_style_bg_opa(icon_bg, LV_OPA_COVER, 0);
        lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_CLICKABLE);

        // 图标文字
        auto* icon_label = lv_label_create(icon_bg);
        lv_label_set_text(icon_label, app.icon.c_str());
        lv_obj_set_style_text_color(icon_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(icon_label, PhoneIconFontLarge(), 0);
        lv_obj_center(icon_label);

        // 应用名称
        auto* name_label = lv_label_create(btn);
        lv_label_set_text(name_label, app.title.c_str());
        lv_obj_set_width(name_label, kCellWidth);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(name_label, rodakos_theme_text_secondary(), 0);  // 使用主题次级文字色
        lv_obj_set_style_text_font(name_label, &phone_font_12, 0);
        lv_obj_align(name_label, LV_ALIGN_BOTTOM_MID, 0, 0);
    }

    // ===== FOOTER 区域 =====
    // 使用 Flex 布局自动排列页面指示器（无需手动计算位置）
    auto* indicator = rodakos_layout_create_flex(footer, LV_FLEX_FLOW_ROW,
                                                  rodakos_layout_padding_medium());
    lv_obj_center(indicator);

    // 激活的页面点
    auto* dot_active = lv_obj_create(indicator);
    lv_obj_remove_style_all(dot_active);
    lv_obj_set_size(dot_active, 18, 6);
    lv_obj_set_style_radius(dot_active, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_active, rodakos_theme_secondary(), 0);  // 使用主题辅助色
    lv_obj_set_style_bg_opa(dot_active, LV_OPA_COVER, 0);
    lv_obj_clear_flag(dot_active, LV_OBJ_FLAG_CLICKABLE);

    // 非激活的页面点
    for (int i = 0; i < 2; ++i) {
        auto* dot = lv_obj_create(indicator);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, rodakos_theme_text_tertiary(), 0);  // 使用主题三级文字色
        lv_obj_set_style_bg_opa(dot, static_cast<lv_opa_t>(128), 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    }

    UpdateClock();
    clock_timer_ = lv_timer_create(ClockTimerCallback, 1000, this);

    ESP_LOGI(TAG, "Phone desktop ready with %u apps", static_cast<unsigned>(apps.size()));
    return true;
}

void HomeApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            if (clock_timer_ != nullptr) {
                lv_timer_delete(clock_timer_);
                clock_timer_ = nullptr;
            }
            if (root_ != nullptr && lv_obj_is_valid(root_)) {
                lv_obj_delete(root_);
            }
        }
    }
    root_ = nullptr;
    grid_ = nullptr;
    clock_label_ = nullptr;
    battery_label_ = nullptr;
    wifi_label_ = nullptr;
    context_ = nullptr;
    ui_ = nullptr;
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
}

void RegisterHomeApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "home",
        .title = "Home",
        .icon = FONT_AWESOME_HOUSE,
        .category = PhoneAppCategory::kSystem,
        .launch_mode = PhoneAppLaunchMode::kHome,
        .capabilities = PhoneCapability::kNone,
        .show_on_home = false,
        .aliases = {"desktop", "launcher"},
        .create = []() { return std::make_unique<HomeApp>(); },
    });
}

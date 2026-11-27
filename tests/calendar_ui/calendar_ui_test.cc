#include "test_framework.h"

#include <cstdlib>
#include <fstream>
#include <string>

#define private public
#include "apps/calendar/calendar_app.h"
#undef private
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/time_service.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "settings.h"

#include <src/others/test/lv_test.h>

namespace {

// 2026-11-26 10:00 UTC; the test binary runs with TZ=UTC.
constexpr std::time_t kToday = 1795687200;

void Pump(uint32_t ms = 20) {
    lv_test_wait(ms);
    lv_obj_update_layout(lv_screen_active());
}

void Screenshot(const char* name) {
    Pump(300);
    const auto* frame = lv_display_get_buf_active(lv_display_get_default());
    std::ofstream out(name, std::ios::binary);
    out << "P6\n320 240\n255\n";
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
            const auto* p = frame->data + y * frame->header.stride + x * 4;
            const char rgb[] = {static_cast<char>(p[2]), static_cast<char>(p[1]), static_cast<char>(p[0])};
            out.write(rgb, 3);
        }
    }
}

std::string Text(lv_obj_t* label) {
    RODAK_CHECK(label != nullptr);
    return label != nullptr ? lv_label_get_text(label) : "";
}

bool Visible(lv_obj_t* obj) {
    return obj != nullptr && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void Click(lv_obj_t* obj) {
    RODAK_CHECK(obj != nullptr);
    lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
    Pump();
}

struct Fixture {
    PhoneUi ui{320, 240};
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context{ui, navigation, registry, services, settings};
    CalendarApp app;
    bool created = false;

    explicit Fixture(const char* theme = "dark") {
        setenv("TZ", "UTC", 1);
        tzset();
        rodakos_theme_init_from_name(theme);
        ui.SyncThemeName(theme);
        calendar_test::time_valid = true;
        calendar_test::now = kToday;
        calendar_test::sync_status = TimeSyncStatus::kReset;
        calendar_test::sync_starts = true;
        calendar_test::sync_calls = 0;
    }
    ~Fixture() {
        if (created) app.OnDestroy();
        lv_test_wait(2000);
        lv_obj_clean(lv_screen_active());
        rodakos_theme_init(RODAKOS_THEME_DARK);
    }
    void Create() {
        RODAK_CHECK(app.OnCreate(context));
        created = true;
        Pump();
    }
    CalendarApp::Cell& CellFor(int day) {
        for (auto& cell : app.cells_) {
            if (!cell.outside && cell.date == day && Visible(cell.button)) return cell;
        }
        RODAK_CHECK(false);
        return app.cells_[0];
    }
};

}  // namespace

RODAK_TEST("month view puts the whole month on one screen with lunar text in every cell") {
    Fixture f;
    f.Create();
    RODAK_CHECK(f.app.view_ == CalendarApp::View::kMonth);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("2026 年 11 月"));
    RODAK_CHECK_EQ(Text(f.app.weekday_labels_[0]), std::string("一"));
    RODAK_CHECK_EQ(Text(f.app.weekday_labels_[6]), std::string("日"));

    // November 2026 starts on a Sunday, so it spans six week rows of 30 px.
    int visible = 0;
    for (const auto& cell : f.app.cells_) visible += Visible(cell.button) ? 1 : 0;
    RODAK_CHECK_EQ(visible, 42);
    lv_area_t area;
    lv_obj_get_coords(f.app.cells_[41].button, &area);
    RODAK_CHECK(area.y2 < 240);
    RODAK_CHECK_EQ(lv_area_get_height(&area), 28);
    RODAK_CHECK_EQ(lv_area_get_width(&area), 42);

    auto& today = f.CellFor(26);
    RODAK_CHECK_EQ(Text(today.lunar), std::string("十八"));
    RODAK_CHECK_EQ(lv_obj_get_style_bg_opa(today.button, 0), LV_OPA_COVER);
    RODAK_CHECK_EQ(Text(f.CellFor(7).lunar), std::string("立冬"));
    RODAK_CHECK_EQ(Text(f.CellFor(22).lunar), std::string("小雪"));
    RODAK_CHECK_EQ(Text(f.CellFor(9).lunar), std::string("十月"));

    // The first cell is October 26 from the previous month, dimmed.
    RODAK_CHECK(f.app.cells_[0].outside);
    RODAK_CHECK_EQ(f.app.cells_[0].month, 10);
    RODAK_CHECK_EQ(Text(f.app.cells_[0].day), std::string("26"));
    Screenshot("calendar-month.ppm");
}

RODAK_TEST("lunar labels stay inside their 44x30 cell") {
    Fixture f;
    f.Create();
    for (const auto& cell : f.app.cells_) {
        if (!Visible(cell.button)) continue;
        lv_area_t box, day, lunar;
        lv_obj_get_coords(cell.button, &box);
        lv_obj_get_coords(cell.day, &day);
        lv_obj_get_coords(cell.lunar, &lunar);
        RODAK_CHECK(lunar.y2 <= box.y2);
        RODAK_CHECK(lunar.x1 >= box.x1 - 1 && lunar.x2 <= box.x2 + 1);
        RODAK_CHECK(day.y2 <= lunar.y1 + 2);
    }
}

RODAK_TEST("shorter months get taller rows and festivals show in the cells") {
    Fixture f;
    f.Create();
    for (int i = 0; i < 3; ++i) Click(f.app.next_button_);
    // February 2027 starts on a Monday and has 28 days: four rows of 44 px.
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("2027 年 2 月"));
    int visible = 0;
    for (const auto& cell : f.app.cells_) visible += Visible(cell.button) ? 1 : 0;
    RODAK_CHECK_EQ(visible, 28);
    lv_area_t area;
    lv_obj_get_coords(f.app.cells_[0].button, &area);
    RODAK_CHECK_EQ(lv_area_get_height(&area), 42);
    RODAK_CHECK_EQ(Text(f.CellFor(5).lunar), std::string("除夕"));
    RODAK_CHECK_EQ(Text(f.CellFor(6).lunar), std::string("春节"));
    RODAK_CHECK_EQ(Text(f.CellFor(20).lunar), std::string("元宵"));
    RODAK_CHECK_EQ(Text(f.CellFor(4).lunar), std::string("立春"));
    // Not the current month: 今天 turns into a primary button.
    RODAK_CHECK(lv_color_to_u32(lv_obj_get_style_bg_color(f.app.today_button_, 0)) ==
                lv_color_to_u32(rodakos_theme_primary()));
    Screenshot("calendar-february.ppm");
    // January 2027 starts on a Friday: five rows of 36 px.
    Click(f.app.previous_button_);
    visible = 0;
    for (const auto& cell : f.app.cells_) visible += Visible(cell.button) ? 1 : 0;
    RODAK_CHECK_EQ(visible, 35);
    lv_obj_get_coords(f.app.cells_[0].button, &area);
    RODAK_CHECK_EQ(lv_area_get_height(&area), 34);
    Click(f.app.today_button_);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("2026 年 11 月"));
}

RODAK_TEST("tapping a day opens its detail page; back returns to the month") {
    Fixture f;
    f.Create();
    Click(f.CellFor(26).button);
    RODAK_CHECK(f.app.view_ == CalendarApp::View::kDay);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("11 月 26 日 周四"));
    RODAK_CHECK_EQ(Text(f.app.day_number_label_), std::string("26"));
    RODAK_CHECK_EQ(Text(f.app.day_relative_label_), std::string("今天"));
    RODAK_CHECK_EQ(Text(f.app.day_lunar_label_), std::string("农历十月十八"));
    RODAK_CHECK_EQ(Text(f.app.day_detail_labels_[0]), std::string("丙午马年"));
    RODAK_CHECK_EQ(Text(f.app.day_detail_labels_[1]), std::string("第 48 周 · 第 330 天"));
    RODAK_CHECK_EQ(Text(f.app.day_detail_labels_[2]), std::string("距大雪 11 天"));
    RODAK_CHECK_FALSE(Visible(f.app.today_button_));
    Screenshot("calendar-day.ppm");

    Click(f.app.next_button_);
    RODAK_CHECK_EQ(Text(f.app.day_relative_label_), std::string("明天"));
    for (int i = 0; i < 5; ++i) Click(f.app.next_button_);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("12 月 2 日 周三"));
    RODAK_CHECK_EQ(Text(f.app.day_relative_label_), std::string("6 天后"));

    RODAK_CHECK(f.app.OnBackRequested());
    RODAK_CHECK(f.app.view_ == CalendarApp::View::kMonth);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("2026 年 12 月"));
    RODAK_CHECK(f.app.OnBackRequested());
    Pump();
    RODAK_CHECK_EQ(f.navigation.home_count, 1);
}

RODAK_TEST("a festival or solar term day shows it on the detail page") {
    Fixture f;
    f.Create();
    Click(f.CellFor(22).button);
    RODAK_CHECK_EQ(Text(f.app.day_detail_labels_[0]), std::string("丙午马年 · 小雪"));
    RODAK_CHECK_EQ(Text(f.app.day_relative_label_), std::string("4 天前"));
}

RODAK_TEST("a dimmed neighbour day switches the grid to its month") {
    Fixture f;
    f.Create();
    Click(f.app.cells_[41].button);
    RODAK_CHECK(f.app.view_ == CalendarApp::View::kMonth);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("2026 年 12 月"));
    RODAK_CHECK_EQ(f.app.selected_day_, 6);
}

RODAK_TEST("unsynced clock shows a sync page that starts SNTP and switches to the month") {
    Fixture f;
    calendar_test::time_valid = false;
    f.Create();
    RODAK_CHECK(f.app.view_ == CalendarApp::View::kUnsynced);
    RODAK_CHECK_FALSE(Visible(f.app.previous_button_));
    RODAK_CHECK_FALSE(Visible(f.app.today_button_));
    RODAK_CHECK_EQ(Text(f.app.sync_button_label_), std::string("立即校时"));
    Screenshot("calendar-unsynced.ppm");

    Click(f.app.sync_button_);
    RODAK_CHECK_EQ(calendar_test::sync_calls, 1);
    RODAK_CHECK_EQ(Text(f.app.sync_button_label_), std::string("正在校时…"));
    RODAK_CHECK(lv_obj_has_state(f.app.sync_button_, LV_STATE_DISABLED));

    calendar_test::time_valid = true;
    calendar_test::sync_status = TimeSyncStatus::kCompleted;
    Pump(1100);
    RODAK_CHECK(f.app.view_ == CalendarApp::View::kMonth);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("2026 年 11 月"));
}

RODAK_TEST("a sync that cannot start says so and keeps the button usable") {
    Fixture f;
    calendar_test::time_valid = false;
    calendar_test::sync_starts = false;
    f.Create();
    Click(f.app.sync_button_);
    RODAK_CHECK_EQ(Text(f.app.sync_button_label_), std::string("立即校时"));
    RODAK_CHECK_FALSE(lv_obj_has_state(f.app.sync_button_, LV_STATE_DISABLED));
}

RODAK_TEST("crossing midnight moves the today marker") {
    Fixture f;
    f.Create();
    calendar_test::now = kToday + 24 * 3600;
    f.app.RefreshToday();
    Pump();
    RODAK_CHECK_EQ(lv_obj_get_style_border_width(f.CellFor(27).button, 0), 2);
}

RODAK_TEST("light theme keeps selected and weekend colours readable") {
    Fixture f("light");
    f.Create();
    const auto& cell = f.CellFor(26);
    RODAK_CHECK(lv_color_to_u32(lv_obj_get_style_text_color(cell.day, 0)) ==
                lv_color_to_u32(rodakos_theme_on_primary()));
    Screenshot("calendar-light.ppm");
}

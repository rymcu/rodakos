#include "test_framework.h"

#include "phone_os/lunar_calendar.h"

#include <fstream>
#include <sstream>
#include <string>

using namespace rodakos;

namespace {

LunarDate Lunar(int y, int m, int d) {
    LunarDate date;
    RODAK_CHECK(SolarToLunar(y, m, d, date));
    return date;
}

void NextDay(int& y, int& m, int& d) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    const int last = m == 2 && leap ? 29 : kDays[m - 1];
    if (++d > last) {
        d = 1;
        if (++m > 12) { m = 1; ++y; }
    }
}

}  // namespace

RODAK_TEST("every lunar month 1900-2100 starts on the reference solar date") {
    std::ifstream in(LUNAR_REFERENCE);
    RODAK_CHECK(in.good());
    std::string line;
    int months = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] != 'M') continue;
        std::istringstream row(line.substr(2));
        int y, m, d, ly, lm, leap;
        row >> y >> m >> d >> ly >> lm >> leap;
        if (y > 2100 || (y == 1900 && m == 1)) continue;
        const LunarDate date = Lunar(y, m, d);
        RODAK_CHECK_EQ(date.year, ly);
        RODAK_CHECK_EQ(date.month, lm);
        RODAK_CHECK_EQ(date.day, 1);
        RODAK_CHECK_EQ(date.leap, leap == 1);
        ++months;
    }
    RODAK_CHECK(months > 2480);
}

RODAK_TEST("every solar term 1900-2100 lands on the reference day") {
    std::ifstream in(LUNAR_REFERENCE);
    std::string line;
    int years = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] != 'T') continue;
        std::istringstream row(line.substr(2));
        int y;
        row >> y;
        for (int i = 0; i < 24; ++i) {
            int day;
            row >> day;
            const int month = i / 2 + 1;
            if (y == 1900 && month == 1) continue;
            RODAK_CHECK_EQ(Lunar(y, month, day).solar_term, i);
        }
        ++years;
    }
    RODAK_CHECK_EQ(years, 201);
}

RODAK_TEST("consecutive days advance by one lunar day across the whole range") {
    int y = 1900, m = 2, d = 1;
    LunarDate previous = Lunar(y, m, d);
    int checked = 0;
    while (y <= 2100) {
        NextDay(y, m, d);
        if (y > 2100) break;
        const LunarDate today = Lunar(y, m, d);
        if (today.day == 1) {
            RODAK_CHECK_EQ(previous.day, LunarMonthDays(previous.year, previous.month, previous.leap));
        } else {
            RODAK_CHECK_EQ(today.day, previous.day + 1);
            RODAK_CHECK_EQ(today.month, previous.month);
            RODAK_CHECK_EQ(today.leap, previous.leap);
        }
        previous = today;
        ++checked;
    }
    RODAK_CHECK(checked > 73000);
}

RODAK_TEST("known dates, names, festivals and terms") {
    LunarDate date = Lunar(2026, 11, 26);
    RODAK_CHECK_EQ(date.year, 2026);
    RODAK_CHECK_EQ(date.month, 10);
    RODAK_CHECK_EQ(date.day, 18);
    RODAK_CHECK_EQ(std::string(LunarMonthName(date.month)), "十");
    RODAK_CHECK_EQ(std::string(LunarDayName(date.day)), "十八");
    char year_name[32];
    LunarYearName(date.year, year_name, sizeof(year_name));
    RODAK_CHECK_EQ(std::string(year_name), "丙午马");

    date = Lunar(2027, 2, 6);
    RODAK_CHECK_EQ(date.month, 1);
    RODAK_CHECK_EQ(date.day, 1);
    RODAK_CHECK_EQ(std::string(FestivalName(2027, 2, 6, date)), "春节");
    date = Lunar(2027, 2, 5);
    RODAK_CHECK_EQ(std::string(FestivalName(2027, 2, 5, date)), "除夕");
    date = Lunar(2025, 7, 25);
    RODAK_CHECK(date.leap);
    RODAK_CHECK_EQ(date.month, 6);
    RODAK_CHECK(FestivalName(2026, 10, 1, Lunar(2026, 10, 1)) != nullptr);
    RODAK_CHECK_EQ(std::string(FestivalName(2026, 9, 25, Lunar(2026, 9, 25))), "中秋");

    RODAK_CHECK_EQ(Lunar(2026, 11, 22).solar_term, 21);
    RODAK_CHECK_EQ(std::string(SolarTermName(21)), "小雪");
    int days = 0;
    RODAK_CHECK_EQ(NextSolarTerm(2026, 11, 26, 31, days), 22);
    RODAK_CHECK_EQ(days, 11);
    RODAK_CHECK_EQ(NextSolarTerm(2026, 12, 25, 31, days), 0);
    RODAK_CHECK_EQ(days, 11);
}

RODAK_TEST("out-of-range and invalid dates are rejected") {
    LunarDate date;
    RODAK_CHECK_FALSE(SolarToLunar(1899, 12, 31, date));
    RODAK_CHECK_FALSE(SolarToLunar(1900, 1, 30, date));
    RODAK_CHECK_FALSE(SolarToLunar(2101, 1, 1, date));
    RODAK_CHECK_FALSE(SolarToLunar(2026, 2, 29, date));
    RODAK_CHECK_FALSE(SolarToLunar(2026, 13, 1, date));
    RODAK_CHECK(SolarToLunar(2100, 12, 31, date));
    RODAK_CHECK(SolarToLunar(1900, 1, 31, date));
    RODAK_CHECK_EQ(LunarMonthDays(2026, 6, true), 0);
    int days = 0;
    RODAK_CHECK_EQ(NextSolarTerm(2100, 12, 31, 31, days), -1);
}

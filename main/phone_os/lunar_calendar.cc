#include "phone_os/lunar_calendar.h"

#include <cstdio>
#include <cstring>

namespace rodakos {
namespace {

#include "phone_os/lunar_calendar_data.inc"

constexpr const char* kMonthNames[] = {"正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊"};
constexpr const char* kDayNames[] = {
    "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
    "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
    "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十"};
constexpr const char* kTermNames[] = {
    "小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨", "立夏", "小满", "芒种", "夏至",
    "小暑", "大暑", "立秋", "处暑", "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至"};
constexpr const char* kStems[] = {"甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"};
constexpr const char* kBranches[] = {"子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"};
constexpr const char* kZodiac[] = {"鼠", "牛", "虎", "兔", "龙", "蛇", "马", "羊", "猴", "鸡", "狗", "猪"};

bool IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DaysInSolarMonth(int year, int month) {
    constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && IsLeapYear(year) ? 29 : kDays[month - 1];
}

int DayOfYear(int year, int month, int day) {
    int total = day;
    for (int m = 1; m < month; ++m) total += DaysInSolarMonth(year, m);
    return total;
}

uint32_t YearWord(int lunar_year) {
    return kLunarYears[lunar_year - kLunarFirstYear];
}

int LeapMonthOf(int lunar_year) {
    return static_cast<int>((YearWord(lunar_year) >> 13) & 0xf);
}

int MonthLength(int lunar_year, int month, bool leap) {
    const uint32_t word = YearWord(lunar_year);
    const bool big = leap ? ((word >> 12) & 1) != 0 : ((word >> (month - 1)) & 1) != 0;
    return big ? 30 : 29;
}

int FirstDayOfYear(int lunar_year) {
    return static_cast<int>((YearWord(lunar_year) >> 17) & 0x3f);
}

int YearLength(int lunar_year) {
    int total = 0;
    const int leap = LeapMonthOf(lunar_year);
    for (int m = 1; m <= 12; ++m) {
        total += MonthLength(lunar_year, m, false);
        if (m == leap) total += MonthLength(lunar_year, m, true);
    }
    return total;
}

int TermDay(int year, int index) {
    const uint64_t word = kTermOffsets[year - kLunarFirstYear];
    return kTermBaseDay[index] + static_cast<int>((word >> (2 * index)) & 3);
}

bool ValidSolar(int year, int month, int day) {
    return year >= kLunarFirstYear && year <= kLunarLastYear && month >= 1 && month <= 12 && day >= 1 &&
           day <= DaysInSolarMonth(year, month);
}

}  // namespace

bool SolarToLunar(int year, int month, int day, LunarDate& out) {
    if (!ValidSolar(year, month, day)) return false;
    const int doy = DayOfYear(year, month, day);
    int lunar_year = year;
    int offset = doy - FirstDayOfYear(year);
    if (offset < 0) {
        lunar_year = year - 1;
        if (lunar_year < kLunarFirstYear) return false;
        // Days from the previous lunar new year to this solar date.
        const int prev_len = IsLeapYear(lunar_year) ? 366 : 365;
        offset = prev_len - FirstDayOfYear(lunar_year) + doy;
    }
    const int leap = LeapMonthOf(lunar_year);
    for (int m = 1; m <= 12; ++m) {
        for (int pass = 0; pass < (m == leap ? 2 : 1); ++pass) {
            const bool is_leap = pass == 1;
            const int length = MonthLength(lunar_year, m, is_leap);
            if (offset < length) {
                out.year = lunar_year;
                out.month = m;
                out.day = offset + 1;
                out.leap = is_leap;
                const int a = month * 2 - 2;
                out.solar_term = TermDay(year, a) == day ? a : TermDay(year, a + 1) == day ? a + 1 : -1;
                return true;
            }
            offset -= length;
        }
    }
    return false;
}

int LunarMonthDays(int lunar_year, int lunar_month, bool leap) {
    if (lunar_year < kLunarFirstYear || lunar_year > kLunarLastYear || lunar_month < 1 || lunar_month > 12) {
        return 0;
    }
    if (leap && LeapMonthOf(lunar_year) != lunar_month) return 0;
    return MonthLength(lunar_year, lunar_month, leap);
}

int NextSolarTerm(int year, int month, int day, int max_days, int& days_until) {
    days_until = 0;
    if (!ValidSolar(year, month, day)) return -1;
    int y = year, m = month, d = day;
    for (int step = 0; step <= max_days; ++step) {
        const int a = m * 2 - 2;
        if (TermDay(y, a) == d) { days_until = step; return a; }
        if (TermDay(y, a + 1) == d) { days_until = step; return a + 1; }
        if (++d > DaysInSolarMonth(y, m)) {
            d = 1;
            if (++m > 12) {
                m = 1;
                if (++y > kLunarLastYear) return -1;
            }
        }
    }
    return -1;
}

const char* LunarMonthName(int month) {
    return month >= 1 && month <= 12 ? kMonthNames[month - 1] : "";
}

const char* LunarDayName(int day) {
    return day >= 1 && day <= 30 ? kDayNames[day - 1] : "";
}

const char* SolarTermName(int index) {
    return index >= 0 && index < 24 ? kTermNames[index] : "";
}

void LunarYearName(int lunar_year, char* out, int out_size) {
    if (out == nullptr || out_size <= 0) return;
    // 1984 is 甲子 (stem 0, branch 0).
    const int cycle = ((lunar_year - 1984) % 60 + 60) % 60;
    std::snprintf(out, static_cast<size_t>(out_size), "%s%s%s", kStems[cycle % 10], kBranches[cycle % 12],
                  kZodiac[cycle % 12]);
}

const char* FestivalName(int year, int month, int day, const LunarDate& lunar) {
    if (!lunar.leap) {
        if (lunar.month == 12 && lunar.day == LunarMonthDays(lunar.year, 12, false)) {
            // 除夕 is the last day of 腊月 only when no leap 腊月 follows it.
            if (LeapMonthOf(lunar.year) != 12) return "除夕";
        }
        if (lunar.month == 1 && lunar.day == 1) return "春节";
        if (lunar.month == 1 && lunar.day == 15) return "元宵";
        if (lunar.month == 5 && lunar.day == 5) return "端午";
        if (lunar.month == 7 && lunar.day == 7) return "七夕";
        if (lunar.month == 8 && lunar.day == 15) return "中秋";
        if (lunar.month == 9 && lunar.day == 9) return "重阳";
        if (lunar.month == 12 && lunar.day == 8) return "腊八";
    }
    (void)year;
    if (month == 1 && day == 1) return "元旦";
    if (month == 5 && day == 1) return "劳动节";
    if (month == 10 && day == 1) return "国庆";
    return nullptr;
}

}  // namespace rodakos

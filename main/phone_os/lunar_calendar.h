#pragma once

#include <cstdint>

// Offline Chinese lunar calendar for 1900-02-01 .. 2100-12-31, from a compact table
// (main/phone_os/lunar_calendar_data.inc, about 4 KB of flash). Pure arithmetic, no locale,
// safe from any task.
namespace rodakos {

struct LunarDate {
    int year = 0;    // lunar year number, e.g. 2026 for 丙午年
    int month = 0;   // 1..12
    int day = 0;     // 1..30
    bool leap = false;
    // Solar term that falls on this solar date, 0..23 (小寒 first), or -1.
    int solar_term = -1;
};

// False outside the supported range or for an invalid date.
bool SolarToLunar(int year, int month, int day, LunarDate& out);

// Day number (1..30) of the last day of a lunar month, or 0 if out of range.
int LunarMonthDays(int lunar_year, int lunar_month, bool leap);

// The solar term index (0..23) on or after the given solar date within `max_days`, with the
// distance in days; -1 when none.
int NextSolarTerm(int year, int month, int day, int max_days, int& days_until);

// Chinese text helpers. Returned pointers are static UTF-8 strings.
const char* LunarMonthName(int month);          // "正" .. "腊" (no 月)
const char* LunarDayName(int day);              // "初一" .. "三十"
const char* SolarTermName(int index);           // "小寒" .. "冬至"
// "丙午马" style stem-branch + zodiac for a lunar year.
void LunarYearName(int lunar_year, char* out, int out_size);
// Festival on this date ("春节", "除夕", "国庆" ...), or nullptr.
const char* FestivalName(int year, int month, int day, const LunarDate& lunar);

}  // namespace rodakos

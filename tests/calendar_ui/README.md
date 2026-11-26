# Calendar UI regression

This target compiles the production `CalendarApp` and `lunar_calendar` against LVGL 9.3's real
320×240 in-memory display with the production fonts, including the 12 px lunar subset
`font_puhui_lunar_12_4`. `time_service.h` is a fake with a settable clock and SNTP status;
the binary runs with `TZ=UTC` and "today" is 2026-11-26.

The 10 cases cover the whole-month grid (six 30 px rows for November 2026, lunar text,
solar terms and the 十月 month start in cells, dimmed neighbour days), lunar labels staying
inside their cells, shorter months getting taller rows (February 2027 in four 44 px rows with
除夕/春节/元宵/立春, January 2027 in five), the day page (lunar date, 丙午马年, ISO week, day of
year, next solar term, ‹ › stepping), festival and term lines on the day page, tapping a
neighbour day, the unsynced page starting SNTP and switching to the month when time arrives,
a sync that cannot start, midnight moving the today marker, and the light theme. The suite
writes `calendar-*.ppm` screenshots.

```sh
cmake -S tests/calendar_ui -B "$HOME/.cache/rodakos-calendar-ui" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$HOME/.cache/rodakos-calendar-ui" -j 6
ctest --test-dir "$HOME/.cache/rodakos-calendar-ui" --output-on-failure
```

The lunar conversion itself is covered by `tests/lunar_calendar` against reference data for
1900–2100.

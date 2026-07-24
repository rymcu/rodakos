# Home UI Host Tests

This target compiles the production Home app, Home model/store, Registry, `PhoneUi`, layout,
theme, components, and `SoftKeyboard` against LVGL 9.3's in-memory 320x240 display. Pointer
interactions use LVGL's test input device; only platform services, Settings, navigation, and fonts
are replaced by host fakes.

Run from Windows through WSL:

```powershell
wsl -d Debian -- bash -lc '
  cmake -S /mnt/d/workspace/rodakos/tests/home_ui \
        -B /tmp/rodakos-home-ui -G Ninja -DCMAKE_BUILD_TYPE=Debug &&
  cmake --build /tmp/rodakos-home-ui &&
  ctest --test-dir /tmp/rodakos-home-ui --output-on-failure
'
```

The suite covers tap slop, one-page and multi-page drag suppression, bidirectional page boundaries
and swipes, long-press Arrange entry, Cancel/Done persistence behavior, repeated Home, theme
rebuilding, keyboard geometry, the 96/97-app `All Apps` boundary, and asynchronous page residency.
It does not replace physical GT911/ST7789 interaction, readability, or true embedded out-of-memory
testing.

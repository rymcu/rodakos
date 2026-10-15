# Home UI Host Tests

This target compiles the production Home app, Home model/store, Registry, `PhoneUi`, layout,
theme, components, `BootAnimation`, appearance metadata codec, and `SoftKeyboard` against LVGL 9.3's in-memory 320x240 display. Pointer
interactions use LVGL's test input device; only platform services, Settings, navigation, and fonts
are replaced by host fakes.

Run from Windows through WSL:

```powershell
wsl -d Debian -- bash -lc '
  cmake -S /mnt/d/workspace/rodakos/tests/home_ui \
        -B ~/.cache/rodakos-home-ui -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DRODAKOS_IDF_PATH=/mnt/c/esp/v6.0.2/esp-idf &&
  cmake --build ~/.cache/rodakos-home-ui &&
  ctest --test-dir ~/.cache/rodakos-home-ui --output-on-failure
'
```

The target links the task-retirement host fixture, which reads FreeRTOS sources from the reviewed
ESP-IDF 6.0.2 tree. Configuration fails without `RODAKOS_IDF_PATH` (or `IDF_PATH`).

The suite covers tap slop, one-page and multi-page drag suppression, bidirectional page boundaries
and swipes, long-press Arrange entry, Cancel/Done persistence behavior, repeated Home, theme
rebuilding, keyboard geometry, the 96/97-app `All Apps` boundary, and asynchronous page residency.
Appearance tests read the rendered framebuffer for expanded A8 glyphs, little-endian RGB565,
RGB565A8 transparency, and the Home wallpaper. They advance real LVGL ticks to verify entrance,
configured duration, system-ready waiting, fade completion, timer/resource cleanup, and touch
restoration. GNU linker wrappers inject one-shot object/image/timer creation failures at named
boundaries; these do not simulate arbitrary heap exhaustion. Theme checks cover all four presets
with a custom primary color and readable button foregrounds. Icon tests read the framebuffer for
the 14px-radius vertical gradient, check the category/tint slot mapping, the 44px darkened press
state (including presses that become swipes or input resets), folder previews, collection rows,
the Arrange header, the All Apps foreground, and the per-preset palette contrast. The host
framebuffer is XRGB8888, so RGB565 banding needs hardware review. The host logo font is a test fake,
so built-in EDIX typography still requires production-font or hardware visual verification.
It does not replace physical GT911/ST7789 interaction, readability, or true embedded out-of-memory
testing.

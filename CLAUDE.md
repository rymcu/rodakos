# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

RodakOS is an ESP32-S3 firmware "Phone OS" experiment targeting the RYMCU BigSmart board (16MB flash, 8MB PSRAM, ST7789 LCD, GT911 touch). Built on ESP-IDF 5.5.4 (GCC), LVGL 9.3, `esp_lvgl_port` 2.6, and esp-brookesia HAL with Board Manager.

## Build Commands

Activate the local ESP-IDF environment once per shell session, then run any project script directly:

```powershell
. .\activate_idf.ps1                 # auto-detect installed ESP-IDF
. .\activate_idf.ps1 -Version v5.4.2 # pin a specific version
. .\activate_idf.ps1 -List           # show available versions
```

The activator scans `C:\esp\<vX.Y.Z>\esp-idf\`, reads `C:\Espressif\tools\eim_idf.json`, and falls back to `IDF_PATH`. After activation, `$env:IDF_PATH` is set and `idf.py` is on PATH, so the project scripts work without the official IDF PowerShell profile.

### First Build / Board Regeneration

```powershell
.\build_rodakos.ps1
```

Manual equivalent:

```powershell
idf.py set-target esp32s3
idf.py bmgr -b rymcu_bigsmart
.\fix_gen_paths.ps1
idf.py build
```

`build_rodakos.ps1` runs: `idf.py bmgr` → `fix_gen_paths.ps1` → clean `build/` → `idf.py reconfigure` → `idf.py build`.

### Daily Incremental Build

```powershell
idf.py build
```

Quick builds are build-only under the Recovery layout:

```powershell
.\quick_build.ps1
```

### Flash and Monitor

```powershell
.\flash_and_test.ps1              # default COM3
.\flash_and_test.ps1 -Port COM5
```

Exit monitor with `Ctrl+]`.

### Full Clean Rebuild (for Clang/bootloader issues)

```powershell
idf.py fullclean
Remove-Item -Recurse -Force build, components/gen_bmgr_codes -ErrorAction SilentlyContinue
idf.py bmgr -b rymcu_bigsmart
.\fix_gen_paths.ps1
idf.py build
```

### Reset Device State

```powershell
.\build_ota_bundle.ps1
.\flash_and_test.ps1 -Port COM3 -Erase
```

## Architecture

Three layers:

1. **Board/HAL** — esp-brookesia + Board Manager. Board YAML at `components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart/`. Generated code at `components/gen_bmgr_codes/` (gitignored). Device handles retrieved via `esp_board_manager_get_device_handle("device_name", ...)`.

2. **Phone OS (`main/phone_os/`)** — app lifecycle, navigation, and system services. Key types:
   - `PhoneAppDescriptor` — static metadata (id, title, icon, aliases, category, capabilities, factory)
   - `PhoneAppRegistry` — registry of descriptors
   - `PhoneAppHost` — create/show/hide/destroy app instances
   - `PhoneNavigation` — launch + return-home routing
   - `PhoneServices` — dependency container for all hardware/system services

3. **Phone UI (`main/phone_ui/`)** — LVGL wrappers for the 320x240 screen:
   - `PhoneUi` + `PhoneUiLock` — centralized LVGL access
   - `rodakos_theme` — dark/light/blue/green tokens
   - `rodakos_layout` — header/body/footer, grids, flex rows
   - `phone_fonts` — xiaozhi Chinese fonts + Font Awesome icons
   - `image_library` — JPG/PNG/BMP loader, prefers SPIRAM

### Adapters (`main/rodakos_adapters/`)

Bridge raw esp-brookesia device handles to C++ RodakOS interfaces (BacklightAdapter, WiFiAdapter, WiFiConfig, FileService). New system services belong in `phone_os/` and are exposed via `PhoneServices`. New apps go in `main/apps/<app>/` and register via `RegisterRodakBuiltInApps` in `main/apps/built_in_apps.cc`.

## Boot Order (critical)

`main.cc` `app_main()` must follow this sequence:

1. NVS init
2. USB MSC boot gate (early-boot SD-card mass-storage path, one-shot, returns before normal boot)
3. `esp_board_manager_init()`
4. Theme init + `PhoneUi`
5. LVGL port init + display
6. Cached GT911 touch polling bridge (created after display)
7. Fonts (`PhoneFontsInit()`)
8. Backlight (initialize then `RestoreBrightness()`)
9. Services — construct into `static` locals, then `services.Set*()` on `PhoneServices`
10. `PhoneSystem::Start()`
11. `button_binding_service.Init()`, `voice_wake_service.Start()`
12. WiFi auto-connect (after system start, non-blocking)

Reordering causes LVGL lock failures, blank screen, or touch hangs.

## GT911 Touch

Touch is read by a **cached polling bridge** in `main.cc`, not by `lvgl_port_add_touch()`. A FreeRTOS task (`touch_poll`) reads the controller every 20ms; LVGL's indev callback serves cached coordinates. This avoids blocking I2C inside the LVGL task. Do not call I2C from LVGL callbacks — update the bridge instead.

## App Authoring

- App lives in `main/apps/<app>/` with a `PhoneApp` subclass exposing `OnCreate / OnDestroy / OnResume / OnPause`.
- Register via `Register<App>App(registry)` in `main/apps/built_in_apps.cc`.
- Add sources to `main/CMakeLists.txt` `SOURCES` list.
- Use shared theme/layout/components (`rodakos_theme.h`, `rodakos_layout.h`, `phone_components.h`).
- Wrap LVGL calls in `PhoneUiLock lock(ui);`.
- App capabilities should be visible to Settings/System Info where useful.

## Service Authoring

- Services live in `main/phone_os/`.
- Wire raw esp-brookesia devices via `main/rodakos_adapters/` and inject from `main.cc`.
- Heavy hardware (codec, camera) is opened **on demand**, not at boot.
- WiFi credentials are stored in NVS via `WiFiConfig`; auto-connect starts after `PhoneSystem::Start()`.

## File Layout (high-level)

```text
main/
├── main.cc                     # app_main, boot sequence, touch bridge
├── settings.cc                 # NVS-backed settings
├── usb_msc_mode.cc             # early-boot SD-card mass-storage
├── rodakos_adapters/           # backlight, wifi, file service adapters
├── phone_os/                   # app lifecycle, services, audio, camera, etc.
├── phone_ui/                   # LVGL wrapper, fonts, theme, layouts, images
└── apps/                       # home, settings, photos, camera, music, etc.
components/
├── brookesia_hal_boards/       # board YAMLs for rymcu_bigsmart
├── esp_board_manager/          # Board Manager framework
└── gen_bmgr_codes/             # GENERATED — gitignored
docs/
├── architecture.md
├── firmware-download.md
└── roadmap.md
```

## Partition Table

`partitions_16m.csv` at repo root:

```csv
nvs,      data, nvs,     0x9000,  0x6000,
otadata,  data, ota,     0xf000,  0x2000,
phy_init, data, phy,     0x11000, 0x1000,
ota_state,data, nvs,     0x12000, 0x6000,
recovery, app,  factory, 0x20000, 0x280000,
app,      app,  ota_0,   0x2a0000,0xd50000,
coredump, data, coredump,0xff0000,0x10000,
```

Use exact hex sizes. Shorthand like `1M`/`15M` causes capacity errors. `rodakos.bin` is currently
about 3.2 MiB; it belongs in the 13.3125 MiB `ota_0` slot, not the 2.5 MiB factory Recovery slot.

## Code Style

- Match surrounding code density, naming, idioms.
- `ESP_LOGI / ESP_LOGW / ESP_LOGE` with component-specific TAGs.
- `std::unique_ptr` for app and large object ownership.
- `static_cast` only — no C-style casts.
- Comments describe non-obvious WHY only, never WHAT.

## Generated Paths Gotcha

`idf.py bmgr` can emit absolute paths into `components/gen_bmgr_codes/` pointing at `managed_components/`. Always run `.\fix_gen_paths.ps1` after `idf.py bmgr`. The script rewrites `idf_component.yml` and `CMakeLists.txt` to use `../../components/brookesia_hal_boards` instead.

If board "rymcu_bigsmart" not found: run `.\setup_board.ps1` first (creates junction under `components/esp_board_manager/boards/`).

## Toolchain Notes

- **Use GCC**, not Clang. ESP-IDF 5.5.4 with Clang on Windows has response-file bugs (`clang: error: no such file or directory: '@.../cflags'`).
- `sdkconfig` has `CONFIG_IDF_TOOLCHAIN="gcc"` and `CONFIG_IDF_TOOLCHAIN_GCC=y`.
- If builds still fail, use the VSCode ESP-IDF extension.

## Validation After Non-Trivial Changes

1. `idf.py build` succeeds.
2. `build/rodakos.bin` is under 8MB.
3. Flash with `.\flash_and_test.ps1 -Port COM<n>` and confirm boot log contains:
   - `RodakOS: Starting RodakOS with esp-brookesia HAL`
   - `Board manager initialized`
   - `Touch input registered with cached polling`
   - `LVGL port initialized`
   - `Backlight initialized and turned on`
   - `PhoneSystem: Starting Phone OS`
   - `HomeApp: Phone desktop ready with N apps`
   - `RodakOS: RodakOS started successfully`

If you cannot flash or observe the device, say so explicitly in the final report.

## Things That Often Break

- **LCD config field errors**: use `lcd_width` / `lcd_height`, not `x_max` / `y_max`.
- **Backlight LEDC**: control via ESP-IDF LEDC APIs using `periph_ledc_handle_t*`, not `dev_ledc_ctrl_*`.
- **LVGL task stack**: keep in internal RAM (not PSRAM) — PNG/JPG decoders need 16KB+ and NVS reads from async callbacks can fault if the stack is in flash.
- **Images OOM**: confirm PSRAM enabled, use `ImageLibrary::LoadImage()` return value, prefer FileService paths.
- **Audio/camera unavailable**: heavy hardware opens on demand — verify Board Manager device names (`audio_dac`, `audio_adc`, `camera`, `fs_sdcard`) and I2C bus health.
- **WiFi no auto-connect**: credentials must be saved via `WiFiConfig`; auto-connect runs after `PhoneSystem::Start()`.

## Built-In Apps (registered in `built_in_apps.cc`)

Home, Settings, Photos, Camera, Clock, File Manager, System Info, Music, Assistant, Smart.

## Active Services

Backlight, WiFi, FileService (SD card on demand), WebFileSystemService, AudioOutput, AudioService, MusicPlayer, AudioFocus, VoiceAssistant, VoiceWake, VoiceRecorder, DeviceCloudConfig, Camera, Light, ButtonBinding, Time.

## Non-Goals

- Runtime binary plug-in loading on ESP32-S3.
- OTA partitioning until app/service set stabilizes.
- Hand-written `main/board/` parallel to Board Manager.

## Additional Reference

- `README.md` — project overview, layout
- `AGENTS.md` — agent workflow rules (read alongside this file)
- `docs/architecture.md` — layers and service/app model
- `docs/firmware-download.md` — build, flash, monitor details
- `docs/roadmap.md` — current baseline and milestones
- `TROUBLESHOOTING.md` — known build/runtime fixes

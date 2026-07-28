# RodakOS Architecture

RodakOS is split into three practical layers: board/HAL integration, Phone OS services, and LVGL-facing UI/apps.

```mermaid
flowchart TD
  "app_main" --> "NVS"
  "app_main" --> "USB MSC boot gate"
  "app_main" --> "esp_board_manager"
  "esp_board_manager" --> "display_lcd"
  "esp_board_manager" --> "lcd_touch"
  "esp_board_manager" --> "audio_dac/audio_adc"
  "esp_board_manager" --> "fs_sdcard"
  "esp_board_manager" --> "camera"
  "app_main" --> "LVGL port"
  "LVGL port" --> "PhoneUi"
  "app_main" --> "PhoneServices"
  "PhoneServices" --> "Backlight/WiFi/FileService"
  "PhoneServices" --> "Audio/Music/Voice"
  "PhoneServices" --> "Camera/WebFiles/Time/Buttons/Lights/Motion/Wake-on-LAN"
  "PhoneServices" --> "Unified MQTT/SD OTA"
  "Unified MQTT/SD OTA" --> "Factory Recovery"
  "PhoneUi" --> "PhoneSystem"
  "PhoneSystem" --> "PhoneAppRegistry"
  "PhoneSystem" --> "PhoneAppHost"
  "PhoneSystem" --> "PhoneNavigation"
  "PhoneSystem" --> "PhoneShell"
  "PhoneShell" --> "Lock Screen overlay"
  "PhoneShell" --> "Control Center overlay"
  "PhoneAppRegistry" --> "Built-in Apps"
```

## Board And HAL

The board layer is esp-brookesia plus Board Manager generated code. RodakOS does not keep a hand-written `main/board/` implementation anymore.

- Board YAML lives under `components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart/`.
- `generate_board_config.ps1` owns Board Manager generation, including cold-bootstrap passes,
  generated-path normalization, and reconfiguration.
- Device handles are acquired through `esp_board_manager_get_device_handle()` and adapted by RodakOS services.

## Phone OS

`phone_os` owns app metadata, lifecycle, navigation, and system services.

Important pieces:

- `PhoneAppDescriptor`: static metadata, role, aliases, category, descriptive requirements, and app factory.
- `PhoneAppRegistry`: validates app identities and the unique Home role, then freezes before launch.
- `PhoneAppHost`: is the sole lifecycle owner and calls `OnCreate`, `OnResume`, `OnPause`, and
  `OnDestroy`; it also dispatches the optional repeated-Home request hook without exposing app
  instances to navigation policy.
- `PhoneNavigation`: app launch, return-home, theme, lock, Control Center, and Shell-preference routing.
- `PhoneShell`: owns Lock Screen and Control Center on `lv_layer_top()` without replacing or destroying the current app.
- `PhoneServices`: dependency container for hardware and system services.

RodakOS still uses statically linked apps. "Pluggable" means apps are modular at firmware architecture level: a new app registers a descriptor and factory, and Home discovers it from the registry.

Native app capabilities are descriptive metadata, not a security boundary. A future untrusted MiniApp runtime must use a separate capability broker, per-app storage, resource limits, and signed packages instead of receiving `PhoneServices` directly.

Home layout is app-owned state under `main/apps/home/`: a strict versioned JSON codec, pure exact-ID
model, Registry reconciliation, eight-page projection, guarded short-lived NVS store, discrete root
reordering, non-nested folder commands, single-save draft sessions, and a runtime-only page anchor.
Only a confirmed store save advances the live revision; uncertain writes freeze editing for that Home
session. Descriptor metadata remains static; user order and folders never mutate `PhoneAppDescriptor`
or Registry order.

Home creates one stable tile shell per projected page, but retains each page's grid, buttons, and
labels only for the active page and its existing immediate neighbors. The pure
`HomePageRenderWindow` and `HomePageRenderPlan` policy selects that window and fixes population order
as active, previous, then next. After `LV_EVENT_SCROLL_END`, `HomeApp` queues an LVGL async refresh
that releases far-page child trees before populating the new window. The tileview exposes a scroll
direction only when that adjacent page is already populated. UI teardown cancels the queued refresh
before navigation destruction or a theme rebuild can invalidate its `HomeApp` or LVGL objects.
Per-window and final-ready logs report resident page count, internal-SRAM free space, and the largest
free internal block.

`tests/app_model/` compiles the production Home codec/model/store and Registry, Host, and Navigation
sources against small host fakes. It verifies layout boundaries and persistence failures, identity
validation, transactional lifecycle order, theme recreation, re-entrancy guards, and navigation
forwarding without entering the firmware image. Its pure-model page-window tests pass for one through
eight pages, including boundary clamping and active/previous/next order; this target isolates policy.

`tests/home_ui/` complements the model suite by compiling the production `HomeApp`, Home
model/store, Registry, `PhoneUi`, layout, theme, components, and `SoftKeyboard` against LVGL 9.3's
in-memory 320x240 display. LVGL's test pointer drives the real widget/event tree while platform
services, Settings, navigation, and fonts use host fakes. Its thirteen tests cover tap slop,
one-page and multi-page drag suppression, boundary and bidirectional page swipes, long-press Arrange,
Cancel/Done persistence, repeated Home, theme rebuilding, keyboard geometry, 96/97-app `All Apps`,
and asynchronous active-plus-neighbors residency. The suite reports 13 tests and 0 failures,
including 20 consecutive normal runs and ASan/UBSan with leak detection.

The latest device run had 11 visible apps, so it exercised only a one-page `1/1` residency window.
Multi-page swiping remains unverified on hardware. GT911 touch behavior and ST7789 readability still
require manual or fixture validation. True out-of-memory recovery is also unproven: LVGL currently
uses CLIB allocation with malloc assertions enabled, so the SRAM logs provide observability rather
than evidence of graceful recovery. The host LVGL suite validates Home behavior and object lifetime,
but it cannot replace those embedded and physical gates.

## Phone UI

`phone_ui` wraps LVGL conventions for this 320x240 screen:

- `PhoneUi` and `PhoneUiLock` centralize LVGL access.
- `rodakos_theme` defines dark/light/blue/green theme tokens.
- `rodakos_layout` provides fixed-screen helpers for header/body/footer, grids, and flex rows.
- `phone_fonts` initializes xiaozhi Chinese fonts and Font Awesome icon fonts.
- `image_library` scans and loads JPG/PNG/BMP images from FileService-backed storage, preferring SPIRAM.

GT911 touch is registered through a cached polling bridge in `main.cc`: a low-priority task reads the touch controller and LVGL reads cached coordinates. This avoids doing I2C reads directly inside the LVGL task. `PhoneUi` retains the primary LVGL input device so system gestures can observe LVGL events without accessing GT911 or I2C.

## Built-In Apps

Built-in apps are registered in `main/apps/built_in_apps.cc`:

- Home: phone desktop, status area, app grid, dock/page affordances.
- Settings: WiFi, display/theme/brightness, USB disk mode, web/cloud file tools, time sync.
- Photos: scans `/photos`, `/DCIM`, and fallback roots for images.
- Camera: opens camera service on demand, previews and captures to storage.
- Clock: local display and network time sync entry points.
- Calendar: local month navigation and current-day selection.
- File Manager: browses and manages FileService-backed storage.
- Gyro: motion capability surface for gyroscope/accelerometer samples.
- System Info: firmware, WiFi, memory, and storage status.
- Music: scans `/music` and plays supported audio through the music/audio services.
- Recorder: captures microphone audio through the recording service and stores it through FileService.
- Assistant: voice assistant surface, wake-listening controls, cloud transport status.
- Smart: light/smart-device control surface.
- Wake: persists network devices and sends validated Wake-on-LAN magic packets over UDP broadcast.

## Service Notes

- SD storage mounts on demand through FileService; USB MSC mode is an early-boot path and does not start normal UI/services.
- Audio, music, voice assistant, camera, web file server, and cloud services are initialized as services but open heavy hardware paths only when needed.
- WiFi credentials are stored in NVS by `WiFiConfig`; auto-connect starts after PhoneSystem is up so UI boot is not blocked.
- MotionService exposes a stable app-facing motion API. The BigSmart QMI8658 adapter samples over the shared Board Manager I2C peripheral in a background task so apps never perform I2C work in the LVGL thread.
- UnifiedMqttService consumes Rodak bootstrap credentials, reports device state, and routes OTA
  notifications. OtaUpdateService stages and verifies the main image on SD; the separate factory
  Recovery project is the only runtime allowed to rewrite `ota_0`.
- Shell preferences use the short-lived `shell` NVS namespace. Settings exposes explicit commit results so a failed save cannot be reported as successful.
- ButtonBindingService encodes Lock and Control Center as stable actions; persisted `btnbind` values override compiled defaults.
- WakeOnLanService creates a UDP socket only for a user-requested wake, requires active WiFi, and
  leaves device-list persistence to the Wake app's versioned `wol/devices` NVS document.

## Related Docs

- [Firmware build and flash](firmware-download.md)
- [Project roadmap](roadmap.md)
- [OpenOS comparison and design decisions](openos-comparison.md)
- [Home layout and folder design](home-layout-design.md)
- [Rodak MQTT and SD Recovery OTA](mqtt-ota-sd-recovery.md)
- [Troubleshooting](../TROUBLESHOOTING.md)

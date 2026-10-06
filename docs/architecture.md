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
  "PhoneServices" --> "WebRTC Camera/Display peers"
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
- `AppearanceService`: publisher pinning, signed SD resources, bounded boot loading, revision trials
  and local theme overrides. It is independent of firmware OTA and exposed through `PhoneServices`.

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

The same target now compiles production `AudioOutputService`, `AudioService`, and
`AudioCodecOutput` with fake lower-level board/codec APIs. It verifies volume API error propagation,
configuration-cache retention, atomic relative updates and initial-open cleanup/retry. It also
compiles the production volume MCP dispatcher. `tests/voice_volume_service/` compiles the real
`VoiceAssistantService`, inbound queue, I/O loop and reconnect coordinator with host dependencies,
covering early initialize, Stop cancellation, stale generations and session ledger reset.
`tests/codec_volume/` separately
compiles the real dependency source with the project's volume correction and real software-volume
implementation, injecting failures at the lower-level driver callbacks. See
[dependency maintenance](dependency-maintenance.md) for source identity and build integration,
and [local audio validation](ota-release-readiness.md#2026-10-06-local-audio-volume-validation)
for the earlier service fixture's results and hardware limits. Current wire behavior and
software-receipt boundaries live in [voice volume MCP](voice-volume-mcp.md).

`tests/home_ui/` complements the model suite by compiling the production `HomeApp`, Home
model/store, Registry, `PhoneUi`, layout, theme, components, `BootAnimation`, appearance metadata
codec and `SoftKeyboard` against LVGL 9.3's
in-memory 320x240 display. LVGL's test pointer drives the real widget/event tree while platform
services, Settings, navigation, and fonts use host fakes. The suite covers tap slop,
one-page and multi-page drag suppression, boundary and bidirectional page swipes, long-press Arrange,
Cancel/Done persistence, repeated Home, theme rebuilding, keyboard geometry, 96/97-app `All Apps`,
and asynchronous active-plus-neighbors residency, boot-image formats/timing/lifetime, touch recovery,
Home wallpaper and unified theme colors. The current WSL run reports 43 tests and 0 failures in
Debug and ASan/UBSan with leak detection. The logo font fake does not verify built-in EDIX typography.

The latest 2026-10-01/02 COM3 evidence also covers the integrated media peers: six display sessions
(320x240, four rounds, 283 JPEG samples), camera/display mutual exclusion, and clean stop/navigation
cleanup. Remote text and explicit screen-control authorization were exercised against the real LVGL
widget tree. This proves the WebRTC peer path on the tested device; it does not provide an HTTP
camera snapshot/MJPEG endpoint or establish sustained high-frame-rate video. The 13-app/two-page
Home population still proves boot only; page gestures, Arrange, page restoration, GT911/ST7789
interaction, three-page far-page release, and graceful LVGL out-of-memory recovery remain physical
gates. The host suite validates Home behavior and object lifetime but cannot replace those gates.

## Phone UI

`phone_ui` wraps LVGL conventions for this 320x240 screen:

- `PhoneUi` and `PhoneUiLock` centralize LVGL access.
- `rodakos_theme` defines dark/light/blue/green theme tokens.
- `rodakos_layout` provides fixed-screen helpers for header/body/footer, grids, and flex rows.
- `phone_fonts` initializes the RodakOS-owned CJK and Font Awesome UI font subset.
- `BootAnimation` renders the built-in EDIX fallback or compiled appearance units with elapsed LVGL
  ticks. `PhoneUi` retains a shared RGB565 Home wallpaper and follows the global theme colors.
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
- Assistant: configuration and status for the local "你好达克" monitor; interaction runs as a
  system service rather than an app-owned Talk/Stop session.
- Smart: light/smart-device control surface.
- Wake: persists network devices and sends validated Wake-on-LAN magic packets over UDP broadcast.

## Service Notes

- Appearance resources use `components/rodak_appearance` for metadata/signature/revision policy and
  `/rodakos/appearance/` on SD for two verified slots. A background loader overlaps display setup;
  only results accepted within the total 1500 ms budget may enter the boot UI. The device requires
  physical touch to pin a publisher key/origin. Runtime downloads defer during media/voice/OTA and
  activate through a next-boot trial. See [Appearance resources](appearance-customization.md) for
  the RAP1 layout, HTTP limitations, limits and still-required hardware evidence.

- SD storage mounts on demand through FileService; USB MSC mode is an early-boot path and does not start normal UI/services.
- Audio, music, voice assistant, camera, web file server, and cloud services are initialized as services but open heavy hardware paths only when needed.
- `AudioOutputService` owns the shared output configuration. With an open codec it commits a new
  volume only after `AudioCodecOutput::SetVolume` succeeds. `AudioService` reads that shared value
  for playback/UI, and atomic set/up/down operations serialize calculation, hardware application,
  revision and receipt under the output mutex. With the codec closed, a setter accepts configuration
  for the next open. Failure of the initial codec volume API call closes that open attempt and
  clears its format state so a later request can retry. This preserves on-demand hardware ownership.
- MQTT desired-volume application checks the setter result and reports the retained configuration
  on failure. A source-verified build overlay for esp_codec_dev 1.5.7 propagates the selected
  codec/software-volume driver's exact error and only commits the dependency cache on success.
  It preserves software priority and no-codec software output. Other dependency API error paths
  remain outside this focused correction; success does not establish hardware or acoustic success.
  Ordinary shadow reports contain no volume effect ID or applied desired version; see the
  [AIoT shadow contract](rodak-aiot-contract-v1.md#5-shadow-state-and-device-properties).
  Correlated MQTT volume effects use a separate `effects/receipt` publication and a bounded
  authority-scoped ledger. Real connection epochs travel through fragment assembly and the
  worker queue; the scope check and `ApplyVolume` share the MQTT service lock with cancellation.
  Receipt enqueue runs in the SDK custom-event callback to preserve SDK/service lock order.
  See [MQTT volume effects](mqtt-volume-effects.md) for the contract and host service tests.
- Voice wake monitoring uses local MultiNet without a cloud connection. A wake match takes exclusive
  audio focus and opens one Rodak WebSocket session. Every non-terminal reply drains TTS and starts
  the next input turn on that same session; a user saying “再见” results in `session.end`, while 30 seconds of follow-up silence, errors, or a
  connection/listening watchdog disconnect and re-arm local monitoring. Active TTS playback is not
  terminated by that watchdog. See [Voice assistant integration](voice-assistant.md) and the
  [realtime voice v1 contract](rodak-realtime-voice-contract-v1.md).
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
- [BigSmart WebRTC peer integration](esp-peer-integration.md)
- [Appearance verification](appearance-verification.md)
- [Project roadmap](roadmap.md)
- [OpenOS comparison and design decisions](openos-comparison.md)
- [Home layout and folder design](home-layout-design.md)
- [Rodak AIoT v1 contract](rodak-aiot-contract-v1.md)
- [Rodak realtime voice v1 contract](rodak-realtime-voice-contract-v1.md)
- [Voice assistant integration](voice-assistant.md)
- [Serial provisioning](serial-provisioning.md)
- [Rodak MQTT and SD Recovery OTA](mqtt-ota-sd-recovery.md)
- [Troubleshooting](../TROUBLESHOOTING.md)

# RodakOS Roadmap

RodakOS is an embedded Phone OS experiment for the RYMCU BigSmart, not a web prototype. The current direction is to keep the OS, services, and UI framework clearly separated while making the device feel like a small real phone home screen instead of a debug menu.

## Current Baseline

As of 2026-07-22:

- ESP32-S3 target, 16MB flash, 8MB PSRAM.
- Local Board Manager configuration and BigSmart board definition for `rymcu_bigsmart`.
- LVGL 9.3 display pipeline, double buffering, xiaozhi fonts, theme/layout helpers.
- ST7789 display, LEDC backlight, GT911 touch via cached polling, WiFi STA, SD FileService, USB MSC mode.
- App registry/host/navigation model is in place, validates a unique Home role, and gives the host sole ownership of app lifecycle.
- Native Lock Screen and Control Center overlays are owned by PhoneSystem rather than modeled as apps.
- Home Phase 4 keeps stable page tile shells while retaining grids/buttons only for the active page
  and immediate neighbors. Pure policy tests and a production-HomeApp host LVGL suite pass;
  multi-page hardware validation remains open.
- Built-in apps: Home, Settings, Photos, Camera, Clock, File Manager, Gyro, System Info, Music, Recorder, Assistant, Smart.
- Services in use or scaffolded: backlight, WiFi, file service, web file service, camera, audio input/output, music player, recording, audio focus, voice assistant, voice wake, device cloud config, time, button binding, lights, motion, unified MQTT, and SD-staged OTA.
- Current built app binary is about 3.2 MiB of the 13.3125 MiB main application partition.

## Milestone 0: Hardware And Build Baseline

Status: done, with ongoing maintenance.

- Keep `idf.py build` working from ESP-IDF PowerShell.
- Keep `build_rodakos.ps1`, `fix_gen_paths.ps1`, and `flash_and_test.ps1` aligned with the board-manager workflow.
- Keep the 16 MiB Recovery/ota_0 partition table stable after the wired OTA migration.
- Keep the guarded first-boot capture green before allowing an interactive monitor to attach.
- Keep generated `components/gen_bmgr_codes/` out of git.

## Milestone 1: Phone Shell

Status: active; source implementation requires hardware verification.

- Home is a real desktop, not a debug list.
- Home supports 4x3 pages driven by the registered app list.
- Home has a versioned exact-ID layout model, strict JSON codec, Registry reconciliation, guarded NVS
  persistence, folder browsing, and an eight-page `All Apps` overflow projection.
- Home restores its page anchor in runtime RAM across app returns and theme rebuilding. Long-press
  Arrange supports adjacent App/Folder moves, folder create/rename/dissolve, and member moves within,
  out of, or between folders. Cancel discards the whole draft and Done performs one guarded save;
  failures preserve the live layout and unsafe write states lock the current Home session.
- Home keeps all tile shells stable but only the active page plus existing previous/next pages retain
  their LVGL child trees. Scroll-end work asynchronously releases far pages and populates active,
  previous, then next. Scroll directions stay restricted until the corresponding adjacent page is
  ready, queued refreshes are canceled before theme/navigation teardown, and final-ready logs report
  internal-SRAM free space and largest free block.
- The host LVGL 9.3 target compiles the real Home UI and reports 13 tests and 0 failures covering
  tap-versus-drag suppression, bidirectional page swipes and boundaries, long press, Cancel/Done,
  repeated Home, theme rebuild, keyboard geometry, 96/97-app `All Apps`, and async
  active-plus-neighbors residency. Twenty repeated normal runs and ASan/UBSan with leak detection
  also pass.
- Apps register through descriptors and factories.
- Lock Screen and Control Center use native top-layer overlays and do not consume an app lifecycle slot.
- Settings controls startup locking and the top-edge Control Center gesture.
- Button bindings expose Lock and Control Center; IO10 defaults to Control Center / Smart / Lock for single / double / long press.
- Settings manages brightness, theme, WiFi, USB disk mode, file tools, and time/cloud-related settings.
- Touch input is available through cached polling rather than direct LVGL I2C reads.

Next polish:

- Keep free drag deferred until paging and touch are proven together.
- Use live service state consistently in status UI.
- Tighten navigation transitions and back/home behavior across all apps.
- Add a hardware population that exceeds the current 11 visible apps; the latest device run had only
  one page (`1/1` resident), so multi-page swiping and lazy page turnover are not yet device-verified.
- Validate Home Arrange, page restoration, GT911 gestures, ST7789 readability, System Shell
  preferences, and both physical-button paths manually or with an external fixture.
- Define and execute a true low-memory recovery gate. Current LVGL CLIB allocation with malloc
  assertions means the SRAM residency logs do not yet prove graceful out-of-memory recovery.
- Treat swipe unlock as a privacy cover only until PIN, encrypted storage, Secure Boot, and Flash Encryption policies exist.

## Milestone 2: Media And Storage Apps

Status: active.

- Photos scans SD-backed image folders and renders JPG/PNG/BMP images.
- File Manager browses FileService-backed storage.
- Music scans `/music` and plays through the audio/music service stack.
- Camera preview and capture are wired through CameraService.
- USB MSC mode exposes the SD card before normal UI starts.

Next work:

- Verify media apps on real SD cards with large files and low-memory conditions.
- Add clearer empty/error states for missing SD card, unsupported images, no tracks, and camera unavailable.
- Decide whether captured photos should prefer `/photos`, `/DCIM`, or both.

## Milestone 3: System Services

Status: partially implemented.

- WiFi scan/connect/auto-connect exists.
- Time sync entry points exist.
- Audio playback and voice assistant services exist, with heavy hardware opened on demand.
- Light and button binding services exist.

Next work:

- Add battery/charging service and replace any placeholder status values.
- Harden audio codec startup/shutdown and failure recovery.
- Finish voice recorder/runtime integration beyond the current no-op/unavailable paths.
- Add diagnostics for I2C bus health, SD card status, and memory pressure.
- Consider moving QMI8658 metadata into the board definition once Board Manager has a first-class IMU device type.

## Milestone 4: App Model

Status: static native app model is hardened.

- Registry finalization rejects invalid identities, alias conflicts, missing factories, and invalid Home roles.
- Lifecycle is `OnCreate -> OnResume -> OnPause -> OnDestroy`, owned only by PhoneAppHost.
- App launch is transactional: a failed candidate does not destroy the current app.
- Continuous playback, recording, and other background behavior belongs to services rather than retained UI app instances.
- Host-side tests execute production Registry, Host, and Navigation sources and cover lifecycle, validation, theme recreation, and forwarding contracts.
- A separate host LVGL target executes production `HomeApp`, layout/theme, `SoftKeyboard`, and its
  real LVGL event/object tree against an in-memory 320x240 display.

Next work:

- Extend host LVGL coverage into PhoneSystem policy when it can be isolated from NVS and hardware.
- Keep app-specific code inside `main/apps/<app>/` and avoid central switch statements.
- Make app capabilities visible to Settings/System Info where useful.
- Keep native apps statically linked; do not treat descriptive native capabilities as access control.

## Milestone 5: Assistant And Cloud

Status: early integration.

Next work:

- Complete cloud configuration UX.
- Replace no-op recorder/wake runtime placeholders with real hardware-backed implementations when the audio path is stable.
- Decide what should be system-level assistant behavior versus a regular Assistant app surface.

## Milestone 6: Rodak Device And OTA Protocol

Status: active.

- Unified MQTT v2 bootstrap, telemetry, reported/desired shadow and OTA notification transport.
- SD-staged main image download with SHA-256 and a separate factory Recovery writer.
- Isolated OTA journal, startup confirmation, rollback restore and Rodak result reporting.
- Wired first-flash Recovery handoff, local image confirmation, and a direct second boot have been
  verified on the COM3 BigSmart device.

Before production rollout:

- Add cryptographic image signatures and provision the verification key in Recovery.
- Run power-cut tests at download, journal, erase, write, boot confirmation and rollback boundaries.
- Verify the complete flow on hardware with Rodak-hosted artifacts.

## Milestone 7: Sandboxed Mini Apps

Status: design only.

- Do not import the OpenOS OSA VM, `.osa/.osac` format, or writable-SD privileged system scripts.
- Define a versioned manifest around stable ID, version, entry point, declared capabilities, and resource limits.
- Run untrusted code behind a capability broker; never expose `PhoneServices` or raw hardware handles.
- Give each MiniApp an isolated storage root and bounded memory, execution time, UI objects, and network access.
- Require authenticated transport and package signatures rooted in an immutable trust key.
- Use staged install, path traversal checks, size limits, backup, and rollback before activating a package.

The source comparison and rationale are recorded in [OpenOS comparison and design decisions](openos-comparison.md).

## Non-Goals For Now

- Runtime binary plug-in loading on ESP32-S3.
- Reintroducing a hand-written board layer parallel to Board Manager.
- Direct execution of application images from SD card.

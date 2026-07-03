# RodakOS Roadmap

RodakOS is an embedded Phone OS experiment for the RYMCU BigSmart, not a web prototype. The current direction is to keep the OS, services, and UI framework clearly separated while making the device feel like a small real phone home screen instead of a debug menu.

## Current Baseline

As of 2026-07-02:

- ESP32-S3 target, 16MB flash, 8MB PSRAM.
- Local Board Manager configuration and BigSmart board definition for `rymcu_bigsmart`.
- LVGL 9.3 display pipeline, double buffering, xiaozhi fonts, theme/layout helpers.
- ST7789 display, LEDC backlight, GT911 touch via cached polling, WiFi STA, SD FileService, USB MSC mode.
- App registry/host/navigation model is in place and Home launches apps from descriptors.
- Built-in apps: Home, Settings, Photos, Camera, Clock, File Manager, System Info, Music, Assistant, Smart.
- Services in use or scaffolded: backlight, WiFi, file service, web file service, camera, audio output, music player, audio focus, voice assistant, voice wake, device cloud config, time, button binding, lights.
- Current built app binary is about 3.7MB of the 8MB factory partition.

## Milestone 0: Hardware And Build Baseline

Status: done, with ongoing maintenance.

- Keep `idf.py build` working from ESP-IDF PowerShell.
- Keep `build_rodakos.ps1`, `fix_gen_paths.ps1`, and `flash_and_test.ps1` aligned with the board-manager workflow.
- Keep the 16MB partition table stable unless OTA is introduced.
- Keep generated `components/gen_bmgr_codes/` out of git.

## Milestone 1: Phone Shell

Status: largely implemented.

- Home is a real desktop, not a debug list.
- Apps register through descriptors and factories.
- Settings manages brightness, theme, WiFi, USB disk mode, file tools, and time/cloud-related settings.
- Touch input is available through cached polling rather than direct LVGL I2C reads.

Next polish:

- Improve Home page density and dock/page behavior for more than one page of apps.
- Use live service state consistently in status UI.
- Tighten navigation transitions and back/home behavior across all apps.

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

## Milestone 4: App Model

Status: static app model is working.

Next work:

- Normalize app manifest fields: id, title, icon, aliases, category, capabilities, visibility.
- Keep app-specific code inside `main/apps/<app>/` and avoid central switch statements.
- Make app capabilities visible to Settings/System Info where useful.
- Consider manifest generation later, but keep static linking as the default ESP-IDF-friendly model.

## Milestone 5: Assistant And Cloud

Status: early integration.

Next work:

- Complete cloud configuration UX.
- Replace no-op recorder/wake runtime placeholders with real hardware-backed implementations when the audio path is stable.
- Decide what should be system-level assistant behavior versus a regular Assistant app surface.

## Non-Goals For Now

- Runtime binary plug-in loading on ESP32-S3.
- Reintroducing a hand-written board layer parallel to Board Manager.
- OTA partitioning until the current app/service set stabilizes.

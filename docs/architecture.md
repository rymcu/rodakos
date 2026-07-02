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
  "PhoneServices" --> "Camera/WebFiles/Time/Buttons/Lights"
  "PhoneUi" --> "PhoneSystem"
  "PhoneSystem" --> "PhoneAppRegistry"
  "PhoneSystem" --> "PhoneAppHost"
  "PhoneSystem" --> "PhoneNavigation"
  "PhoneAppRegistry" --> "Built-in Apps"
```

## Board And HAL

The board layer is esp-brookesia plus Board Manager generated code. RodakOS does not keep a hand-written `main/board/` implementation anymore.

- Board YAML lives under `components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart/`.
- `idf.py bmgr -b rymcu_bigsmart` regenerates `components/gen_bmgr_codes/`.
- `fix_gen_paths.ps1` rewrites generated paths so the local `components/` copy is used instead of old `managed_components` paths.
- Device handles are acquired through `esp_board_manager_get_device_handle()` and adapted by RodakOS services.

## Phone OS

`phone_os` owns app metadata, lifecycle, navigation, and system services.

Important pieces:

- `PhoneAppDescriptor`: static metadata, aliases, category, capabilities, and app factory.
- `PhoneAppRegistry`: registry of all app descriptors.
- `PhoneAppHost`: creates, shows, hides, and destroys app instances.
- `PhoneNavigation`: app launch and return-home routing.
- `PhoneServices`: dependency container for hardware and system services.

RodakOS still uses statically linked apps. "Pluggable" means apps are modular at firmware architecture level: a new app registers a descriptor and factory, and Home discovers it from the registry.

## Phone UI

`phone_ui` wraps LVGL conventions for this 320x240 screen:

- `PhoneUi` and `PhoneUiLock` centralize LVGL access.
- `rodakos_theme` defines dark/light/blue/green theme tokens.
- `rodakos_layout` provides fixed-screen helpers for header/body/footer, grids, and flex rows.
- `phone_fonts` initializes xiaozhi Chinese fonts and Font Awesome icon fonts.
- `image_library` scans and loads JPG/PNG/BMP images from FileService-backed storage, preferring SPIRAM.

GT911 touch is registered through a cached polling bridge in `main.cc`: a low-priority task reads the touch controller and LVGL reads cached coordinates. This avoids doing I2C reads directly inside the LVGL task.

## Built-In Apps

Built-in apps are registered in `main/apps/built_in_apps.cc`:

- Home: phone desktop, status area, app grid, dock/page affordances.
- Settings: WiFi, display/theme/brightness, USB disk mode, web/cloud file tools, time sync.
- Photos: scans `/photos`, `/DCIM`, and fallback roots for images.
- Camera: opens camera service on demand, previews and captures to storage.
- Clock: local display and network time sync entry points.
- File Manager: browses and manages FileService-backed storage.
- System Info: firmware, WiFi, memory, and storage status.
- Music: scans `/music` and plays supported audio through the music/audio services.
- Assistant: voice assistant surface, wake-listening controls, cloud transport status.
- Smart: light/smart-device control surface.

## Service Notes

- SD storage mounts on demand through FileService; USB MSC mode is an early-boot path and does not start normal UI/services.
- Audio, music, voice assistant, camera, web file server, and cloud services are initialized as services but open heavy hardware paths only when needed.
- WiFi credentials are stored in NVS by `WiFiConfig`; auto-connect starts after PhoneSystem is up so UI boot is not blocked.

## Related Docs

- [Firmware build and flash](firmware-download.md)
- [Project roadmap](roadmap.md)
- [Troubleshooting](../TROUBLESHOOTING.md)

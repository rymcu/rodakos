# RodakOS

RodakOS is ESP32-S3 firmware for the RYMCU BigSmart board. It is a small "Phone OS" experiment
with a 320x240 touch Home screen, system apps, media apps and hardware services. It uses ESP-IDF,
LVGL and Espressif Board Manager.

## Features

- **Phone OS shell**: static app registry, single lifecycle host, navigation, and a Shell-owned
  Lock Screen and Control Center that sit above the current app.
- **Home**: versioned exact-ID layout in `home/layout`, folders, and an Arrange mode with one
  guarded save. Home has up to eight pages plus a read-only `All Apps` page. Only the active page
  and its two neighbors keep their widgets. See [Home layout](docs/home-layout-design.md).
- **Built-in apps**: Home, Settings, Photos, Camera, Clock, Calendar, Files, Gyro, System,
  Music, Recorder, Assistant, Smart and Wake (Wake-on-LAN).
- **Hardware services**: display, backlight, cached GT911 touch polling, WiFi, SD card with
  path-scoped leases, early-boot USB MSC mode, audio playback and recording, GC0308 camera,
  QMI8658 motion, RGB light and battery monitor. The firmware opens heavy devices on demand, not
  at boot.
- **IO10 key**: a single click toggles Control Center, a double click opens Smart, and a long
  press locks the device. Custom bindings in NVS replace these defaults.
- **Voice**: local MultiNet wake word, `rodak-realtime-voice/v1` sessions over pinned WSS, AEC and
  barge-in, volume MCP tools with software receipts, versioned voice identity. See
  [voice assistant](docs/voice-assistant.md).
- **Rodak cloud**: MQTT provisioning and same-authority credential refresh, shadow and
  telemetry, correlated volume and light effects, command results bound to their original
  connection, and Camera/Display WebRTC peers with MQTT signaling. See the
  [AIoT contract](docs/rodak-aiot-contract-v1.md).
- **Trusted server**: a server identity installed over USB pins HTTPS, MQTTS and WSS to a stable
  `.local` name. The firmware authenticates each DNS-SD candidate before it promotes a route. See
  [trusted server discovery](docs/trusted-server-discovery.md).
- **OTA**: immutable Recovery, isolated A/B OTA journal, SD-staged updates into one 13.3125 MiB
  `ota_0` slot, per-task RSA-2048 manifests and offline production signing. See
  [Rodak MQTT and SD Recovery OTA](docs/mqtt-ota-sd-recovery.md).
- **Appearance**: signed SD packages for boot logo, wallpaper and theme with publisher
  confirmation and next-boot trial. See [appearance customization](docs/appearance-customization.md).

Production release is still **NO_GO**. The open gates are the production signing root, real power
cuts, the eight-hour qualification soak and the remaining physical checks. The
[roadmap](docs/roadmap.md) lists them. [OTA release readiness](docs/ota-release-readiness.md)
keeps the dated device evidence.

## Hardware And Frameworks

- RYMCU BigSmart: ESP32-S3, 16 MB flash, 8 MB PSRAM, 320x240 ST7789 LCD, GT911 touch,
  PCA9557 IO expander, ES8311/ES7210 audio codecs, GC0308 DVP camera, WS2812 RGB light,
  LEDC backlight.
- ESP-IDF 6.0.2 with its recommended Xtensa GCC, LVGL 9.3, `esp_lvgl_port` 2.8, Espressif
  Board Manager and the RodakOS-owned `rodakos_hal_boards` component.

## Build

Activate the project-local ESP-IDF 6.0.2 environment once per shell. The build, package and flash
scripts reject other IDF versions. You do not need a global PowerShell profile.
[Firmware build and flash](docs/firmware-download.md) gives the full procedure.

```powershell
# Activate ESP-IDF. The script prefers the installed 6.0.2. -List shows all installed versions.
. .\activate_idf.ps1
. .\activate_idf.ps1 -Version v6.0.2

# First build, board changes, or after deleting generated board files
.\build_rodakos.ps1

# Daily incremental build of the ota_0 application
idf.py build

# Build main + Recovery and a signed package (development key on this host)
.\build_ota_bundle.ps1 -SigningKeyPath C:\secure\ota-private.pem `
  -VerificationKeyPath C:\secure\ota-public.pem `
  -SigningTaskNo <rodak-task> -SigningVersion <compiled-version>

# Production: keep the private key offline. Package with the public key only.
.\build_ota_bundle.ps1 -VerificationKeyPath C:\secure\ota-public.pem `
  -SigningTaskNo <rodak-task> -SigningVersion <compiled-version> -OfflineSigning

# Refresh a Recovery-layout device, preserving NVS and the OTA journal
.\flash_and_test.ps1 -Port COM3 -NoMonitor

# First Recovery-layout migration. This clears all device state.
.\flash_and_test.ps1 -Port COM3 -Erase -NoMonitor
```

- Do not use root `idf.py flash` or `idf.py app-flash`. They write to the small factory
  (Recovery) partition. A normal Rodak OTA uploads only `build/rodakos.bin`.
- Without `-Erase`, the flash script first checks that the installed partition table and
  immutable Recovery match the package. It then writes only `otadata` and `ota_0`. Then it
  checks the Recovery handoff, the boot confirmation and the Home startup.
- [Production signing](docs/production-signing.md) gives the steps for offline signing,
  signature attachment and readback.
- Board Manager generates `components/gen_bmgr_codes/`, and Git ignores it.
  `generate_board_config.ps1` runs the generation, normalizes paths and reconfigures the project.
- `dependencies.lock` is the authoritative resolved component graph. `main/idf_component.yml`
  pins the direct versions.
- The CMake build generates reviewed overlays for some managed components (codec, MQTT,
  WebSocket, LVGL/LodePNG, camera/DVP, AES DMA) from `patches/`. The build stops if the upstream
  source changes. See [dependency maintenance](docs/dependency-maintenance.md).

## Host Tests

Host tests run under WSL/Linux, outside the firmware build. This command runs one suite:

```powershell
wsl -d Debian -- bash -lc '
  cmake -S /mnt/d/workspace/rodakos/tests/app_model \
        -B ~/.cache/rodakos-app-model -G Ninja -DCMAKE_BUILD_TYPE=Debug &&
  cmake --build ~/.cache/rodakos-app-model &&
  ctest --test-dir ~/.cache/rodakos-app-model --output-on-failure
'
```

The full release regression runs all suites with ASan/UBSan and leak checks, plus the Python
tests. It needs the reviewed ESP-IDF 6.0.2 source tree:

```bash
export RODAKOS_IDF_PATH=/path/to/esp-idf-v6.0.2
bash tools/run_release_host_checks.sh "$HOME/.cache/rodakos-release"
```

Each `tests/<suite>/README.md` states what the suite compiles and what it does not prove.
[Host CI](docs/host-ci.md) describes the dependencies and the CI workflow.

## Project Layout

```text
main/
├── main.cc                # Boot sequence (see AGENTS.md for the required order)
├── apps/                  # Built-in Phone OS apps
├── phone_os/              # App model, navigation, Shell and system services
├── phone_ui/              # LVGL wrapper, fonts, themes, layout, components, image loading
├── rodakos_adapters/      # Board Manager device adapters (audio, camera, files, WiFi, motion)
└── usb_msc_mode.cc        # Early-boot USB mass-storage mode for SD card access

recovery/                  # Immutable Recovery (factory) application
components/
├── rodakos_hal_boards/    # BigSmart board definition and its PCA9557 driver
├── esp_board_manager/     # Espressif Board Manager
├── esp_peer/              # WebRTC peer (local copy with RodakOS fixes)
├── rodak_ota_state/       # OTA journal, signature format and release-test fault points
├── rodak_appearance/      # Signed appearance package parsing and policy
├── rodakos_fonts/         # Font subset (Chinese glyphs, Ionicons UI icons, EDIX logo)
├── json/                  # IDF 5 `json` name re-exported to managed cJSON for ESP-SR
└── gen_bmgr_codes/        # Generated board config (gitignored)

cmake/, patches/           # Reviewed overlays applied to managed components at build time
tools/                     # OTA signing/verification, overlay generators, serial capture tools
tests/                     # Host test suites, one directory per suite
docs/                      # Design, contracts, evidence and plans (see below)
```

## Hardware Configuration

Board configuration lives in `components/rodakos_hal_boards/boards/rymcu/rymcu_bigsmart/`:

- `board_devices.yaml`: LCD, touch, audio codecs, SD card, camera, buttons, RGB light.
- `board_peripherals.yaml`: GPIO, I2C, SPI, SDMMC, ADC, DVP, LEDC/RMT pins.
- `board_info.yaml` and `sdkconfig.defaults.board`: board identity and build defaults.
- `setup_device.c`: board-specific initialization hooks (including the camera DVP_EN power reset).
- `components/esp_io_expander_pca9557/`: the board's local IO-expander driver.

BigSmart is the only supported board. Future ports belong under `boards/<vendor>/<board>/` with
explicit build selection and validation. Board schematics are in `images/`.

The project uses `partitions_16m.csv`. The firmware stages OTA payloads on the SD card. Internal
flash therefore holds an immutable Recovery and one large main application slot:

```csv
# Name,    Type, SubType, Offset,   Size,     Flags
nvs,       data, nvs,     0x9000,   0x6000,
otadata,   data, ota,     0xf000,   0x2000,
phy_init,  data, phy,     0x11000,  0x1000,
ota_state, data, nvs,     0x12000,  0x6000,
recovery,  app,  factory, 0x20000,  0x280000,
app,       app,  ota_0,   0x2a0000, 0xd50000,
coredump,  data, coredump,0xff0000, 0x10000,
```

Use hexadecimal partition sizes. Shorthand such as `1M` or `15M` caused capacity errors before.

## Documentation

Start here:

- [Architecture](docs/architecture.md): layers, services and app model
- [Firmware build and flash](docs/firmware-download.md) and [Troubleshooting](TROUBLESHOOTING.md)
- [Roadmap](docs/roadmap.md): current baseline and open work
- [OTA release readiness](docs/ota-release-readiness.md): release gates and dated device evidence

Design and contracts:

- [Rodak AIoT v1 contract](docs/rodak-aiot-contract-v1.md),
  [Rodak realtime voice v1 contract](docs/rodak-realtime-voice-contract-v1.md)
- [Rodak MQTT and SD Recovery OTA](docs/mqtt-ota-sd-recovery.md),
  [production signing](docs/production-signing.md)
- [Serial provisioning](docs/serial-provisioning.md),
  [trusted server and LAN discovery](docs/trusted-server-discovery.md),
  [MQTT credential refresh](docs/mqtt-credential-refresh.md)
- [Home layout and folder design](docs/home-layout-design.md),
  [appearance customization](docs/appearance-customization.md),
  [OpenOS comparison](docs/openos-comparison.md)
- [Dependency maintenance](docs/dependency-maintenance.md)

Features and their verification boundaries:

- Voice: [assistant](docs/voice-assistant.md), [AEC and barge-in](docs/voice-aec-integration.md),
  [identity and wake word](docs/voice-identity-wake-word.md),
  [volume MCP](docs/voice-volume-mcp.md), [task retirement](docs/voice-task-retirement.md)
- MQTT effects: [volume](docs/mqtt-volume-effects.md), [light](docs/mqtt-light-effects.md)
- Media: [music playback](docs/music-playback.md), [Photos/Files browsing](docs/media-browsing.md),
  [media save](docs/media-save.md)
- Video: [WebRTC peer integration](docs/esp-peer-integration.md),
  [Camera teardown diagnostics](docs/camera-teardown-diagnostics.md),
  [video task retirement](docs/task-retirement.md)
- Appearance hardware evidence: [appearance verification](docs/appearance-verification.md)
- CI: [host CI](docs/host-ci.md), [firmware CI](docs/firmware-ci.md)
- [Historical plans](docs/archive/README.md) and [agent notes](AGENTS.md)

Each dated hardware record names the exact package that it observed. Write new evidence in the
document that owns the feature, not in this README.

## Acknowledgments

The hardware abstraction uses Espressif Board Manager and the BigSmart board definition. The UI
uses the small RodakOS-owned font subset in `components/rodakos_fonts` for Chinese glyphs and
Ionicons UI icons (MIT).

# RodakOS

RodakOS is an ESP32-S3 firmware project that turns the RYMCU BigSmart into a small "Phone OS" experiment: a 320x240 touch-first home screen, system apps, media apps, and hardware services built on ESP-IDF, LVGL, and Board Manager based board definitions.

## Current Status

Current trusted-server work starts from `7101282`; source, build and hardware
validation identities are recorded in the linked roadmap and feature documents.
Current work and dated evidence are separated in the [roadmap](docs/roadmap.md).
032 source `514ebb8c` retains the 031 voice retirement implementation and adds a test-only
USB lifecycle cycle. Test package `20261008-080207` was used for the bounded matrix;
ordinary OFF package `20261008-081054` has passed its separate package audit and guarded
Recovery/main/OTA/Home restoration. Independent review records one idle and three Listening
cycles on the test image, then a separate ordinary voice-session stop/rearm observation.
The device remains on the ordinary OFF package with its original binding and tokenVersion 4.
See [032 evidence](docs/ota-release-readiness.md#2026-10-08-voice-lifecycle-diagnostic-and-restoration-032) and
[voice task retirement](docs/voice-task-retirement.md). The 030 video record retains its own
firmware identity and limits.

- Target hardware: ESP32-S3, 16MB flash, 8MB PSRAM, ST7789 LCD, GT911 touch, PCA9557 IO expander, LEDC backlight.
- Frameworks: ESP-IDF 6.0.2 with its recommended Xtensa GCC toolchain, LVGL 9.3, `esp_lvgl_port` 2.8, local Board Manager and BigSmart board components.
- Optional USB-installed server trust now enables pinned HTTPS, MQTTS and WSS with
  a stable `.local` server name. Bounded DNS-SD address/port candidates are authenticated before
  endpoint promotion, preserving an existing device binding. Numeric routes now persist
  and are reused by every trusted transport; compact authority v3 shares the server
  trust and keeps v1/v2 read compatibility. Compact-v3 package `20261007-023218`
  passed same-port failover from an unreachable address to the genuine address,
  retained its numeric MQTTS/WSS route after restart, and passed two trusted USB
  refreshes, MQTTS/shadow/telemetry and both port migrations on COM3 with
  wake listening and no crypto allocation errors; port recovery includes a controlled
  device restart. A separate 45-second hotspot outage recovered WiFi/MQTTS and
  fresh telemetry without USB provisioning or a reboot. Two synthetic-silence WSS
  sessions also passed with MQTT and wake recovery, but reported an internal-heap
  historical minimum of 863 bytes; full audio/concurrency and soak remain open.
  New-server-address roaming and broader negative cases remain open. An earlier 006 HTTPS
  wrong-certificate test was rejected before any HTTP request. Further
  hardware evidence, WSS Host compatibility, and the remaining damaged-trust and
  power-cut gates are recorded in
  [trusted server discovery](docs/trusted-server-discovery.md).
- Display, backlight, LVGL, cached touch polling, WiFi, SD card file service, USB MSC mode, audio playback/recording, camera service, QMI8658 motion sensing, and core Phone OS navigation are integrated. Camera and display WebRTC peer services are wired through MQTT signaling; the normal command path supports camera/display mutual exclusion and explicit stop. Stream instances now revoke at connection changes and clean up outside the MQTT event callback; hardware fault acceptance remains in the roadmap.
- Five video workers now retire through an external owner after their complete body and local
  destructors return, avoiding IDF's exit-time cleanup-task creation. Serial app launch and Camera
  Home use a bounded precreated queue. The historical 030 package recorded seven correlated Stops,
  four bounded local-Camera/remote-Display order checks and one remote-pointer Home check.
  The same-boot internal minimum is 359 B; image quality, resource recovery and production
  release remain open. The three voice exits use the 031 implementation; 032 distinguishes
  test-flavor Deinit observations from ordinary OFF firmware restoration. See [task retirement](docs/task-retirement.md) and
  [voice task retirement](docs/voice-task-retirement.md).
- The native Phone Shell owns Lock Screen and Control Center overlays independently of app lifecycle, with startup-lock and gesture preferences under Settings.
- Signed appearance resources support a desktop-compiled boot logo, Home wallpaper and theme.
  Publisher trust requires physical confirmation in Settings; SD packages are trialed on the next
  boot with a 1500 ms loading acceptance budget and built-in EDIX fallback. See
  [Appearance customization](docs/appearance-customization.md) for limits and hardware gates. The
  2026-10-02 COM3 run applied revision 14 (remote Dark theme, retained wallpaper) and verified the
  next-boot trial path.
- Home resolves a versioned exact-ID layout from `home/layout`, reconciles it against the Registry in
  RAM, and supports folder browsing plus draft-only reorder/create/rename/move/dissolve commands with
  one guarded save on Done. It restores its runtime page anchor and caps the managed desktop at eight
  pages with a read-only `All Apps` slot. Stable tile shells remain allocated, while only the active
  page and its immediate neighbors retain grids and buttons. Scroll-end refreshes release far-page
  child trees asynchronously and populate in active, previous, next order; a direction is enabled
  only after its adjacent page is ready. Pending refreshes are canceled before theme rebuild or
  navigation teardown, and Home logs final internal-SRAM free space and largest free block.
- `tests/home_ui` compiles the production Home UI against LVGL 9.3's in-memory display and test
  pointer. Its tests cover tap-versus-drag suppression on one-page boundaries and real
  multi-page swipes, long press, Cancel/Done, repeated Home, theme rebuild, keyboard geometry, the
  96/97-app `All Apps` boundary, asynchronous active-plus-neighbors residency, compiled boot-image
  pixels, animation lifetime/touch restoration, Home wallpaper and unified theme colors. The current
  WSL suite reports 43 tests and 0 failures in Debug and ASan/UBSan with leak detection. Its built-in
  logo font is a host fake; this does not establish EDIX or wireless hardware acceptance.
- The dated 2026-10-01/02 protected COM3 refresh reached Home with the production app set and completed
  the local OTA confirmation. That evidence verifies six WebRTC display sessions (320x240,
  four rounds, 283 JPEG samples), camera/display mutual exclusion, remote text/control cleanup, and
  appearance revision 14. This proves the integrated paths on COM3; three-page Home turnover,
  physical page gestures, and true LVGL out-of-memory recovery remain open.
- IO10 defaults to Control Center on single click, Smart on double click, and Lock on long press; NVS custom bindings remain authoritative.
- Built-in apps currently registered: Home, Settings, Photos, Camera, Clock, Calendar, File Manager,
  Gyro, System Info, Music, Recorder, Assistant, Smart, and Wake.
- Voice sessions expose three MCP tools for setting, raising and lowering volume, with atomic
  shared configuration, bounded session deduplication and `rodakos.volume-receipt.v1` software
  receipts. The checked esp_codec_dev 1.5.7 overlay propagates driver errors without changing
  managed sources. A closed codec accepts RAM configuration without opening hardware; receipts
  do not claim persistence or physical speaker verification. MQTT shadow reports retain their
  separate evidence boundary. See [voice volume MCP](docs/voice-volume-mcp.md).
- MQTT volume effects now carry single-dispatch correlation and publish software results on
  `effects/receipt`. The device preserves bounded deduplication across same-authority reconnects
  while connection epochs cancel old queued work/results. Plain reported volume remains state
  only. See [MQTT volume effects](docs/mqtt-volume-effects.md).
- Automatic same-authority MQTT credential refresh now replaces the complete SDK client and
  checks the current persisted credentials at attachment. Old callbacks/outbox entries cannot
  enter the new generation; changed authority or unconfirmed SDK stop retain restart isolation.
  See [credential refresh](docs/mqtt-credential-refresh.md) for validation and remaining limits.
- Ordinary command ACKs and video signal/state results are bound to their original MQTT connection
  and sent without a replayable SDK outbox item. Stale queued results and late callbacks are dropped.
  Stream-instance cleanup, final remote-input grants and delayed ACK ownership now use revocable
  identities. A latest-64 command cache replays original results without repeating retained
  requests; eviction and device restart have no deduplication guarantee. See the
  [command result boundary](docs/rodak-aiot-contract-v1.md#command-results-and-replay-boundary).
- Release-soak collection now rejects missing/repeated/regressed device uptime and requires both
  queued and successful completion evidence for app exercises. These host checks do not close the
  eight-hour device gate in [OTA release readiness](docs/ota-release-readiness.md).
- Voice identity uses a bounded versioned record, an accepted revision watermark and explicit
  runtime/storage recovery states. Temporary expiry uses Unix time plus a monotonic lifetime limit;
  pending or failed recovery is never reported as a confirmed identity. Software and acoustic
  evidence remain separate; see [identity and wake word](docs/voice-identity-wake-word.md).
- Recorder and Web upload hold path-scoped FileService leases through writing and cleanup;
  Camera uses the short I/O lock and exclusive creation. Recorder and Camera report success only
  after complete writes, flush and close. Recorder library errors remain separate from save
  results, and Camera delivers completion through a revocable shared result and UI timer.
  Real SD, power-loss and hardware audio/camera gates remain open. See
  [media save boundaries](docs/media-save.md).
- The `20261006-225642` COM3 package records legacy serial provisioning and same-URL
  binding preservation; its 7,015,312-byte main image predates TLS pinning. The trusted
  transport package `20261007-010516` has a 7,081,040-byte main image; its bounded
  hardware evidence and remaining gates are recorded in
  [trusted server discovery](docs/trusted-server-discovery.md#validation).
  Numeric-route/WiFi-recovery package `20261007-020911`, built from `8188e7e`, failed
  its first USB NVS write gate. Diagnostic package `20261007-022059` passed the
  bounded USB/port/telemetry gate without reproducing the error; it is not a storage
  repair. Compact-v3 package `20261007-023218` was built from `2af9ce5` with a
  7,095,392-byte main image. Its two USB refreshes, bidirectional port recovery and
  60-second stability window passed, followed by a 45-second hotspot-outage recovery
  without provisioning or reboot, then two WSS silence sessions. Low internal-heap
  headroom remains open despite the separate two-address failure/success and
  post-restart numeric-route gate passing.
  The main application slot is 13.3125 MiB and supports SD-staged Recovery OTA from Rodak.

## Build

Activate the local ESP-IDF environment, then run any project script directly. The activator prefers
the installed ESP-IDF 6.0.2 baseline, and the build/package/flash entry scripts reject other
versions. A direct `idf.py build` assumes this 6.0.2 shell is already active. No global PowerShell
profile is required.

`main/idf_component.yml` pins the resolved direct component versions plus the Board Manager button
and camera dependencies that otherwise drift when generated configuration is recreated.
`dependencies.lock` remains the authoritative complete resolved graph and must be reviewed with any
intentional dependency upgrade.

The root CMake build automatically validates and generates the codec volume/open and I2S failure corrections after
dependency resolution. See [dependency maintenance](docs/dependency-maintenance.md) for its
source provenance, drift rejection, host tests and upgrade procedure.
The same build also applies the checked MQTT custom-event queue correction; it requires the
reviewed ESP-IDF event source and keeps managed components unchanged.

```powershell
# Activate ESP-IDF (prefer the installed 6.0.2 baseline)
. .\activate_idf.ps1

# Or pick a version, or list what's installed
. .\activate_idf.ps1 -List
. .\activate_idf.ps1 -Version v6.0.2

# First build, board changes, or after deleting generated board files
.\build_rodakos.ps1

# Daily incremental build
idf.py build

# Build the first-flash Recovery bundle and the normal Rodak OTA artifact
.\build_ota_bundle.ps1 -SigningKeyPath C:\secure\ota-private.pem `
  -VerificationKeyPath C:\secure\ota-public.pem `
  -SigningTaskNo <rodak-task> -SigningVersion <compiled-version>

# Refresh an existing Recovery-layout device, preserving NVS and OTA journal
.\flash_and_test.ps1

# Refresh, verify the complete boot handoff, and leave the device running
.\flash_and_test.ps1 -NoMonitor

# First Recovery-layout migration; intentionally clears all device state
.\flash_and_test.ps1 -Erase -NoMonitor

# Or choose a port
.\flash_and_test.ps1 -Port COM5
```

Without `-Erase`, the flash script first verifies that the installed partition table and immutable
Recovery match the package, then writes only `otadata` and `ota_0`; default NVS and the isolated OTA
journal are preserved. `-Erase` is the explicit first-migration path and writes the complete 16 MiB
image. Both paths keep the device in download mode until serial capture is ready, then verify
Recovery handoff, main-image confirmation, and Home startup. The monitor attaches with `--no-reset`,
so it cannot turn a still-unconfirmed image into an aborted rollback candidate.

Manual board regeneration is:

```powershell
. .\activate_idf.ps1
.\generate_board_config.ps1
idf.py build
```

`components/gen_bmgr_codes/` is generated by Board Manager and intentionally gitignored.
`generate_board_config.ps1` owns extension discovery, the two-pass cold bootstrap, generated-path
normalization, and reconfiguration.

App-model host tests run outside the firmware build and do not change the device partition layout.
They compile a test-only copy of the resolved `espressif/cjson` managed component used by the
ESP-IDF 6.0.2 baseline, so no host JSON package or active IDF shell is required:

```powershell
wsl -d Debian -- bash -lc '
  cmake -S /mnt/d/workspace/rodakos/tests/app_model \
        -B ~/.cache/rodakos-app-model -G Ninja -DCMAKE_BUILD_TYPE=Debug &&
  cmake --build ~/.cache/rodakos-app-model &&
  ctest --test-dir ~/.cache/rodakos-app-model --output-on-failure
'
```

The Home UI host target compiles the real `HomeApp`, `PhoneUi`, layout, theme, components, and
`SoftKeyboard` against LVGL 9.3 with an in-memory 320x240 display and LVGL test pointer:

```powershell
wsl -d Debian -- bash -lc '
  cmake -S /mnt/d/workspace/rodakos/tests/home_ui \
        -B ~/.cache/rodakos-home-ui -G Ninja -DCMAKE_BUILD_TYPE=Debug &&
  cmake --build ~/.cache/rodakos-home-ui &&
  ctest --test-dir ~/.cache/rodakos-home-ui --output-on-failure
'
```

## Project Layout

```text
main/
├── rodakos_adapters/      # Backlight, WiFi, WiFi credentials, SD card file service
├── phone_os/              # App lifecycle, services, audio, camera, time, cloud, web files
├── phone_ui/              # LVGL wrapper, fonts, themes, layouts, components, image loading
├── apps/                  # Built-in Phone OS apps
└── usb_msc_mode.cc        # Early-boot USB mass-storage mode for SD card access

components/
├── brookesia_hal_boards/  # BigSmart board definition and board-specific helper components
├── esp_board_manager/     # Board Manager framework
└── gen_bmgr_codes/        # Generated board config, ignored

docs/
├── architecture.md        # Layering and service/app model
├── firmware-download.md   # Build, flash, monitor, and esptool details
├── esp-peer-integration.md # Camera/display WebRTC peer and hardware evidence
├── media-save.md         # Recorder, camera, file writer and upload save boundaries
├── appearance-verification.md # Signed appearance resource hardware evidence
├── home-layout-design.md  # ID-based Home ordering and folder design
├── openos-comparison.md   # OpenOS research and RodakOS design decisions
└── roadmap.md             # Current baseline and near-term plan

tests/
├── app_model/             # Host-side models, lifecycle, policy and audio-volume service tests
├── voice_volume_service/  # Real voice service startup/queue/stop/reconnect with host dependencies
├── mqtt_volume_service/   # Real MQTT callback/fragment/worker/epoch and receipt publishing
├── remote_input/          # Production input grants and final operations against real host LVGL
├── display_control_ack_service/ # Complete production display ACK sender with fake peers
└── home_ui/               # Production HomeApp exercised against host LVGL 9.3
```

## Hardware Configuration

Board configuration lives in:

```text
components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart/
```

Key files:

- `board_devices.yaml`: LCD, touch, audio codecs, SD card, camera, etc.
- `board_peripherals.yaml`: GPIO, I2C, SPI, SDMMC, ADC, DVP, LEDC/RMT-level pins.
- `setup_device.c`: board-specific initialization hooks.

The project uses `partitions_16m.csv`. OTA payloads are staged on SD, so internal flash reserves an
immutable Recovery and one large main application slot:

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

Use hexadecimal partition sizes; shorthand such as `1M`/`15M` has previously caused capacity errors.

## Documentation

- [Architecture](docs/architecture.md)
- [Dependency maintenance](docs/dependency-maintenance.md)
- [Appearance customization](docs/appearance-customization.md)
- [Appearance verification](docs/appearance-verification.md)
- [Roadmap](docs/roadmap.md)
- [Firmware build and flash](docs/firmware-download.md)
- [Rodak MQTT and SD Recovery OTA](docs/mqtt-ota-sd-recovery.md)
- [BigSmart WebRTC peer integration](docs/esp-peer-integration.md)
- [OTA release readiness](docs/ota-release-readiness.md)
- [Rodak AIoT v1 contract](docs/rodak-aiot-contract-v1.md)
- [Rodak realtime voice v1 contract](docs/rodak-realtime-voice-contract-v1.md)
- [Voice assistant integration](docs/voice-assistant.md)
- [Voice task retirement and software evidence](docs/voice-task-retirement.md)
- [Music scanning, playback errors and retry](docs/music-playback.md)
- [Photos/Files scanning, image errors and retry](docs/media-browsing.md)
- [Voice volume MCP and software receipts](docs/voice-volume-mcp.md)
- [MQTT volume effects and software receipts](docs/mqtt-volume-effects.md)
- [MQTT light patches and software receipts](docs/mqtt-light-effects.md)
- [Voice AEC and barge-in integration](docs/voice-aec-integration.md)
- [Rodak identity and wake word](docs/voice-identity-wake-word.md)
- [Serial provisioning](docs/serial-provisioning.md)
- [Trusted server and LAN discovery](docs/trusted-server-discovery.md)
- [OpenOS comparison and design decisions](docs/openos-comparison.md)
- [Home layout and folder design](docs/home-layout-design.md)
- [Historical plans](docs/archive/README.md)
- [Troubleshooting](TROUBLESHOOTING.md)
- [Agent/developer notes](AGENTS.md)

The archive preserves superseded plans for traceability. Current build, protocol, runtime knowledge
and open acceptance work belong in the documents above; dated hardware evidence retains the exact
package it observed.

## Acknowledgments

Hardware abstraction is based on Espressif Board Manager and the BigSmart board definition. The UI uses the small RodakOS-owned font subset in `components/rodakos_fonts` for Chinese glyphs and Font Awesome icons.

# RodakOS Troubleshooting

This file is the single place for current build, flash, and runtime fixes. Old migration notes, quick-fix cards, and touch/Clang one-off reports have been merged here.

## ESP-IDF Environment Missing

Symptom:

```text
idf.py: command not found
```

Fix:

Use the project-local activator from the repository root, then verify the session:

```powershell
cd D:\workspace\rodakos
. .\activate_idf.ps1
echo $env:IDF_PATH
idf.py --version
```

If more than one ESP-IDF version is installed, list and select one explicitly:

```powershell
. .\activate_idf.ps1 -List
. .\activate_idf.ps1 -Version v6.0.2
```

## Board "rymcu_bigsmart" Not Found

Symptom:

```text
Board "rymcu_bigsmart" not found
```

Cause: Board Manager generation is missing, stale, or was invoked without the project extension path.

Fix:

```powershell
. .\activate_idf.ps1 -Version v6.0.2
.\generate_board_config.ps1
idf.py build
```

## Generated Paths Point To managed_components

Symptoms include `override_path` errors or CMake failing to find `setup_device.c`.

Cause: the gitignored generated component predates the current local Board Manager or IDF baseline.

Fix:

```powershell
. .\activate_idf.ps1 -Version v6.0.2
.\generate_board_config.ps1
idf.py build
```

Check these files if needed:

- `components/gen_bmgr_codes/idf_component.yml`
- `components/gen_bmgr_codes/CMakeLists.txt`

They should point to `../../components/brookesia_hal_boards`, not `managed_components`.

## Missing partitions_16m.csv

Symptom:

```text
FileNotFoundError: partitions_16m.csv
```

Fix: keep `partitions_16m.csv` in the project root. The current 16MB layout is:

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

## Partition Table Exceeds 16MB

Symptom:

```text
Partitions table occupies ... which does not fit in configured flash size 16MB
```

Fix: use exact hexadecimal sizes, not shorthand `1M`/`15M`. The current Recovery partition is
2.5 MiB and the main `ota_0` partition is 13.3125 MiB.

## Recovery Rejects The Main Image Or The Screen Stays Blank

Symptoms include:

```text
RodakRecovery: Bootloader rejected the main image; refusing an automatic retry
PhoneAppRegistry: App identity 'voice' conflicts between 'recorder' and 'assistant'
PhoneSystem: App registry validation failed
RodakOS: PhoneSystem start failed
```

An `ESP_OTA_IMG_ABORTED` entry means the main image reached its first
`PENDING_VERIFY` boot but reset again before `OtaUpdateService::ConfirmRunningImage()` completed.
It does not by itself prove that the bytes in `ota_0` are corrupt. A blank screen can also mean LVGL
started but Registry, Shell, or Home initialization failed before the desktop was created.

When Registry validation fails, inspect the preceding `PhoneAppRegistry` message. App IDs, titles,
and aliases are normalized and checked globally; a collision prevents Home from being created. The
Recorder/Assistant blank-screen regression was caused by both descriptors claiming the `voice`
alias. Recorder now uses recording-specific aliases and Assistant retains `voice`. This is a
compile-time descriptor conflict, so erasing NVS does not fix it; rebuild and flash corrected
firmware.

Build the latest package and use the guarded refresh flow on a device that already has the Recovery
layout:

```powershell
. .\activate_idf.ps1 -Version v6.0.2
.\build_ota_bundle.ps1 `
  -SigningKeyPath C:\secure\rodak-ota-release-private.pem `
  -VerificationKeyPath C:\secure\rodak-ota-release-public.pem `
  -SigningTaskNo <new-rodak-task> -SigningVersion <compiled-version>
.\flash_and_test.ps1 -Port COM3 -VerifyOnly
.\flash_and_test.ps1 -Port COM3 -NoMonitor
```

`-VerifyOnly` performs the same partition-table and Recovery hash gate without writing Flash. The
default refresh first verifies those regions again, then updates only
`otadata` and `ota_0`, preserving NVS and the isolated OTA journal. Use `-Erase` only for the first
Recovery-layout migration or when clearing all device state is intentional. The script captures the
first boot without a log gap, requires the OTA confirmation marker, and will not open a monitor after
a failed or incomplete boot. Do not use a default `idf.py monitor` while an image may still be
pending verification; it resets the target on startup. Use `idf.py -p COM3 monitor --no-reset` only
after a healthy boot.

When diagnosing an existing aborted image, read back and hash the installed image before rewriting
it. If it matches the package, restore only the package's `ota_data_initial.bin`, then capture the
entire Recovery-to-main sequence. Preserve the first failure log; a later reset replaces the useful
startup error with the generic Recovery rejection.

## Wrong ESP-IDF Or GCC Toolchain

Symptom:

Symptoms include an environment-gate failure, an unexpected compiler path, or errors caused by
building with an IDF version other than the locked 6.0.2 baseline.

Fix:

```powershell
. .\activate_idf.ps1 -Version v6.0.2
.\assert_idf6_environment.ps1
.\generate_board_config.ps1
idf.py build
```

The gate verifies the exact IDF version and the recommended Xtensa GCC from that installation's
`tools/tools.json`. The old ESP-IDF 5.5.4 Clang response-file workaround is no longer part of the
current build path.

## LCD Config Field Errors

Symptom:

```text
dev_display_lcd_config_t has no member named x_max
dev_display_lcd_config_t has no member named y_max
```

Fix: use `lcd_width` and `lcd_height` from `dev_display_lcd_config_t`.

```cpp
static PhoneUi ui(lcd_cfg->lcd_width, lcd_cfg->lcd_height);
```

## Backlight LEDC API Errors

Symptom: missing `dev_ledc_ctrl_set_brightness_percent` or invalid cast errors.

Cause: the esp-brookesia LEDC device layer exposes handles; RodakOS controls brightness through ESP-IDF LEDC APIs.

Pattern:

```cpp
auto handle = static_cast<periph_ledc_handle_t*>(ledc_handle);
uint32_t duty = (brightness * 8191) / 100;
ledc_set_duty(handle->speed_mode, handle->channel, duty);
ledc_update_duty(handle->speed_mode, handle->channel);
```

## LVGL Lock Or Blank Screen

Checks:

- `esp_board_manager_init()` must run before display handle lookup.
- `lvgl_port_init()` and `lvgl_port_add_disp()` must run before `PhoneSystem::Start()`.
- Backlight should be initialized and restored after LVGL display setup.
- LVGL uses two internal RGB565 DMA buffers of `lcd_width * 24` pixels each. On
  this 320-pixel board that is 30720 bytes total, saving 20480 bytes compared with
  the previous 40-row buffers for wake audio and authenticated MQTT. Keep the
  row-count/byte-budget startup log when investigating memory pressure; increasing
  the buffers consumes internal DMA memory, while smaller buffers split redraws
  into more flush operations.
- Display config currently uses RGB565 byte swapping through `.flags.swap_bytes = true`.

Expected order in `main.cc`:

```text
NVS
USB MSC boot gate
Board Manager
Theme and PhoneUi
LVGL port and display
Touch cached polling
Fonts
Backlight
Services
PhoneSystem
```

## Touch Not Responding

Current design: GT911 touch is handled by a cached polling bridge in `main.cc`, not by direct `lvgl_port_add_touch()`.

Why: earlier direct LVGL-task polling could hang the I2C bus on hardware without a GT911 interrupt pin.

Healthy log:

```text
Touch input registered with cached polling
```

If touch does not work:

- Check `lcd_touch` exists in generated Board Manager config.
- Check GT911 I2C address and bus health.
- Look for repeated `Touch read failed` warnings.
- Keep I2C reads out of LVGL callbacks; update the cached bridge instead.

## SD Card Or Photos Show Empty

Symptoms:

```text
sdmmc_init_ocr ... returned 0x107
Photos app shows no photos
```

Checks:

- Insert a FAT-formatted SD card.
- Put images under `/photos` or `/DCIM`; Photos also has a shallow fallback scan from root.
- Supported image formats: `.jpg`, `.jpeg`, `.png`, `.bmp`.
- FileService mounts the Board Manager `fs_sdcard` device on demand.
- USB MSC mode uses the early-boot path in `main/usb_msc_mode.cc`.

Photos/Files now show storage/service/read failures separately from a successfully empty
list. Use the visible Retry after restoring storage; Files retries the displayed folder,
and Back attempts its parent. Refresh failures discard the old list instead of allowing
stale rows to open. Image failures remain visible with a retry path. See
[media browsing](docs/media-browsing.md) for the synchronous I/O and hardware limits.

### Music reports unavailable or playback fails

Music distinguishes an empty supported library from unavailable storage or a failed directory scan.
Use **Songs → Refresh / Retry** after inserting or repairing the SD card. Failed scans clear the
old playable list; a retained filename is historical playback state, not proof that storage is
available. The scan worker does not perform SD reads from the Refresh button's LVGL callback.

WAV must contain complete 16-bit PCM mono/stereo frames with valid RIFF lengths. MP3 must contain
complete decodable frames; accepted ID3/APE metadata is separated from audio. A loading request
can still fail later, and Music displays the asynchronous error rather than a generic progress
percentage. A failed hardware resume cancels the paused worker so a new play request can retry.
See [music playback](docs/music-playback.md) for software tests and hardware limits.

### Recorder shows an error after stopping

`Start` only accepts an asynchronous recording task. Recorder shows **Saved** only after the WAV
data length and RIFF length are rewritten successfully and the file's seek, final header write,
flush and close all succeed. A read/write, focus, task, finalization or cleanup error remains visible
with its specific message and can be retried. A cancellation with a flush/close/remove failure is
also an error; it is not downgraded to **Cancelled**.

The recording path is created exclusively. A timestamp collision tries a bounded suffix; a path
lease protects the file from cooperating FileService Delete/Rename/upload operations until final
cleanup. `library_error` describes a later directory scan and does not invalidate an already Saved
recording. Check [media save boundaries](docs/media-save.md) when the list is empty while the last
save still says Saved.

### Camera capture or upload reports a file conflict

Camera capture uses a short FileService I/O lock and `WriteNewFile`; it publishes a result only after
the complete file is closed. Web upload holds a path lease through receive, flush, close and failure
cleanup. A cooperating writer on the same or a parent/child path may return a conflict; retry after
the other operation finishes. A conflict or write failure does not mean that an older successful
photo was lost. Real SD removal, slow-card behavior and hardware resource pressure still require
device validation.

## Out Of Memory Loading Images

Checks:

- Confirm PSRAM is enabled.
- Reduce very large source images.
- Check `ImageLibrary::LoadImage()` return value.
- Prefer scanned FileService paths rather than hard-coded local paths.

## WiFi Does Not Auto-Connect

Checks:

- Settings must save credentials through `WiFiConfig`.
- Auto-connect starts after `PhoneSystem::Start()`, so early boot logs may show UI before WiFi connects.
- Clear credentials from Settings or erase flash if NVS is polluted during testing.

## Audio, Assistant, Or Camera Unavailable

These services intentionally open heavy hardware paths on demand. If an app says unavailable:

- Check Board Manager device names: `audio_dac`, `audio_adc`, `camera`, `fs_sdcard`.
- Check I2C errors from codec/camera setup.
- Confirm SD card files exist for Music and Photos.
- For Assistant wake/runtime, confirm the embedded MultiNet model symbols exist, the `components/json`
  compatibility shim is present, and the log reports the `ni hao da ke` command as loaded. Idle mode
  must not show a `VoiceWs` connection; that transport opens only after "你好达克" is detected.
- If ESP-SR 2.2.x fails to link with `undefined reference to '_ctype_'` on IDF 6, keep the project
  linker alias `_ctype_=_ctype_b+127` in `main/CMakeLists.txt`; do not replace it with a C pointer
  variable or patch the prebuilt library under `managed_components/`.

If GC0308 detection succeeds but camera startup reports `no mem for CAM DVP DMA receive buffer`
and `Failed to start camera stream: Not enough space`, check contiguous internal DMA SRAM, not
total free heap or PSRAM. The ESP32-S3 DVP driver copies through an internal receive ring even
when the full image buffers are in PSRAM. Its default 32768-byte limit requires a 30720-byte
contiguous allocation for 320x240 RGB565; the resident voice/network runtime can leave only a
20480-byte block.

Keep `CONFIG_CAM_CTRL_DVP_DMA_BUFFER_SIZE=8192` in both `sdkconfig` and `sdkconfig.defaults`.
That supported setting uses a 7680-byte receive ring at the current preview resolution. Rebuild
and verify on hardware with wake monitoring enabled: `Starting camera stream` reports the DMA
budget, and `Camera first frame ready` confirms actual reception. Reopen Camera after a voice
interaction and check sustained preview for DVP overflow/dequeue errors; a smaller ring increases
the frequency at which the driver's copy task must run.

If the voice WebSocket reaches Rodak but the handshake returns HTTP 401 with an expired-device-token
reason, the endpoint is reachable but its cached AIoT credential is no longer valid. The current
Rodak server issues 600-second tokens. New voice interactions verify the token's advertised
`expiresIn` and refresh it using the existing paired device secret before opening the stream.
Freshness is tracked with a monotonic clock in RAM and is re-established after each boot. A rejected
refresh ends the attempt without opening a WebSocket or starting a new pairing request.

MQTT refreshes bootstrap before its first connection after boot, so a cached broker address from a
previous LAN can be replaced by the configured server's current address. Verify `MQTT health:
connected=1`, `Realtime voice session ready`, and `Sent speech input start`. A serial simulated wake
proves connection setup, while acoustic wake and a complete spoken answer require a real-person test.

### Volume is reported but speaker behavior differs

Reported volume is the shared runtime configuration. A closed codec accepts changes without
opening audio hardware; application is deferred until the next playback open. If the codec API
reports a failed write while open, the shared output and playback/UI caches retain their prior
values. Failure of the initial volume call also fails and cleans up the open attempt for retry.

The pinned codec setter is corrected through the project's checked build overlay to propagate
driver errors and preserve its cache on failure. If configuration reports `Codec volume overlay
refused`, resolve the source/version mismatch using [dependency maintenance](docs/dependency-maintenance.md);
do not bypass the check or edit the managed source. A successful return still does not prove an
I2C write or audible output. Check codec/I2C logs and playback on hardware before
claiming that result. An ordinary MQTT shadow report has no volume effect ID or applied desired
revision and cannot acknowledge a specific Agent Runtime effect. See the
[shadow contract](docs/rodak-aiot-contract-v1.md#volume-configuration-and-evidence).

## USB Disk Mode

USB disk mode is not a normal app runtime state. Settings requests a one-shot boot flag, then the next boot enters TinyUSB MSC before the normal UI and services start.

If it does not appear on the host:

- Reboot after enabling the mode.
- Check SD card presence.
- Confirm USB cable supports data.
- Use the board's MSC startup button path only if that hardware input is configured.

## MQTT Keeps Retrying While Voice Works

For a device provisioned with server trust, first check the pinned stable `.local` name and
HTTPS/MQTTS listeners in Rodak. Firmware must advertise `server_trust:1` in its provisioning READY
line before the desktop sends a certificate. DNS-SD `_rodak._tcp` results are candidate routes;
a matching server ID in TXT does not bypass TLS. Failed TLS/authentication preserves the existing
binding and does not justify unbinding or replacing its secret. See
[trusted server discovery](docs/trusted-server-discovery.md) for diagnosis and evidence boundaries.

An unreadable or missing authority after the pin latch is set fails closed. Normal network
retries cannot repair that record, and explicit physical recovery remains open; do not erase NVS
or accept a new key as an automatic workaround. Independent numeric address/port candidates are
implemented and host-verified. Package 009 also passed same-port unreachable→genuine address
selection and numeric MQTTS/WSS after restart; scoped/link-local IPv6 remains
unsupported. Real subnet changes selected through USB WiFi configuration have separate 006
evidence. Package 009 also recovered from a 45-second known-hotspot outage without USB provisioning
or a reboot. New-server-IP roaming, wider candidate/AP failures and physical power-cut gates remain open.
Preserve NVS and use a reader supporting the stored authority version: 006 cannot read v2,
and 007/008 cannot read compact v3. The outage evidence below predates pinning.

As of 2026-09-28, TCP transport failures are counted independently from authentication rejection. After three consecutive TCP failures, the MQTT worker may refresh bootstrap configuration, with at least 60 seconds between TCP recovery attempts. Successful MQTT connection clears the failure count. Authentication rejection retains priority over TCP recovery.

Voice activity defers applying refreshed settings or a required safety restart; pending snapshots return to the worker loop so waiting for voice to finish does not block queued MQTT messages and telemetry. The bootstrap HTTP call itself remains synchronous. Session identity/outbox changes still use the existing restart policy once voice is idle. Rebinding a device also restarts the MQTT service if unbinding previously stopped it.

Historical baseline (2026-09-28): package `build/packages/ota/20260928-072926` passed 197 app-model
host tests, an ESP-IDF 6.0.2 build, and a COM3 protected non-erasing refresh. Main SHA-256:
`a006b10af123feae9aceae4c2516d01afa507a2d49e22f10f373e03c35530740`. It is retained for diagnosing
that run; use the current signed package and [appearance verification](docs/appearance-verification.md)
for the 2026-10-01/02 device baseline.

Hardware evidence: a 75-second local service outage produced one TCP recovery schedule and one bootstrap attempt, then MQTT reconnected after service restoration and telemetry resumed, without reset/panic/watchdog. A subsequent USB-injected silent wake/stop session kept MQTT connected after voice ended. This verifies connection lifecycle, not acoustic wake, speech quality, or speaker output. Stale-address changes and simultaneous HTTP/voice races remain covered by policy tests and review rather than this hardware fault injection.

## Signed OTA release work

The release work introduces manifest v2 and a configurable RSA-2048 public key. Both firmware
builds need the same `RODAK_OTA_PUBLIC_KEY`; an unconfigured build refuses OTA authentication.
Use the signing arguments in `docs/firmware-download.md`. Existing immutable Recovery needs a wired
migration; changing manifest fields cannot upgrade its verifier. No production key is embedded by
default, and a test key does not establish release readiness.

Board Manager's default recursive scan does not reach
`components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart` on a clean checkout.
`generate_board_config.ps1` now passes the supported `--customer-path` explicitly, then normalizes
generated paths and reconfigures as before. Do not repair missing `g_esp_board_devices` linker
symbols by adding handwritten board tables.

The original transitive versions of `esp_sccb_intf`, `tinyusb`, and `usb_host_uvc` are now pinned in
`main/idf_component.yml`, preventing a fresh IDF install from silently upgrading them. Release host
checks run under Linux with `tools/run_release_host_checks.sh`; install CMake, Ninja, a C++ compiler,
`libmbedtls-dev`, and Python cryptography. The script uses a persistent cache directory instead of
WSL's temporary directory, which can disappear when the distro stops.
Set `RODAKOS_IDF_PATH` (or `IDF_PATH`) to the Linux-visible ESP-IDF 6.0.2 source tree for the MQTT
event-overlay checks, for example `/mnt/c/esp/v6.0.2/esp-idf` in this Windows/WSL workspace.

Test-only one-shot resource failures exercise cleanup at named boundaries. They do not demonstrate
arbitrary LVGL heap exhaustion recovery. Keep that gate, real power interruption, the signed COM13
migration, and the eight-hour release soak open until their evidence is captured in
`docs/ota-release-readiness.md`.

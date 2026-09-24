# Rodak MQTT And SD Recovery OTA

RodakOS uses Rodak's unified MQTT v2 bootstrap for device telemetry and OTA coordination. The
firmware image is staged on the SD card, while an immutable recovery application writes the image
to the single large internal application slot.

## Capacity Model

The 16 MiB flash layout is:

| Partition | Offset | Size | Purpose |
| --- | ---: | ---: | --- |
| `nvs` | `0x9000` | `0x6000` | Device and application settings |
| `otadata` | `0xF000` | `0x2000` | Boot selection and rollback state |
| `phy_init` | `0x11000` | `0x1000` | PHY calibration |
| `ota_state` | `0x12000` | `0x6000` | Isolated OTA recovery journal |
| `recovery` | `0x20000` | `0x280000` | Immutable factory recovery image |
| `app` | `0x2A0000` | `0xD50000` | Main RodakOS image (`ota_0`) |
| `coredump` | `0xFF0000` | `0x10000` | Crash diagnostics |

The main slot is 13.3125 MiB. Release images should remain below 12.5 MiB so validation and future
image-format overhead retain useful margin. A 10 MiB application image fits; a 16 MiB application
image does not.

## Bootstrap And MQTT

RodakOS declares the BigSmart device's autonomous product identity with the product key
`rymcu-bigsmart` and protocol marker `rodak-aiot` (version 1). The Board Manager hardware
discriminator remains `board.type = rymcu_bigsmart`; it is deliberately separate from the cloud
product key. The request also includes the device MAC and stable client UUID. RodakOS sends only
this canonical AIoT identity; any compatibility translation for other firmware belongs to a Rodak
server adapter.

The firmware now performs the autonomous onboarding lifecycle against the configured server:

```text
GET  /api/v1/aiot/devices/bootstrap
POST /api/v1/aiot/devices/register
POST /api/v1/aiot/devices/activate
POST /api/v1/aiot/devices/auth/token
```

The generated device secret is persisted before the first register request, so a reset between
register and token exchange can safely retry with the same credential. The returned access token is
used as the MQTT password and HTTP Bearer credential. `unifiedMqtt` and `mqttConnectInfo` are both
accepted for the broker/topics payload, with the server origin and standard device topics used as
fallbacks. RodakOS does not call legacy voice/bootstrap paths or persist a legacy voice-websocket
credential set. AIoT and MQTT credentials are committed under a pending marker; boot ignores a
candidate pair left incomplete by a reset and retries enrollment.

The configured bootstrap endpoint returns `unifiedMqtt` v2 with:

- broker address, port, username, JWT password, keepalive and device key;
- OTA HTTP base URL and the same JWT as the HTTP Bearer credential;
- complete telemetry, shadow, OTA, command and PC status topics.

RodakOS stores these values in the `unified_mqtt` NVS namespace. ESP-MQTT connects after WiFi gets
an address, publishes telemetry every 30 seconds, publishes the reported shadow, subscribes to the
desired shadow and OTA notification topics, and relies on ESP-MQTT auto-reconnect.

On the BigSmart board, telemetry includes the board-backed `battery` percentage and `charging`
state when the ADC readings are valid. The battery divider is sampled from ADC2 CH0 (GPIO11)
through the board's 2:1 divider; the charge detector is ADC1 CH2 (GPIO3), active below the
charging threshold. If an ADC read fails, its field is omitted rather than reporting a guessed
value. `wifi_rssi`, `volume`, firmware and health counters remain part of the regular report.

MQTT is currently plain TCP on the local network. Do not expose port 1883 beyond the trusted LAN.

## OTA State Machine

```text
MQTT notify
  -> request HTTP download ticket
  -> request canonical manifest
  -> back up the running image to SD
  -> stream artifact to pending.bin.part
  -> verify size and SHA-256, fsync, rename to pending.bin
  -> persist pending in ota_state
  -> reboot into recovery
  -> verify SD image again
  -> esp_ota_begin/write/end into ota_0
  -> persist ready_to_boot and boot ota_0
  -> main app health confirmation
  -> persist confirmed
  -> MQTT connected confirmation
  -> promote SD backup and report HTTP result
  -> persist report_acknowledged
  -> clear the completed journal task
```

If the new main image resets before health confirmation, the ESP-IDF rollback path returns to the
factory recovery image. Recovery persists `restoring` before touching `ota_0`, restores
`installed.bin`, then advances through `rollback_ready` and `rollback_booting`. A power loss in any
of those phases either repeats the restore or resumes the boot handoff. The restored main image marks
the rollback as confirmed and reports the failed task to Rodak.

Local application health and Rodak connectivity are deliberately separate gates. Reaching the end
of local service/UI startup cancels ESP-IDF bootloader rollback and persists `confirmed`; a later
MQTT connection completes server-side confirmation, promotes `pending.bin` to the installed backup,
and reports success. A temporary LAN outage therefore does not roll back an otherwise healthy
firmware, and the previous SD backup remains available until connectivity returns.

Successful local confirmation emits `OtaUpdate: Local boot confirmation complete`. The wired flash
script requires this marker before it may attach an interactive monitor. The monitor uses
`--no-reset`; resetting a `PENDING_VERIFY` image before this marker would make the bootloader mark it
aborted on the next boot.

Result delivery retries transport failures with capped exponential backoff. A terminal 4xx business
rejection ends the local task so one deleted server task cannot poison all future OTA work. After an
HTTP acknowledgement, `report_acknowledged` is persisted before cleanup; cleanup retries never
repeat an already acknowledged result. Rodak also treats duplicate terminal reports idempotently to
cover a lost HTTP response or a reset between acknowledgement and journal persistence.

The recovery journal is isolated from default NVS and stored as alternating A/B blobs with a schema
version, generation number and CRC32. Recovery selects the newest valid generation and fails closed
when neither copy is valid. It never erases default NVS or silently repairs a damaged OTA journal.
Journal wire schema v1, its phase codes, record size and field limits are part of the immutable
Recovery ABI. Normal OTA images must continue writing v1; changing it requires another wired
migration that replaces both Recovery and the main application. The package manifest records the
required `otaJournalSchemaVersion`.

## MQTT Worker Resources

MQTT reserves one 6 KiB internal-SRAM worker before local wake monitoring starts.
Bootstrap, credential refresh, connected initialization and message processing reuse this
worker; telemetry timers only schedule work. NVS access requires an internal stack.
Incoming messages use an eight-entry queue with explicit overflow diagnostics, while
connection and refresh work use separate coalesced flags. PUBACK handling remains in the
MQTT callback so a worker waiting for an acknowledgement cannot block its delivery.

A WiFi IP address alone does not prove MQTT recovery. Check `Unified MQTT connected`,
incoming PC status and the Rodak device's new connected/shadow events plus at least two
telemetry reports. The previous per-event 6 KiB task allocation could silently fail when
the largest free internal block was only 5 KiB, despite saved WiFi connecting successfully.
Worker reservation failures and stack high-water marks are now logged. This does not
establish memory headroom for OTA's separate download/report tasks or active AEC.

The main firmware disables `ESP_WIFI_IRAM_OPT` and `ESP_WIFI_RX_IRAM_OPT` to return
shared SRAM to runtime allocations. IDF documents more than 27 KiB of combined IRAM
savings at the cost of peak WiFi throughput. That initial MQTT fix kept static RX/TX
buffers and the receive BA window unchanged; the subsequent
[voice session memory work](voice-session-memory.md) reduces static TX to 8.
Reserving the MQTT worker alone was insufficient: WiFi logged
`mem fail` / `m f null` and disconnected after the first telemetry report. The serial
stability checker treats both warnings as allocation failures.

Keep internal heap and worker stack diagnostics in the serial `MQTT health` line. Battery and
charging are declared read-only properties in the `rymcu-bigsmart` thing model; adding any other
undeclared telemetry fields causes Rodak validation warnings.

The 2026-09-16 COM3 package `20260916-115916` passed the protected non-erasing
refresh and boot gates. Rodak observed a new connection at 12:01:08 CST, a reported
shadow and subsequent 30-second telemetry. Serial logs show incoming PC status,
internal free space around 18 KiB, a 10 KiB largest block and 3,852 bytes of minimum
MQTT worker stack headroom. Evidence is in `build/mqtt-final-flash.log` and
`build/logs/mqtt-final-soak.log`. These are idle-network checks, not active AEC,
OTA download, credential-rotation or peak-throughput validation.

## Build Artifacts

Activate ESP-IDF, then run:

```powershell
. .\activate_idf.ps1 -Version v6.0.2
.\build_ota_bundle.ps1
```

The script regenerates the Board Manager component before every package build so stale generated
code cannot enter the Recovery or OTA artifacts.

The package contains:

- `rodakos_sd_recovery_merged.bin`: exactly 16 MiB first wired migration image, flashed at `0x0`;
- `rodakos.bin`: the only image uploaded to a normal Rodak OTA package;
- `rodakos_recovery.bin`, bootloader and partition table for factory servicing;
- `manifest.json` with the main image size and SHA-256.

Do not use root `idf.py flash` or `idf.py app-flash` with this layout. ESP-IDF selects the factory
partition for those targets, while the root project builds the main image for `ota_0`.

## First Migration

Changing from the former factory/storage table cannot be done by the old application. Back up any
required settings, then perform a wired erase and flash of the merged image. The erase intentionally
removes the old NVS and internal FAT contents.

## Security Boundary

SHA-256 detects corruption but does not authenticate who produced an image. The current phase must
remain limited to a trusted local network. Production rollout requires signed application images
and signature verification in recovery before `esp_ota_begin()`.

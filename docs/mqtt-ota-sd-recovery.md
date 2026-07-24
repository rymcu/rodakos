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

The device posts its system information to `/xiaozhi/ota/`. Rodak returns `unifiedMqtt` v2 with:

- broker address, port, username, JWT password, keepalive and device key;
- OTA HTTP base URL and the same JWT as the HTTP Bearer credential;
- complete telemetry, shadow, OTA, command and PC status topics.

RodakOS stores these values in the `unified_mqtt` NVS namespace. ESP-MQTT connects after WiFi gets
an address, publishes telemetry every 30 seconds, publishes the reported shadow, subscribes to the
desired shadow and OTA notification topics, and relies on ESP-MQTT auto-reconnect.

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

## Build Artifacts

Activate ESP-IDF, then run:

```powershell
. .\activate_idf.ps1 -Version v5.5.4
.\build_ota_bundle.ps1
```

The script reuses an existing Board Manager generation. Pass `-RegenerateBoardConfig` after changing
board YAML/defaults or when the generated component is missing.

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

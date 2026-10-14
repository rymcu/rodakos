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

The canonical identity and owner-confirmed onboarding flow are defined in
[Rodak AIoT v1](rodak-aiot-contract-v1.md). In summary, RodakOS uses
`productKey = rymcu-bigsmart`, `protocol = rodak-aiot`, and Board Manager's
separate `board.type = rymcu_bigsmart`. It calls `bootstrap`, then
`binding/request` and `binding/requests/{id}/status`; only `confirmed` or
`approved` returns the access token and unified MQTT configuration. A bound
device may refresh through `auth/token` with its persisted secret. The older
`register`/`activate` endpoints are server compatibility paths, not the
RodakOS onboarding sequence.

The returned access token is used as the MQTT password and HTTP Bearer
credential. `unifiedMqtt` and `mqttConnectInfo` are accepted for the
broker/topics payload, with the server origin and standard device topics used
as fallbacks. AIoT identity, MQTT, and realtime voice values are committed under
a pending marker; boot ignores a candidate pair left incomplete by a reset and
retries the binding/refresh flow.

RodakOS stores MQTT values in the `unified_mqtt` NVS namespace. ESP-MQTT
connects after WiFi gets an address, publishes telemetry every 30 seconds,
publishes the reported shadow, subscribes to desired shadow, OTA notification,
command, and PC status topics, and relies on ESP-MQTT auto-reconnect.

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
  -> atomically write the RSA signature to pending.sig
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

## OTA Image Authentication

Signed OTA uses `manifestVersion: 2`, `signatureType: "rsa2048-sha256"`, and exactly 512 lowercase
hex characters in `signatureValue`. RSA-2048 PKCS#1 v1.5 signs the SHA-256 digest of these UTF-8
lines, including the final newline:

```text
rodakos-ota-v2
rymcu-bigsmart
ota_0
rsa2048-sha256
<taskNo>
<version>
<fileSize as an unsigned decimal integer>
<lowercase SHA-256>
```

Task and version fields reject control characters, exceedance of journal limits, and embedded
newlines. Sizes must be integral and fit `ota_0`. The main application verifies the signature before
backing up or downloading an image. It writes exactly 512 signature bytes to `pending.sig.part`,
checks flush/close, and renames it to `pending.sig` before persisting the journal. The staged handoff
verifies both files again. These sidecars preserve the journal v1 layout and phase codes.

Recovery verifies the signature and the entire candidate file before setting `applying` and before
`esp_ota_begin()`. Rejection from `pending` records a failure and leaves `ota_0` untouched. Rejection
when resuming an interrupted `applying` phase restores the installed backup, since an earlier boot
may already have erased the main slot. The write pass rehashes the bytes it actually flashes and
rejects changes before `esp_ota_end()` can accept the image. Rollback uses the backup hash anchored in
the internal journal; the SD card alone cannot replace that hash.

Configure both firmware builds with `-DRODAK_OTA_PUBLIC_KEY=<absolute PEM path>`. This embeds only an
RSA-2048 public key and its SHA-256 fingerprint. An unconfigured development build boots but rejects
all signed OTA. There is no default development trust root in source. Keep private keys outside the
repository; test keys generated during development belong in ignored build directories and never
qualify a release as production-ready.

`tools/sign_ota_manifest.ps1` verifies that its private key matches `VerificationKeyPath`, checks the
actual image bytes, signs the metadata, and verifies the result. Packaging requires matching trust
fingerprints in the main image and Recovery, matching compiled/application versions, a valid
signature, and no embedded fault-injection marker. The Rodak manifest endpoint must deliver the
same `taskNo`, `version`, size, checksum, and signature; changing a task requires signing again.
Rodak attaches that signature per task through `server:attachOtaTaskSignature` and does not notify an
unsigned v2 task. A real Rodak OTA with this interface passed in 099.

Installing this verifier requires a wired Recovery migration even though the journal ABI is still
v1. Existing unsigned Recovery cannot gain authentication through a main-only OTA. Key rotation also
requires a new wired Recovery migration. Preserve a verified full flash backup before migration;
never infer trust from a manifest version field alone.

For release fault tests, see [OTA release readiness](ota-release-readiness.md). Software resets
exercise state-machine resumption but do not prove filesystem or flash behavior under power loss.

## MQTT Worker Resources

MQTT reserves one 8 KiB internal-SRAM worker before local wake monitoring starts.
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

Trusted TLS measurements used 5244 bytes of the former 6144-byte worker stack,
leaving only 900 bytes. The 8 KiB worker restores margin while remaining in internal
SRAM for NVS. It is separate from ESP-MQTT's 6 KiB MQTTS task, whose own minimum
stack is logged from SDK callbacks. Two 24-row RGB565 display DMA buffers save
20 KiB compared with the original 40-row configuration on this 320-pixel board.

The main firmware disables `ESP_WIFI_IRAM_OPT` and `ESP_WIFI_RX_IRAM_OPT` to return
shared SRAM to runtime allocations. Static WiFi TX buffers remain at 8, while RX
and the receive BA window remain sized for the PSRAM configuration. This is a
throughput/memory tradeoff; it is not a protocol compatibility setting.

Keep internal heap, DMA free/largest block and worker stack diagnostics in the serial
`MQTT health` line. Mbed TLS allocating its I/O buffers in PSRAM does not remove
the hardware SHA/AES drivers' internal DMA allocations; crypto allocation failures
invalidate stability acceptance even when some telemetry arrives. Battery and
charging are declared read-only properties in the `rymcu-bigsmart` thing model; adding any other
undeclared telemetry fields causes Rodak validation warnings.

The MQTT gate is complete only when the serial log shows `Unified MQTT connected`,
incoming PC status, at least two telemetry reports, and a reported shadow. Keep
the `MQTT health` line's internal free heap, largest block, and worker stack
headroom in captured logs. This gate does not establish memory headroom for OTA
download/report tasks, active AEC, credential rotation, or peak throughput.

## Build Artifacts

Activate ESP-IDF, then run:

```powershell
. .\activate_idf.ps1 -Version v6.0.2
.\build_ota_bundle.ps1 -SigningKeyPath C:\secure\ota-private.pem `
  -VerificationKeyPath C:\secure\ota-public.pem `
  -SigningTaskNo <rodak-task> -SigningVersion <compiled-version>
```

The script regenerates the Board Manager component before every package build so stale generated
code cannot enter the Recovery or OTA artifacts.

The package contains:

- `rodakos_sd_recovery_merged.bin`: exactly 16 MiB first wired migration image, flashed at `0x0`;
- `rodakos.bin`: the only image uploaded to a normal Rodak OTA package;
- `rodakos_recovery.bin`, bootloader and partition table for factory servicing;
- `manifest.json` with authenticated main-image metadata and `ota-public.pem` with its verification key.

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

### 2026-10-12 End-to-end Rodak OTA (098)

Three defects blocked a real Rodak OTA, and all are fixed. (1) The device received `ota/notify`, but the
10 KiB internal-RAM `ota_download` stack could not be allocated (largest block 8,192 bytes) and the caller ignored
the failure, so the notification vanished. The task stack is now in PSRAM, and every flash or NVS step goes to the
`app_main` internal stack, which a task notification wakes at once. A static 10 KiB stack was rejected because it
broke the remote Camera 8 KiB gate. (2) OTA HTTP used the 10-minute AIoT access token and never renewed it, so
requests got 401 after the device had run for ten minutes. Each request now renews a token near expiry, and a 401
invalidates it first. (3) The 6 KiB `ota_report` task overflowed while it refreshed credentials after a 401; the
refresh now runs on the internal stack. The running-image backup to SD dropped from about 225 s to 14 s.
Ordinary package `20261012-192434` (main SHA-256 `6750ed82…`) updated to dev.2 through Rodak about 20 minutes
after boot with an expired token; the task `succeeded` with no 401, overflow or panic (serial SHA-256 `d7d2cdfb…`).
Remote Camera still keeps 8,192-byte internal/DMA largest. Rodak's stale `firmwareVersion` after an OTA and its
missing per-task signing API stayed open after 098; 099 below closes both. Release stays **NO_GO**.

### 2026-10-12 Rodak per-task signing and firmware version (099)

Rodak closes both server gaps from 098 ([rymcu/rodak `d7c41b3e`](https://github.com/rymcu/rodak/commit/d7c41b3e),
[`dd8bad4c`](https://github.com/rymcu/rodak/commit/dd8bad4c)). (1) Rodak used to set a device's `firmwareVersion` only
at registration. It now reads the `firmware` field that telemetry and the reported shadow already carry, so an OTA,
a Recovery rollback or a wired flash updates the record without a new registration. (2) The signature covers
`taskNo`, and Rodak assigns `taskNo` only when it creates the task. The new Rodak IPC
`server:attachOtaTaskSignature` accepts the manifest that `tools/ota_security.py sign` wrote for that `taskNo`.
Rodak checks the task, version, size and SHA-256 against the release snapshot. With the public key it also
verifies the signature. Then it stores the signature on the task and merges it into the manifest response.
The private key stays with the offline signing tool. A `manifestVersion: 2` task without an attached signature
stays `pending`: Rodak does not publish `ota/notify` for it, and its manifest request returns `409`.

Real OTA with no database write: on package `20261012-194546`, Rodak first held task
`ota_1791813228456_b1864b48` as unsigned. After Rodak attached and verified the signature, the campaign
started again and the device updated from 0.1.2-dev.1 to dev.2. The task `succeeded`, Rodak recorded `firmwareVersion`
dev.2 from telemetry, and the serial log has no panic (serial SHA-256 `a0b09508…`). Before the OTA, the first
telemetry after the Rodak restart also corrected the stale dev.2 record left by 098 back to dev.1. OTA power-cut,
concurrent TLS/NVS failures, production signing and the eight-hour qualification stay open. Release stays
**NO_GO**.

### 2026-10-13 Main-side OTA interruption matrix (100)

All six Main-side reset boundaries of the interruption matrix passed on hardware, one Rodak OTA each
(dev.1 → dev.2, signed per task, no database write). Each trial is a one-shot software reset
(`esp_restart`), not a power cut. Every task ended `succeeded` on dev.2, and no serial log has a panic.

| Trial | Boundary | Result |
| ----- | -------- | ------ |
| m100a | `after_download_fsync` | Reboot on dev.1 without Recovery. After the Rodak fix below, the resent task downloads again and updates. |
| m100b | `after_pending_sidecar` | Reboot on dev.1 without Recovery. The device has no journal entry and takes the resent task again. |
| m100c | `after_pending_journal` | Reboot on dev.1, resume the staged acknowledgement, then Recovery writes dev.2. |
| m100e | `after_boot_confirmation` | First dev.2 boot resets after the journal records `confirmed`. The next boot keeps it and reports. |
| m100f | `after_result_http_ack` | Rodak recorded the result. The next boot clears the record, and Rodak does not count the task twice. |
| m100g | `after_report_acknowledged` | The next boot clears the acknowledged record and its signature sidecar. |

m100a found one server defect. When the device resets before its journal records a task, it forgets the
task, and Rodak resent only `pending` or `notified` tasks, so the task stayed `in_progress` forever.
Rodak now resends an in-progress task when the device subscribes again
([rymcu/rodak `bfdbe0c2`](https://github.com/rymcu/rodak/commit/bfdbe0c2)). The device journal still
rejects a duplicate of a task that it has staged, installs or must report.

m100d is void: the serial capture closed when USB re-enumerated at the Recovery restart, so it has no
evidence. The capture now reopens the port, and m100e repeats the boundary. Serial SHA-256 values:
m100a `218ee072…`, m100a resume `c3d50adf…`, m100b `eab4d51c…`, m100c `e89d0c01…`, m100e `2148e62e…`,
m100f `ff4ac133…`, m100g `61bf3a77…`. The device is back on the normal dev.1 package `20261012-192434`.

The Both and Recovery rows stay open. They need a Recovery fault build, and the flash script refuses a
Recovery that differs from the immutable baseline. Real power cuts during download, FAT flush or rename
and flash erase/write need a controlled power fixture. Release stays **NO_GO**.

### 2026-10-13 Recovery-side OTA interruption matrix (101)

All 15 Both and Recovery boundaries passed on hardware, so every software-reset row of the
interruption matrix now has hardware evidence. Each boundary ran one Rodak OTA (signed per task, no
database write) with a temporary fault-injection Recovery on the test device. Each trial is a one-shot
software reset, not a power cut. No serial log has a panic.

- Before the trials, esptool read back and saved the full 2.5 MiB Recovery partition. The trial script
  erases the partition, writes the fault Recovery, and returns `ota_0` to the normal dev.1 package
  `20261012-192434`. esptool verifies all three regions.
- Image write (r101a–r101f): `after_applying_state`, `before_image_erase`, `after_image_erase`,
  `during_image_write`, `after_image_write` and `after_ready_to_boot` all reset once. Recovery then
  verified the candidate again and wrote it again, or (`after_ready_to_boot`) resumed the first boot.
  All six tasks `succeeded` on dev.2.
- Journal A/B (r101g–r101i): Recovery wrote the journal at `before_journal_set`, `after_journal_set`
  and `after_journal_commit` and reset there. The next boot loaded the newest valid generation and
  finished the update. All three tasks `succeeded`.
- Restore (r101j–r101o): a dev.2 package with one flipped byte in flash-mapped code keeps its size,
  descriptor and signed SHA-256 manifest, so it passes the SD checks. `esp_ota_end` then rejects it
  with `ESP_ERR_OTA_VALIDATE_FAILED`, and Recovery restores dev.1. The six restore boundaries reset
  once each, and the restore always finished on dev.1. Rodak recorded each task as `failed` with
  `RECOVERY_FLASH_FAILED` ("新固件启动失败，上一版本已恢复"). A run with the baseline Recovery and
  the same package showed the same rollback before the trials.

After the trials, the baseline Recovery (`ffa412eb…`) went back and the partition read back is
byte-identical to the saved copy (SHA-256 `c69e24d6…`). `flash_and_test.ps1 -VerifyOnly` passes, and
the device runs the normal dev.1 package. Serial SHA-256 values are in `ota-release-readiness.md`.

Real power cuts during download, FAT flush or rename, and flash erase or write still need a controlled
power fixture. Release stays **NO_GO**.

### 2026-10-13 TLS/NVS concurrent failures and Camera-time AES (102-104)

Four one-shot release-test faults passed on hardware: a TLS read failure in the middle of an OTA download,
a TLS failure in a credential refresh, an OTA journal that was written but reported as failed, and MQTT
credentials that were written but reported as failed. ESP-IDF 6 NVS writes on `nvs_set_*` and
`nvs_commit` does nothing, so "save failed" can mean "written, reported as failed".

The work fixed three defects. A `kPending` journal that NVS wrote but reported as failed left the device on the
old image until a reboot (`e6502cd`). The retry then failed to create a second 6 KiB task when internal RAM was
fragmented, so the retry worker now runs the handoff on its own stack (`00ffd9a`). The report loop for a result
that could not be saved had no limit while it held the SD lock. It now stops after 2 minutes (`e6502cd`).

During a remote Camera stream, AES failed 316 times. WebRTC encrypts SRTP in place in PSRAM, and IDF took the
bounce buffer for an unaligned PSRAM output from internal DMA RAM. `3def20e` takes that bounce from 64 B-aligned
PSRAM instead. After the fix, a 74 s Camera stream had zero AES allocation failures.

MQTT reconnect cannot run during Camera: an MQTT disconnect revokes the video stream lease by design. A voice
wake during Camera failed to create the WebSocket task (internal heap 3.3-3.5 KB free, largest 1,600 B). That
is an open Camera-time internal-memory defect. Release stays **NO_GO**.

### 2026-10-14 Voice takes priority over the remote Camera (105)

`78a5c82` stops the remote Camera stream before a voice session opens its WebSocket. During a Camera
stream the largest internal block is about 1 KB, so the voice WebSocket task could not start. The new
`SetBeforeTransportCallback` runs after capture starts and before the transport opens.

On hardware (105a, 105e), the stop took 320-440 ms. The largest internal block then rose to 6.1-6.6 KB.
The voice WSS connected in all four rounds. AES failures, WebSocket task failures and panics were zero.

One residue stays open. After the first voice session that follows a Camera session, the largest block
stays at 6,144 B instead of 8,192 B. An 84 B used block splits the 8 KiB segment. Camera alone, voice
alone (105d) and voice before Camera (105e) do not leave it.
The local Camera app is not covered. By the code, it holds exclusive audio focus, so voice cannot get
focus there (not tested on hardware).
Release stays **NO_GO**.

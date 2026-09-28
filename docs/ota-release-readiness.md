# OTA Release Readiness

The existing Home, voice, media, and MQTT functional gates are accepted as completed by the user.
This document tracks only the new signed-release, interruption, and resource-failure work. Passing
software tests does not close physical power-loss or full heap-exhaustion gates.

## Current evidence

On 2026-09-29, ESP-IDF 6.0.2 builds succeed for normal and fault-injection firmware. The normal
application is 6,597,952 bytes and Recovery is 338,592 bytes, both within their partitions.
The development-signed package is `build/packages/ota/20260929-000449`; all test options are disabled.
Main SHA-256: `a9f8f935d7e9e184ef42342bdcc7ed3a5ef911f8eb408f615367ebb073b99d75`.
Recovery SHA-256: `24dfbe4096e89d884e51e85ff8ad76c06ef8b666e37bbc426e9236113af0eb14`.

There are 200 app-model, 18 Home UI, 11 signature/journal, 3 one-shot fault, and 5 production
Recovery state-machine tests, all passing. ASan/UBSan with leak detection passes for every C++ target.
Eight Python signing/capture-evidence tests pass. `build/logs/release-readiness.json` records the
current NO_GO decision and evidence paths. No new firmware has been flashed to COM13. Its full 16 MiB flash backup is saved under
`build/device-backup/com13-20260929-before-signed-recovery.bin`. After the read-only checks and
backup, a further 40-second capture confirms MQTT connected with no runtime failures.

| Gate | Evidence | State |
| --- | --- | --- |
| RSA verifier | Production C++ verifier: valid/repeated signatures, wrong key, metadata mutation, truncated/tampered/extra image bytes, strict sidecar | Host tests pass |
| Recovery write/rollback behavior | Production Recovery with real signature/file checks and fake flash/reset: invalid pending image never erases, 12 reset boundaries resume | Five host integration tests pass |
| Journal ABI and A/B recovery | Production journal v1 (712 bytes), torn new slot, uncertain commit, corrupt slots, I/O errors and acknowledged cleanup | Host tests pass |
| One-shot reset | Production injection code preserves its consumed marker across simulated reset, skips reset on commit failure | Host tests pass |
| Home resource failure | Real LVGL Home refuses failed neighbor population, preserves current page, disables unavailable direction, retries successfully | Host test passes |
| Other resource failures | Image buffer, camera preview task, voice I/O task, MQTT bootstrap allocation hooks | Embedded validation pending |
| COM13 preflight | Existing firmware: 40-second capture, MQTT connected, no reset/panic; internal largest block 20,480 bytes | Baseline observation only |
| New Recovery deployment | COM13 read-only verification matches partition table but mismatches new Bootloader and Recovery | Wired migration required |
| Actual power interruption | Power fixture and observed cut points | Not established |
| Complete LVGL exhaustion | CLIB allocations and internal LVGL allocations can still assert | Release blocker |
| Eight-hour release soak | Must identify the newly flashed build and capture 28,800 seconds | Not started |
| Production signing root and server manifest | Operator-owned key and Rodak v2 signature fields | Not established |

## Build and package

Set the same `RODAK_OTA_PUBLIC_KEY` for both projects. Build/package with explicit private/public key
paths, task number, and the exact compiled version; see [firmware download](firmware-download.md).
No private key is committed or placed in a firmware package. The test key under ignored build output
is disposable. `-DevelopmentPackage` and `-AllowDevelopmentPackage` explicitly identify its packages.
A manifest version alone cannot prove that an installed immutable Recovery enforces authentication.

The package verifier checks the actual app descriptor version, signature, hashes, key fingerprints
in both binaries, journal ABI, and absence of fault-injection markers. Production packages cannot
contain an active reset or resource-injection build, including through `-SkipBuild`. A local test
package additionally requires `-DevelopmentPackage -AllowReleaseFaultInjection` when packaging and
`-AllowDevelopmentPackage -AllowReleaseFaultInjection` when flashing. These flags do not bypass
cryptographic or immutable-Recovery checks.

## Interruption matrix

Use a separate test build and a fresh `RODAK_RELEASE_TEST_ID` for each case. Set
`RODAK_OTA_FAULT_INJECTION_PHASE` on the project owning the boundary. The default NVS `release_test`
namespace stores the consumed trial before the reset, outside the journal A/B records. Repeating the
same trial resumes instead of resetting forever. Use short ASCII trial names. Production builds
leave the phase empty and `RODAKOS_RELEASE_TESTS=OFF`.

| Project | Injection point | Expected next boot | Hardware evidence |
| --- | --- | --- | --- |
| Main | `after_download_fsync` | Incomplete staging does not select Recovery | Pending |
| Main | `after_pending_sidecar` | Unjournaled files do not start installation | Pending |
| Main | `after_pending_journal` | Resume staged acknowledgement then Recovery handoff | Pending |
| Both | `before_journal_set`, `after_journal_set`, `after_journal_commit` | Select newest valid A/B generation; never erase default NVS | Pending |
| Recovery | `after_applying_state`, `before_image_erase`, `after_image_erase`, `during_image_write` | Revalidate candidate and restart write or restore backup | Pending |
| Recovery | `after_image_write`, `after_ready_to_boot` | Repeat safe write or resume boot handoff | Pending |
| Recovery | `after_restore_state`, `before_restore_erase`, `after_restore_erase`, `during_restore_write` | Repeat validated backup restoration | Pending |
| Recovery | `after_restore_write`, `after_rollback_ready` | Repeat restore or resume restored boot | Pending |
| Main | `after_boot_confirmation` | Preserve local confirmation while offline | Pending |
| Main | `after_result_http_ack`, `after_report_acknowledged` | Server deduplicates a lost acknowledgement; persisted acknowledgement skips resend | Pending |

Record trial, firmware hashes, trigger log, journal phase, resets, resulting image, and HTTP result.
The software hooks bracket erase/write and journal operations; only a controlled power fixture can
interrupt the flash/NVS operation itself. Also cut power during download and FAT flush/rename.
Do not call a software reset an actual power-cut result.

## Resource failures and soak

Build with `-DRODAKOS_RELEASE_TESTS=ON` and issue one serial command:

```text
RODAK_RELEASE_TEST_V1 fail_alloc home_page
```

Other targets are `image`, `camera_task`, `voice_task`, `mqtt_config`, and `clear`. Each target fires
once at its named allocation boundary. Trigger the corresponding operation, record the failure and
cleanup, then repeat to verify recovery. A camera task failure occurs after stream opening, so its
existing cleanup path must close hardware. Use the isolated 25-app Home population for three-page
turnover. These targeted failures do not simulate every allocation inside LVGL, codecs or drivers.

After the production flavor is restored and its build identity is verified, capture the soak:

```powershell
python tools/capture_release_stability.py --port COM13 --duration 28800 `
  --build-id <verified-main-sha256> --exercise-apps --output build/logs/release-soak-<stamp>
```

The collector opens the port once with DTR/RTS false, never reconnects, writes raw serial output, and
updates `status.json` every 30 seconds. Optional exercise requests cycle Home, Photos, Camera and
Music using existing app-launch commands. Physical page gestures and actual media playback still
need observation. `telemetry_queued` proves local enqueue, not broker delivery or server processing.

The collector requires at least 960 complete MQTT samples over eight hours, no reboot/runtime
failure, no health gap above 90 seconds, connected/enqueued telemetry, at least 8 KiB largest internal
block and 512 bytes worker stack headroom, and no internal-free median drop above 8 KiB. These are
local acceptance thresholds, not proof of sufficient memory for all concurrent device operations.
Short or interrupted captures remain incomplete; empty logs and old health formats cannot pass.

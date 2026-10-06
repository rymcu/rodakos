# OTA Release Readiness

The existing Home, voice, media, MQTT, WebRTC display, and signed-appearance functional gates are
accepted as completed where their evidence is recorded in the repository. This document tracks only
the remaining signed-firmware release, interruption, and resource-failure work. Passing software
tests does not close physical power-loss or full heap-exhaustion gates.

## Current evidence

Evidence review updated on 2026-10-06. The earlier source baseline `c64cf06` / `f7e8c91`
includes successful ESP-IDF 6.0.2 builds for normal and fault-injection firmware. The last
recorded signed package is `build/packages/ota/20261001-234748`; its main image is 6,897,584 bytes
(about 6.58 MiB) and remains within `ota_0`. The package booted through guarded COM3 refresh and
appearance revision 14 adoption; exact hashes and live evidence are recorded in
[appearance verification](appearance-verification.md). The earlier 2026-09-29 package remains
useful as the signed-OTA host baseline. Neither package identifies the newer, unflashed local
audio-volume build recorded below.

The app-model suite now passes 248 tests in Debug and ASan/UBSan, including sixteen audio-volume,
nine MCP dispatcher and two canonical-envelope adapter regressions. Ten additional tests instantiate the real voice service
and exercise its queue and lifecycle in both modes with leak detection; see
[voice-volume MCP evidence](#2026-10-06-voice-volume-mcp-validation). The earlier dependency
correction passed 14 real-codec tests in both modes and 13 generator validation tests; see the
[codec overlay evidence](#2026-10-06-codec-dependency-correction).
The previously recorded 43 Home UI, 11 signature/journal, 3 one-shot fault, and
5 production Recovery state-machine tests remain passing evidence for their recorded baseline,
with ASan/UBSan and leak checks; those separate targets were not rerun for the audio-only change.
Seventeen Python signing/capture-evidence tests pass after the 2026-10-06 collector regression update.
The added host cases reject missing/repeated device uptime and unterminated failure logs; they do
not establish a hardware soak. `build/logs/release-readiness.json` records the
current NO_GO decision and evidence paths. No new firmware has been flashed to COM13. Its full 16 MiB flash backup is saved under
`build/device-backup/com13-20260929-before-signed-recovery.bin`. After the read-only checks and
backup, a further 40-second capture confirms MQTT connected with no runtime failures.

| Gate                                        | Evidence                                                                                                                                 | State                                         |
| ------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------- |
| RSA verifier                                | Production C++ verifier: valid/repeated signatures, wrong key, metadata mutation, truncated/tampered/extra image bytes, strict sidecar   | Host tests pass                               |
| Recovery write/rollback behavior            | Production Recovery with real signature/file checks and fake flash/reset: invalid pending image never erases, 12 reset boundaries resume | Five host integration tests pass              |
| Journal ABI and A/B recovery                | Production journal v1 (712 bytes), torn new slot, uncertain commit, corrupt slots, I/O errors and acknowledged cleanup                   | Host tests pass                               |
| One-shot reset                              | Production injection code preserves its consumed marker across simulated reset, skips reset on commit failure                            | Host tests pass                               |
| Home resource failure                       | Real LVGL Home refuses failed neighbor population, preserves current page, disables unavailable direction, retries successfully          | Host test passes                              |
| Audio volume API failures                   | Production output/playback services and codec adapter with fake board/codec APIs: atomic relative changes, shared configuration, retained failure caches, failed first-open cleanup and deferred configuration | Sixteen host regressions pass; hardware unverified |
| Voice volume MCP / lifecycle                 | Production envelope parser, dispatcher and real service queue/I/O task: startup initialize, Stop, stale generations, reconnect, bounded deduplication, receipt roundtrips | Nine dispatcher, two adapter and ten service host regressions pass; hardware unverified |
| Codec volume driver failures                | Real esp_codec_dev and software-volume source: exact driver errors, cache retention, software priority and no-codec PCM path | Fourteen host regressions pass; hardware unverified |
| Other resource failures                     | Image buffer, camera preview task, voice I/O task, MQTT bootstrap allocation hooks                                                       | Embedded validation pending                   |
| COM13 preflight                             | Existing firmware: 40-second capture, MQTT connected, no reset/panic; internal largest block 20,480 bytes                                | Baseline observation only                     |
| Signed appearance / display peers           | COM3 revision 14 and six display sessions are hardware-verified                                                                          | Functional gate passed; release limits remain |
| New Recovery deployment                     | COM13 read-only verification matches partition table but mismatches new Bootloader and Recovery                                          | Wired migration required                      |
| Actual power interruption                   | Power fixture and observed cut points                                                                                                    | Not established                               |
| Complete LVGL exhaustion                    | CLIB allocations and internal LVGL allocations can still assert                                                                          | Release blocker                               |
| Eight-hour release soak                     | Must identify the newly flashed build and capture 28,800 seconds                                                                         | Not started                                   |
| Production signing root and server manifest | Operator-owned key and Rodak v2 signature fields                                                                                         | Not established                               |

## 2026-10-06 local audio-volume validation

Source baseline: `d935cf6` plus the audio-volume correction. The host target compiles the real
`AudioOutputService`, `AudioService`, and `AudioCodecOutput`; only lower-level board, codec,
FreeRTOS and decoder facilities are faked. All 230 tests pass in Debug and ASan/UBSan.
The nine added tests cover rejected volume writes retaining shared/playback/UI caches,
successful retry, clamping, closed-codec configuration without hardware opening, and failed
initial volume setup closing/clearing the attempted open before a retry.

An ESP-IDF 6.0.2 `idf.py build` completed successfully:

| Artifact | Result |
| --- | --- |
| Local main image | `build/rodakos.bin`, 6,897,792 bytes |
| SHA-256 | `876dde19bf0933a44cd18c684d78f7ed378e3a195f9cb9c2057e412adb011aee` |
| Main slot | Fits the 13.3125 MiB `ota_0` partition; 7,061,376 bytes remain |
| Device / signed-package status | Not flashed; does not replace package `20261001-234748` or its COM3 evidence |

The main-image size check also warns that this image exceeds the 2.5 MiB factory Recovery
partition. That is expected for the main target in this layout: use `build_ota_bundle.ps1` to
validate the main and separate Recovery artifacts against their proper slots. Root
`idf.py flash` / `app-flash` remain unsupported for this layout.

At this earlier service-correction baseline, the cache behavior applied when the codec API
reported failure. In the unmodified upstream
`managed_components/espressif__esp_codec_dev/esp_codec_dev.c`, the managed
`esp_codec_dev_set_out_vol` implementation updates its own cached volume and returns success
without propagating the return values from either `codec->set_vol` or `sw_vol->set_vol`;
it was not changed in that slice. The subsequent checked build overlay is described in
[dependency maintenance](dependency-maintenance.md); it corrects this setter without changing
the resolved managed source in place.
With the codec closed, an accepted setter is configuration for the next open, not a hardware write.
These tests and the build therefore establish neither physical I2C/speaker behavior nor a
correlated wire effect receipt. MQTT checks the service result and reports the retained value on
failure, but ordinary shadow reports carry no volume effect ID or applied desired revision.
See the [shadow contract](rodak-aiot-contract-v1.md#volume-configuration-and-evidence).

## 2026-10-06 codec dependency correction

Source baseline: `f7dd117` plus the checked build overlay. The project still resolves
esp_codec_dev 1.5.7 and leaves its managed source unchanged. The setter correction is applied
only to a generated build copy; [dependency maintenance](dependency-maintenance.md) records the
source provenance, automatic build hook and update/removal procedure.

- Real upstream source reproduces six failures in the 14-test codec suite. The generated source
  passes all 14 in Debug and ASan/UBSan with leak detection, including exact positive/negative
  driver errors, cache retention, software priority without fallback, retry and automatic
  software PCM processing with no hardware codec.
- All 13 generator tests pass: fresh output, identical repeat without touching modification time,
  output regeneration, source/version/lock/package drift rejection and LF/CRLF equivalence.
- The existing three-layer service/adapter suite was rerun in Debug and ASan/UBSan: 230/230 pass.
- ESP-IDF 6.0.2 passes both the existing build and a new build directory after the codec package
  was backed up and resolved again by the component manager. Both compile-command databases
  contain exactly one codec implementation source, their own generated overlay. Their
  `sdkconfig.h` files match. Removing the generated C file and rebuilding also regenerates it
  automatically and succeeds.

| Build directory | Main image bytes | SHA-256 |
| --- | --- | --- |
| `build/` | 6,897,776 | `04544c0b9f270639b08712d4e9683a09dc77e7603c0edc2407478f31995095cc` |
| `build/codec-overlay-cold/` | 6,896,896 | `18fc55fb78b4c1277c3e863d2d2a70d3d35e66c8f4e3cd07781575e6c405cb34` |

Both fit `ota_0` (13,959,168 bytes); their remaining space is 7,061,392 and 7,062,272 bytes
respectively. These are separate local build artifacts, not a claim of identical firmware
binaries or a new signed device package. Build logs are
`build/logs/codec-overlay-hot.log`, `codec-overlay-cold.log` and `codec-overlay-regenerate.log`
in that same logs directory. The managed `esp_codec_dev.c` remains at normalized SHA-256
`b7a17e2ad412f4c08e0e9324aea0f217a49305510fd80e7ef3420fb31d7fd2ce`.

No device was flashed and no NVS was changed. The main-image/factory-Recovery size warning has
the same meaning as in the earlier build above. Other upstream API failure paths, actual I2C or
speaker behavior and correlated device effect receipts remain outside these host/build results.

## 2026-10-06 voice-volume MCP validation

Source baseline: `a141297` plus the voice-volume MCP integration. The production service installs
the handler, advertises MCP, retains ready-adjacent requests in its bounded queue and routes
initialized calls through the atomic output service. The detailed
[volume contract](voice-volume-mcp.md) defines the three tools, correlation, deduplication and
`rodakos.volume-receipt.v1` software evidence.

- App-model: **248/248** in Debug and ASan/UBSan with leak detection. This includes sixteen
  production audio/output/playback/adapter tests (including 32 concurrent relative operations)
  and nine production MCP dispatcher tests, plus two production adapter cases for extracting
  inner JSON-RPC from canonical envelopes, metadata preservation and malformed/stale scope rejection.
- Real voice service: **10/10** in Debug and ASan/UBSan with leak detection. The dedicated host
  target compiles production `VoiceAssistantService`, its inbound queue, I/O task, dispatcher
  and reconnect coordinator. Public startup and installed transport callbacks verify early
  initialize, handshake-before-call, cached repeats, stale generations, Stop canceling queued
  work, Stop during open, reconnect handshake/ledger reset and 64-event queue pressure.
  Normal inbound messages traverse the production canonical-envelope parser; response roundtrips
  use the production builder and preserve typed RPC IDs, effect correlation and receipt contents.
  No copied guard or static source-string assertion substitutes for service execution.
- ESP-IDF **6.0.2** `idf.py build` succeeds with the checked codec overlay. No dependency-lock
  or sdkconfig content change was required.

| Artifact | Result |
| --- | --- |
| Local main image | `build/rodakos.bin`, 6,911,216 bytes |
| SHA-256 | `41b48507f59b832999ea6a36ed82fe5bd55bde49a61010ea82eccfb0f464918a` |
| Main slot | Fits `ota_0` (13,959,168 bytes); 7,047,952 bytes remain |
| Device / signed-package status | Not flashed or signed; package `20261001-234748` remains the last recorded device package |

The WebSocket transport I/O, FreeRTOS scheduling primitives, recorder, audio focus, Opus and hardware
APIs are fakes. The envelope extraction and response-building helpers are production code; their
adapter evidence is distinct from the full service lifecycle tests and the cross-repository
canonical WebSocket-to-production-dispatcher fixture. These tests establish software control flow and configuration evidence, not
real network/RTOS timing, codec register readback, audible volume, physical restoration,
NVS persistence or release approval. All volume receipts explicitly remain volatile and
`physicalVerified: false`. MQTT shadow evidence is unchanged. No serial, flash or NVS operation
was performed. The main-image/factory-Recovery size warning retains its earlier meaning.

## Build and package

Set the same `RODAK_OTA_PUBLIC_KEY` for both projects. Build/package with explicit private/public key
paths, task number, and the exact compiled version; see [firmware download](firmware-download.md).
No private key is committed or placed in a firmware package. The test key under ignored build output
is disposable. `-DevelopmentPackage` and `-AllowDevelopmentPackage` explicitly identify its packages.
A manifest version alone cannot prove that an installed immutable Recovery enforces authentication.
The Rodak server must deliver the signed `manifestVersion: 2` fields (`signatureType:
"rsa2048-sha256"` and `signatureValue`) for the same task, version, size, product, slot, and image
digest. Rodak's OTA service resolves and forwards the release manifest; it does not generate the
firmware signature. Appearance resource signatures use a separate trust chain and do not satisfy the
firmware OTA requirement.

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

| Project  | Injection point                                                                              | Expected next boot                                                                 | Hardware evidence |
| -------- | -------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------- | ----------------- |
| Main     | `after_download_fsync`                                                                       | Incomplete staging does not select Recovery                                        | Pending           |
| Main     | `after_pending_sidecar`                                                                      | Unjournaled files do not start installation                                        | Pending           |
| Main     | `after_pending_journal`                                                                      | Resume staged acknowledgement then Recovery handoff                                | Pending           |
| Both     | `before_journal_set`, `after_journal_set`, `after_journal_commit`                            | Select newest valid A/B generation; never erase default NVS                        | Pending           |
| Recovery | `after_applying_state`, `before_image_erase`, `after_image_erase`, `during_image_write`      | Revalidate candidate and restart write or restore backup                           | Pending           |
| Recovery | `after_image_write`, `after_ready_to_boot`                                                   | Repeat safe write or resume boot handoff                                           | Pending           |
| Recovery | `after_restore_state`, `before_restore_erase`, `after_restore_erase`, `during_restore_write` | Repeat validated backup restoration                                                | Pending           |
| Recovery | `after_restore_write`, `after_rollback_ready`                                                | Repeat restore or resume restored boot                                             | Pending           |
| Main     | `after_boot_confirmation`                                                                    | Preserve local confirmation while offline                                          | Pending           |
| Main     | `after_result_http_ack`, `after_report_acknowledged`                                         | Server deduplicates a lost acknowledgement; persisted acknowledgement skips resend | Pending           |

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
Music using existing app-launch commands. Every requested launch requires both `queued:true` and
a successful `RODAK_APP_LAUNCH_COMPLETE`; explicit `ok:false` or a missing completion makes the
completed capture NO-GO. Physical page gestures and actual media playback still
need observation. `telemetry_queued` proves local enqueue, not broker delivery or server processing.

The collector requires at least 960 complete MQTT samples over eight hours, no reboot/runtime
failure, no health gap above 90 seconds, connected/enqueued telemetry, at least 8 KiB largest internal
block and 512 bytes worker stack headroom, and no internal-free median drop above 8 KiB. These are
local acceptance thresholds, not proof of sufficient memory for all concurrent device operations.
Health samples must carry strictly increasing device uptime; missing, repeated, or regressed uptime
is a failure and cannot increase the valid sample count. The collector also inspects any final
unterminated serial fragment before saving its result, including on an interrupted capture, so a
trailing panic/reset or partial health record cannot be silently discarded.
Short or interrupted captures remain incomplete unless an observed failure makes them NO-GO;
empty logs and old health formats cannot pass.

Rerun the signing and collector host regression from the repository root:

```powershell
wsl -d Debian -- python3 -m unittest discover `
  -s /mnt/d/workspace/rodakos/tests/ota_security -p 'test_*.py' -v
```

The 2026-10-06 run against source baseline `2ed1e8c` plus the collector changes passed all 17 tests.
Before the fix, two new regression cases incorrectly returned `pass-observed` for eight hours of
health logs with missing or repeated uptime. The correction does not change the production-key,
physical power-cut, complete LVGL exhaustion, or eight-hour hardware gates above.

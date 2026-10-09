# OTA Release Readiness

The existing Home, voice, media, MQTT, WebRTC display, and signed-appearance functional gates are
accepted as completed where their evidence is recorded in the repository. This document tracks only
the remaining signed-firmware release, interruption, and resource-failure work. Passing software
tests does not close physical power-loss or full heap-exhaustion gates.

Decision updated 2026-10-09: release evidence is collected through local tests, firmware builds,
package verification and the physical gates below without depending on hosted GitHub Actions.
Do not run or repair Actions, including static workflow changes, billing, quota or required-check
setup. Preserve existing CI outcomes with their original candidates as history, including failures.
Missing or unavailable Actions do not make the release NO_GO; the unresolved software,
production-root, power-loss, resource and soak gates retain their existing acceptance criteria.

## Current evidence

Evidence status updated on 2026-10-09. 039 completed one same-boot normal/quiet/normal
comparison on ordinary 037 package `20261008-202609`, with the same desktop PID 2140.
All three windows contained two recovered gaps. Q suppressed 36 target PC publications and
forwarded zero over 110.1013822 seconds; the target PC stream is not necessary for this Q stall.
Other MQTT/TLS/logging/PI candidates remain unresolved. Original publish, serial capture and
temporary inspector cleanup are complete; binding/tokenVersion4 remain. These bounded USB
synthetic-input observations do not establish physical or acoustic acceptance. Root cause is
**INCONCLUSIVE** and resource/production remain **NO_GO**. See
[039 evidence](ota-release-readiness.md#2026-10-09-pc-status-causal-comparison-039) and the separately preserved
[038 software evidence](ota-release-readiness.md#2026-10-08-snapshot-before-log-software-correction-038).

Evidence review updated on 2026-10-09. The earlier source baseline `c64cf06` / `f7e8c91`
includes successful ESP-IDF 6.0.2 builds for normal and fault-injection firmware. The last
recorded appearance-gate package is `build/packages/ota/20261001-234748`; its main image is 6,897,584 bytes
(about 6.58 MiB) and remains within `ota_0`. The package booted through guarded COM3 refresh and
appearance revision 14 adoption; exact hashes and live evidence are recorded in
[appearance verification](appearance-verification.md). The earlier 2026-09-29 package remains
useful as the signed-OTA host baseline. Neither older package identifies the latest
development-signed media package recorded below. The dated 2026-10-06 device refresh used
development-signed package `20261006-225642`,
main image 7,015,312 bytes, validated on COM3 for serial proof auto-binding and same-URL binding
preservation. Its hash, boot log and two provisioning rounds are recorded in
[serial provisioning evidence](serial-provisioning.md#2026-10-06-hotspot-and-binding-proof-gate).
Later trusted-network packages and COM3 observations are recorded separately in
[trusted server discovery](trusted-server-discovery.md#validation), including 007's failed NVS
gate, 008's diagnostic run and compact-authority 009 hardware observations. The paired Rodak workflow and
evidence are [trusted provisioning](https://github.com/rymcu/rodak/blob/master/docs/serial-provisioning.md)
and [network verification](https://github.com/rymcu/rodak/blob/master/docs/trusted-network-verification.md).
These newer development-signed network runs do not replace the appearance baseline above or
close production-key deployment, physical power-cut or eight-hour signed-OTA soak gates.
The historical 025 package (`20261007-200215`, source `8d5cf99`) preserved the original
development signing root and immutable Recovery preserved. Same-authority MQTT refresh now
replaces its SDK client; its bounded validation is recorded in
[025 evidence](#2026-10-07-mqtt-credential-client-replacement-025).
Release remains **NO_GO**: this does not close production-root, power-cut, resource or soak gates.

The earlier 021 package's scoped JPEG PSRAM allocation shows
no net DMA loss during open, and screen-first Camera starts. Camera then stalls at STREAMOFF
begin for 127.583 seconds and requires a controlled reset. The separate recovery window
still has late rejected inputs and no successful post-reenable Retry. Final internal largest
is 8,192 bytes, which does not establish sustained headroom or close the failed lifecycle gate.
See the retained [021 validation](#2026-10-07-scoped-screen-jpeg-psram-validation-021).
The previous 020 independently reproduces pre-callback input delay, AES allocation failure
during Camera/screen concurrency, and static-page first-frame failure; retain its
[diagnostic evidence](#2026-10-07-peer-timing-diagnostics-020).
Package 019 passed targeted device cancellation checks, while end-to-end timeouts and
screen-first DMA failure remained. See
[cancelled-gesture correction](#2026-10-07-cancelled-gesture-correction-019).
Package 018 retains its Camera-exit stall requiring controlled reset and a separate post-reset
window with delayed/rejected control replies. See
[resource and ACK diagnostics](#2026-10-07-resource-and-ack-diagnostics-018).
Package 017 retains its independent [Camera exit and PNG allocation evidence](#2026-10-07-camera-exit-and-png-allocation-validation).
The 013–016 progression remains in
[media decode and display-allocation validation](#2026-10-07-media-decode-and-display-allocation-validation).
The preceding Recorder/Camera work remains recorded in
[Recorder/Camera save validation](#2026-10-06-media-save-validation).

The MQTT-volume slice recorded 266 app-model tests in Debug and ASan/UBSan, including eighteen MQTT volume
effect cases. Fourteen new tests instantiate the actual MQTT service and exercise callbacks,
fragment/queue epochs, cancellation and SDK-task receipt publishing in both modes with leak
detection; see [MQTT volume evidence](#2026-10-06-mqtt-volume-effect-validation).
The earlier sixteen audio-volume, nine MCP dispatcher and two canonical-envelope adapter cases
remain in that suite. The previously recorded ten real voice service tests exercise its queue
and lifecycle in both modes with leak detection; see
[voice-volume MCP evidence](#2026-10-06-voice-volume-mcp-validation). The earlier dependency
correction passed 14 real-codec tests in both modes and 13 generator validation tests; see the
[codec overlay evidence](#2026-10-06-codec-dependency-correction).
The previously recorded 43 Home UI, 11 signature/journal, 3 one-shot fault, and
5 production Recovery state-machine tests remain passing evidence for their recorded baseline,
with ASan/UBSan and leak checks; those separate targets were not rerun for the audio-only change.
The 018 focused runs pass 33 Camera/FileService, 21 production display ACK, 20 real-LVGL input and
24 production `DisplayService` cases in Debug and ASan/UBSan with leak detection. The independent 018
runner passes 30 suites / 42 CTest / 52 Python cases; adding the separate ACK/input checks gives
32 suites / 44 CTest. All 682 recorded source hashes match before/after this run, before 019 changes.
Package 018's 7,116,480-byte image passed guarded refresh and boot.
Its first Camera exit stopped producing serial output and required a controlled RTS reset.
After reset, an 18.080-second / 243-frame preview exited normally and A3 decoded three times,
but the third attempt included a rejected up84 and delayed down83/disable85 replies.
The JPEG stage snapshots show about 8,084 bytes of temporary internal allocation; 018 does not
change the allocator. Final internal largest 11,776 bytes comes from a different run history,
while the historical internal minimum is 47 bytes. These results do not establish sufficient
headroom, reliable input cancellation or release stability. Earlier 016/017 successful windows
remain valid within their recorded limits and do not replace these newer failures.
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
| MQTT volume effects / lifecycle              | Production helper and actual UnifiedMqttService: correlation, ordering, authority ledger, real connection epoch, SDK callback/queue, Stop, refresh, unbind and scoped receipt publishing | Eighteen helper and fourteen service host regressions pass; hardware unverified |
| RGB light patches / lifecycle               | Real LightService, board adapter, MQTT callback/worker and receipts: atomic merge/commit, driver failures, 64-result eviction, authority versions and cancellation | Thirteen native driver and seventeen MQTT light regressions pass; hardware unverified |
| Codec volume driver failures                | Real esp_codec_dev and software-volume source: exact driver errors, cache retention, software priority and no-codec PCM path | Fourteen host regressions pass; hardware unverified |
| Command / stream / input lifecycle | Original-connection publication, bounded result cache, stream cleanup, real LVGL input grants, original-peer ACK retry and cancellation without synthetic click | 021 held-down cancellation causes no extra PNG load, but the next reenabled Retry still times out at 3.197/3.193 s and is rejected. Earlier 019/020 software and device evidence retains its own identity. End-to-end delivery, static first frame and wider physical gates remain open |
| Voice identity / recovery | Single-record persistence, retained revision watermark, Unix/monotonic expiry, runtime recovery and proactive shadow reports | 277 app-model, 8 parser, 25 wake service, 6 frontend and 4 service integration cases pass; 4 desktop cross-repository cases pass; hardware unverified |
| Music scanning / playback | Production directory reader, asynchronous AudioService with managed Helix and real LVGL Music UI | 8 directory + 17 audio + 18 UI cases pass in Debug/ASan; physical SD/audio unverified |
| Media PNG / display allocation | PNG ownership/inflate headroom, Camera frame release, scoped screen JPEG PSRAM allocation and final ELF gate | 021 Display 30 / Home 43 / allocator 10 pass Debug/ASan; checker 26 and real ELF positive/bypass-negative checks pass. First-frame open DMA net loss is zero and screen-first Camera starts, but STREAMOFF stalls for 127.583 s before reset. Two post-reset A3 loads pass; final 8,192-byte internal largest does not close OOM/concurrency or soak |
| Other resource failures                     | Physical image/display coexistence, camera preview task, voice I/O task, MQTT bootstrap allocation hooks                               | Embedded validation pending                   |
| COM13 preflight                             | Existing firmware: 40-second capture, MQTT connected, no reset/panic; internal largest block 20,480 bytes                                | Baseline observation only                     |
| Signed appearance / display peers           | COM3 revision 14 and six display sessions are hardware-verified                                                                          | Functional gate passed; release limits remain |
| New Recovery deployment                     | COM13 read-only verification matches partition table but mismatches new Bootloader and Recovery                                          | Wired migration required                      |
| Actual power interruption                   | Power fixture and observed cut points                                                                                                    | Not established                               |
| Complete LVGL exhaustion                    | CLIB allocations and internal LVGL allocations can still assert                                                                          | Release blocker                               |
| Eight-hour release soak                     | Ordinary OFF `20261009-014905` completed 28,800 seconds; see the [final evidence](#2026-10-09-正式八小时采集最终结果) | NO_GO: 9/16 Camera DMA allocation failures, raw errors and insufficient resource headroom |
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

## 2026-10-06 MQTT volume effect validation

Source baseline: `56eb7bf` plus the MQTT volume effect integration. The
[MQTT contract](mqtt-volume-effects.md) defines single-publication metadata and the independent
`effects/receipt` result topic; ordinary shadow reports remain configuration snapshots.

- App-model: **266/266**, including **18** production MQTT helper cases, in Debug and
  ASan/UBSan with leak detection. The bounded ledger stores the original result, checks repeats
  before the version watermark, rejects conflicts/capacity, and reserves the calculated response
  capacity before mutation. Maximum legal identifiers and longest success/rejection layouts pass.
- Actual MQTT service: **14/14**, in Debug and ASan/UBSan with leak detection. The target compiles
  production `UnifiedMqttService`, its real event callback, fragment assembly, queue, worker,
  desired handler and result publishing path. It covers Stop after dequeue, same-client automatic
  reconnect, fragments spanning disconnect, delayed results, token refresh, binding replacement,
  explicit unbind and an in-progress codec commit versus Stop.
- The fake SDK invokes event callbacks while holding its recursive API lock, matching the pinned
  SDK's ordering. Production receipt delivery uses a custom event inside that SDK task; the worker
  protects the client pointer while posting without taking the SDK API lock. Old-epoch results are
  discarded from the service-owned pending queue instead of being moved to a replacement client.
  Already-enqueued SDK outbox messages cannot be recalled; Rodak must reject late results using
  the original authenticated connection binding.
- ESP-IDF **6.0.2** `idf.py build` succeeds. The final incremental build log is
  `build/logs/mqtt-volume-effects-build.log`; dependency-lock and sdkconfig content are unchanged.

| Artifact | Result |
| --- | --- |
| Local main image | `build/rodakos.bin`, 6,921,328 bytes |
| SHA-256 | `c2056d47d7ba3e0c291a10ef31207d78216da3eb508c81aac166b714480ffb36` |
| Main slot | Fits `ota_0` (13,959,168 bytes); 7,037,840 bytes remain |
| Device / signed-package status | Not flashed or signed; the recorded COM3 package is unchanged |

The standalone `rodakos_mqtt_volume_fixture` runs the same real service and exposes its actual
result publications for a Rodak broker integration run. Host helper/service evidence and the
cross-repository broker fixture are distinct; neither exercises a board's real MQTT task,
codec/I2C or acoustic output. Receipts remain volatile software evidence with
`physicalVerified: false`. No serial, flash or NVS operation was performed. The main image's
factory-Recovery size warning retains the earlier documented meaning.

## 2026-10-06 MQTT light effect validation

Source baseline: `4a72e8c` plus the [RGB light integration](mqtt-light-effects.md). Local UI and
MQTT now use the same atomic light patch, which commits configuration/revision only after the
production LED adapter succeeds. Mid-pixel/refresh failure retains accepted software state and
marks hardware application unverified; no hardware rollback is claimed.

- App-model: **266/266**, Debug and ASan/UBSan with leak detection; existing volume contracts pass.
- Real LightService/board adapter: **13/13**, Debug and ASan/UBSan with leak detection, including
  native setters, scaling/ranges, missing handles, clear/pixel/refresh failures and serialization.
- Real MQTT service: **31/31** (14 existing volume + 17 light), Debug and ASan/UBSan with leak
  detection. This compiles actual LightService/board adapter instead of the previous light fake.
  The added cases cover 70 successive writes, eviction and cross-light retargeting, maximum legal
  IDs/safe-integer versions, cached failure, old desired fields, duplicate root/metadata routing,
  ordinary reports, fragmented epochs, Stop, credential rotation/rebinding and explicit unbind.
- The light receipt scratch buffer is a bounded service member, avoiding an additional large
  frame on the 6 KiB MQTT worker stack. This is a software bound, not measured device stack margin.
- ESP-IDF **6.0.2** `idf.py build` succeeds after the duplicate-metadata guard. The final log is
  `build/logs/mqtt-light-effects-build.log`; `sdkconfig` and `dependencies.lock` content are unchanged.

| Artifact | Result |
| --- | --- |
| Local main image | `build/rodakos.bin`, 6,941,088 bytes |
| SHA-256 | `6112618b30c39e9497ce43192cd7def5f7f7f80252980fbd27b32f4b6d351321` |
| Main slot | Fits `ota_0` (13,959,168 bytes); 7,018,080 bytes remain |
| Host light CLI SHA-256 | `63f69301ddd0f66c386e3be74fcd6143180554ddd052ba589c60aac8ad85b34a` |
| Device / signed-package status | Not flashed or signed; existing recorded device-package evidence is unchanged |

`rodakos_mqtt_light_fixture` exposes production receipts and the latest raw production shadow
report for the separate Rodak broker conformance run. Its LED/Board Manager/network SDKs remain
host fakes. RGB output, RMT timing, persistence, resource pressure and real task/transport timing
remain hardware gates. No serial, flash or NVS operation was performed. The main-image versus
factory-Recovery size warning retains its previously documented meaning.

## 2026-10-06 command publication validation

This unflashed slice starts from RodakOS `0d0f2bd` and Rodak `d152df19`, plus the frozen command
publication and SDK queue changes. The command handler captures its original generation, epoch
and ACK topic; results drain through direct QoS 0 publish without an SDK outbox entry. A checked
ESP-MQTT 1.0.0 overlay separates custom notifications from the native lifecycle event slot.

- Production service host: 31 effect and 24 command cases pass in Debug and ASan/UBSan with
  `ASAN_OPTIONS=detect_leaks=1`. Cases include an old shared-queue negative control, new separate
  queues, stored-outbox replay control, synchronous/late stream callbacks, nested disconnect,
  transfer retry, credential changes, Stop, queue limits and shared wake scheduling.
- SDK function target: 7 scenarios pass in Debug and sanitizer; 8 Python provenance/generation
  tests pass. The target compiles functions extracted from generated SDK source, with fake
  FreeRTOS and event-loop facilities; it is not a full networking or device test.
- Rodak command gate: 19/19 cross-repository cases, 24 host cases, 7 SDK scenarios and 8 generator
  checks pass. The existing MQTT volume/light gate also passes 8/8. Before/after source and
  binary hashes agree. Rodak evidence files are `.codex-temp/command-epoch-conformance.json`
  and `.codex-temp/command-epoch-effect-conformance.json`.
- Final ESP-IDF 6.0.2 build passes. Main image: **6,945,936 bytes** within the 13,959,168-byte
  `ota_0` slot; SHA-256 **`e29716a72cd38192afeffadb5948a5866154772b3f4fe6556a768484d04333d6`**.
  `compile_commands.json` selects `build/rodak_patches/esp_mqtt/mqtt_client.c` and puts its
  generated private-header directory first. Managed sources, dependency lock and `sdkconfig`
  are unchanged; the Home test population remains OFF and the existing local test public key
  remains configured. No new signed package, production key or hardware acceptance is claimed.

The command gate's RodakOS input source SHA-256 is
`69c1319c8c52fb0d30e6b3a33b5a16204d64ea2b2a7b173a71a104da1e721101`.
Rodak stores the 25 changed source/test hashes in `.codex-temp/command-epoch-source-snapshot.json`,
firmware identity in `.codex-temp/command-epoch-firmware.json`, and the final build log in
`.codex-temp/command-epoch-idf-build.log`. Documentation and commits follow that software freeze.

This does not add command-number deduplication, cancel admitted side effects, establish stream
leases/cleanup, fence delayed screen input or fix voice identity persistence/expiry. No serial
port, flash or device NVS was accessed; release, acoustic, power-cut and long-duration gates stay open.

## 2026-10-06 stream lifecycle validation

This unflashed slice starts from RodakOS `9940bea` and Rodak `06f6b479`, plus the frozen
stream-instance, remote-input, ACK-ownership and command-cache changes. Stream admission is
revoked before cleanup; already admitted single operations may finish. The volatile latest-64
command cache replays immutable results and rejects byte-level payload conflicts. Reclaimed
response bodies leave tombstones; eviction and reboot have no deduplication guarantee. Replaying
an old successful start does not reopen a cleaned stream.

- Production MQTT service: 31 effect and 55 command cases pass in Debug and ASan/UBSan with
  leak detection. Tests use real SHA-256 through PSA/libmbedcrypto.
- Remote input: 15 cases compile the production controller and helpers with real managed LVGL
  and cJSON. Display ACK: 13 cases compile the complete production display service and inspect
  actual encoded bytes and target peers. Both targets pass Debug and ASan/UBSan with leak checks;
  network peers, RTOS and capture hardware remain host fakes.
- Desktop terminal sidebands look up a persisted start request by command number and match its
  session ID, device and stream kind. Valid closed/disconnected/failed events end that preview
  without changing the first command ACK. New previews have new session IDs; old-session or
  misclaimed events cannot terminate them. Sessions do not separately bind a unique start command.
- Rodak full coverage passes: 484 files / 3,589 tests, with 3 files / 23 tests skipped. All four
  C++ conformance fixtures are enabled. Main-process type checking, 43 targeted service tests
  and the standard Electron build also pass.
- ESP-IDF 6.0.2 build passes: main image **6,963,840 bytes**, within the 13,959,168-byte `ota_0`
  slot; SHA-256 `6e2f363cd360d4dc45af10b36ef8cdc27c4953d733669ef0c5e786dd8e7bb76f`.
  It retains the existing test public key and disables Home hardware-test population.

- Formal command gate: 21/21 cross-repository cases, 55 host command cases, 15 LVGL input cases,
  13 complete display ACK cases, 7 SDK event scenarios and 8 generator checks pass; no cross-repo
  case is skipped. The original MQTT effect gate also passes 8/8 cases. Inputs and all five command
  gate binaries are identical before/after execution. Evidence is in Rodak
  `.codex-temp/stream-lifecycle-conformance.json`, its `.vitest.json` report and
  `.codex-temp/stream-lifecycle-effect-conformance.json`.

The command gate's RodakOS input source SHA-256 is
`84c0b71a3387d697de34ce41296e6a97915737d741a881115b8e865d01558e9e`.
Rodak stores all 37 changed source/test hashes in `.codex-temp/stream-lifecycle-source-snapshot.json`,
the four coverage fixture hashes in `stream-lifecycle-coverage-fixtures.json`, and firmware identity
in `stream-lifecycle-firmware.json`. Build and test logs share that filename prefix. Only documentation
and commits follow this software freeze; older dated tests above remain evidence for their own baselines.

No device, serial, NVS, flashing or packaging operation was performed. These software checks do
not replace wireless reconnect/resource-contention, physical screen/input, acoustic, power-cut or
eight-hour signed-OTA acceptance.

## 2026-10-06 voice identity validation

This unflashed slice starts from RodakOS `a7cb1b0` and Rodak `90f853e4`, plus the frozen identity
consistency changes. A single bounded record replaces multi-key identity writes, retaining the
last accepted request across expiry. Indeterminate storage or failed runtime recovery prevents
ordinary updates from claiming success. The desktop allocates revisions and excludes unconfirmed
identity candidates from its actual Base System Prompt.

- Formal identity gate: 277 app-model, 8 MQTT parsing/reporting, 25 complete wake service,
  6 complete audio frontend and 4 real MQTT/wake integration cases pass. Four desktop cross-repo
  cases use the real Broker, C++ reports, persistence and prompt rendering; none is skipped.
  Inputs and all six tested binaries have identical before/after hashes.
- The app-model, wake-service, frontend and real integration targets also pass ASan/UBSan with
  leak detection. The shared MQTT service also passes all 31 effect, 55 command and 8 identity
  cases under sanitizers with leak detection. The frontend keeps the active AFE model catalog during wake-graph failure and
  copies wake-service error strings under its lock. Other recorder consumers of the legacy
  `last_error()` pointer API are outside this change.
- Ordinary command gate 21/21 and MQTT volume/light gate 8/8 pass on this source. Desktop main
  and preload type checks and the standard Electron build pass. Full desktop coverage with all
  five C++ fixtures passes 487 files / 3,623 tests, with 3 files / 23 tests skipped in 631.48 seconds.
  Statements: 69.47%; branches: 63.45%; functions: 66.93%; lines: 70.49%.
- ESP-IDF 6.0.2 build passes. Main image: **6,983,280 bytes** within the 13,959,168-byte `ota_0`
  slot; SHA-256 `b932cd9593c3709b90f4c7359211cbc0acd91904041f682feb218fc87f0f1fca`.
  The configured key remains the local test public key and Home hardware-test population is OFF.
  Generic IDF factory-partition flashing suggestions do not apply to the immutable Recovery layout.

Identity-gate RodakOS source SHA-256:
`3aa4e55d85b41acd0fc60972686abed878cb1e6e590bb6767a40a2a1a017f553`.
Rodak stores the 77 changed source/config/test hashes in `.codex-temp/voice-identity-source-snapshot.json`,
the five coverage fixtures in `voice-identity-coverage-fixtures.json`, firmware identity in
`voice-identity-firmware.json`, and gate reports/logs under `.codex-temp/voice-identity-*`.
Documentation and commits follow this software freeze. Reproduction is documented in Rodak
`scripts/rodakos-voice-identity-conformance.md` and this repository's host-test READMEs.

No packaging, flashing, serial or device NVS operation was performed. Real wake accuracy, physical
clock synchronization, Flash power cuts, resource-pressure recovery, two-device isolation and the
signed-OTA soak remain open. These are software configuration/reporting results, not acoustic proof.

## 2026-10-06 music scanning and playback validation

Source baseline: RodakOS `9bd388f` and Rodak `5ae4c52d`, plus the frozen music changes. Music
separates library failures from an empty library, retries on its monitor worker, rejects stale
song-list revisions and displays actual asynchronous playback errors. The production directory
reader discards partial results; WAV/MP3 playback does not complete on early EOF or read failure.

- 8 production directory-reader cases, 17 AudioService cases and 17 real-LVGL Music cases pass
  in Debug and ASan/UBSan with leak checks. Audio tests compile the managed Helix decoder with
  documented host arithmetic intrinsics, real files and threads; they do not substitute decoded
  results. They include incomplete MP3 header/CRC/side-info at the allocation boundary and valid
  frames crossing the input buffer. No sanitizer exclusions are used in that target.
- The existing app-model suite passes all 277 cases in both modes. The desktop MCP gate passes
  16 cross-repository cases; 8 unrelated cases are excluded by its `-t` filter. Source inputs and
  the production MCP fixture hashes match before/after the gate. No desktop application source
  changed, and the earlier full-coverage results retain their original baseline.
- Software screenshots for empty library, unavailable card and playback failure were reviewed.
  A long title no longer overlaps the error; two status lines fit inside the card. Host fake
  fonts and the LVGL software display do not establish device typography, touch or readability.
- ESP-IDF 6.0.2 build passes: main image **6,995,040 bytes**, below the 13,959,168-byte `ota_0`
  slot. SHA-256: `0943f3c437963532914df72657a134145a9066a58bbb4475708d797a53a18e2e`.
  `sdkconfig` and `dependencies.lock` are unchanged; Home hardware-test population is OFF and
  the configured public key remains the local test key. No Recovery image or signed package
  was generated. Generic IDF factory flashing suggestions do not apply to this partition layout.

Rodak keeps 33 changed source/config/test hashes in `.codex-temp/music-source-snapshot.json`,
nine test/fixture binary hashes in `music-test-binaries.json`, and the image identity in
`music-firmware.json`. The gate report is `music-mcp-conformance.json`; logs are
`music-file-directory-final.log`, `music-audio-playback.log`, `music-ui.log`, `music-app-model.log`,
`music-mcp-conformance.log` and `music-idf-build.log`. The MCP input source digests are
Rodak `a664dc99dbe72f90ca014b2a214ebe825df612a10ce475c2eaf791f561a41a8a` and
RodakOS `00c1d73a40f4a0bad9d99d4c184f590876db4084ecafeb6eb3b06a9379dfd97e`.

Reproduction and limitations: [music behavior](music-playback.md),
[directory tests](../tests/file_directory/README.md),
[audio tests](../tests/audio_playback_service/README.md), and
[Music UI tests](../tests/music_ui/README.md). Shutdown waits for in-flight I/O and does not
promise a time bound if a hardware driver never returns. Playback preference writes retain
their existing persistence semantics; this change adds no remote media effect or MCP tool.

No hardware, serial, flashing, packaging or device NVS operation was performed. Real SD-card
removal/slow-card behavior, audio focus with Recorder/voice, audible output, resource exhaustion,
NVS power cuts and signed-OTA soak remain open. Recorder final-save and Camera completion-delivery
errors were still separate implementation work at this music baseline; their later software evidence is recorded below.

## 2026-10-06 media save validation

Source baseline: RodakOS `b29d8375` and Rodak `e52869b7`, plus this media-save working tree.
Recorder checks the final WAV seek/header, stream flush and close before publishing Saved; library
scan errors remain separate from that save result. Cancellation cannot overwrite a finalization
or cleanup error. Camera workers publish an owned, generation-bound result; an independent LVGL
timer consumes it without worker-side LVGL locking or async allocation. App teardown revokes
old results and removes timers before releasing UI objects.

FileService serializes short mutations and reserves normalized paths for long Recorder/Web
writes. The lease covers creation, data writes, finalization and cleanup, rejects conflicting
parent/child paths, and releases without holding the global I/O lock through recording or network
receive. Exclusive creation protects existing photos and recordings. These guarantees cover
cooperating FileService callers; flush/close success does not establish power-loss durability.
See [media save behavior](media-save.md) for the implementation and hardware boundaries.

Seven focused targets pass **79 cases** in both Debug and ASan/UBSan with leak detection:

| Target | Cases | Production coverage / host substitutions |
| --- | --- | --- |
| File writer | 10 | Real stdio write helper; linker-injected failures |
| Path leases | 5 | Real normalization and conflict helper |
| Recording service | 17 | Real service, threads and WAV files; storage/audio/focus adapters are fakes |
| Recorder UI | 14 | Real app, recording service and LVGL; hardware/playback adapters are fakes |
| Camera capture | 14 | Real CameraService, FileService and writer; V4L2/JPEG/board dependencies are fakes |
| Camera UI | 13 | Real app, host lifecycle and LVGL; CameraService/focus are fakes |
| Web upload | 6 | Real UploadHandler, host files and stdio fault injection; HTTP/FileService are fakes |

The upload target verifies actual 409 admission conflicts, 200 success, and 500 responses for
truncated input, stream error, flush and close failure. The recorder cancellation regression stops
before PCM capture and verifies that flush/close/remove failures stay errors. Forced overlapping
recordings exercise active-name collision recovery. The existing app-model target also passes all
277 cases in both modes. Camera/Recorder screenshots were reviewed for long paths, error states and
layout; the host fonts do not validate device typography. No desktop application code changed, so
this slice does not claim a new full desktop coverage, Electron E2E or cross-repository protocol run.

ESP-IDF **6.0.2** final incremental build passes. Main image: **7,013,664 bytes**, below the
13,959,168-byte `ota_0` slot. SHA-256:
`51e5cce984071dc83aaab26a60b9fcaedf8c8d824a4243acfb518d38a23c9106`.
The 16 changed production inputs match their pre-build hashes. `sdkconfig`, `dependencies.lock`
and the configured test public key are unchanged; Home test population remains OFF. Recovery was
not rebuilt. Generic IDF factory-partition flashing suggestions do not apply to this layout.

Rodak stores 74 source/test/config file hashes in `.codex-temp/media-save-source-snapshot.json`,
16 host binary hashes in `media-save-test-binaries.json`, and the main-image identity in
`media-save-firmware.json`. The sorted source map SHA-256 is
`1690d1dcc90811aa356919f6af8964e23334e0c475b6e8e33cd44d7213f5e749`.
Final logs are `media-save-file-writer-final.log`, `media-save-file-path-lease-final.log`,
`media-save-camera-capture-final.log`, `media-save-camera-ui-app-model.log`,
`media-save-recording-final.log`, `media-save-recording-final-asan.log`,
`media-save-recorder-ui-after-review-debug.log`, `media-save-recorder-ui-after-review-asan.log`,
`media-save-web-upload-final.log` and `media-save-idf-build-final.log`. Reproduction commands and
per-target fake boundaries are linked from [media save behavior](media-save.md).

No hardware, serial port, flashing, packaging or device NVS operation was performed. Physical SD
removal/slow-card behavior, camera/JPEG quality, codec and acoustic behavior, full LVGL exhaustion,
power interruption and eight-hour signed-OTA soak remain open. No new remote media effect or MCP
capability is introduced.

## 2026-10-07 media decode and display-allocation validation

Development package 013 reached the `A3.PNG` Retry action, but crash-ELF/termination review located
the abort in the concurrent `DisplayService` JPEG path after an unhandled `std::bad_alloc`. That
evidence did not identify the PNG as 16-bit and did not attribute the abort to PNG bit depth or the
LodePNG decoder overlay. Package 013 did not produce a stable PNG result. Package 014 later identified
the file as 69,200 bytes, 471 x 423, 8-bit color type 6 (RGBA); first open and two Retries all returned
LodePNG error 83 / `Not enough image memory`. Display JPEG frames continued and the device did not
abort, panic or reboot, so 014 fixed the fatal boundary without displaying the image.

The checked LVGL 9.3.0 overlay validates the component lock and exact LF-normalized hashes of 11
upstream files, then substitutes only the generated LodePNG source outside `managed_components/`.
Package 015 adds an eligible non-interlaced RGBA8 path that unfilters and compacts in the decompression
allocation and adopts it as the ARGB8888 draw buffer. `A3.PNG` then displayed on first open and both
Retries, but the retained image exposed a separate display-stream problem: the old JPEG path could
not sustain its frame copy, RGB888 input and frame-sized output scratch allocations together.

Package 016 removes that 153,600-byte JPEG-worker frame copy. It allocates one 230,400-byte buffer,
copies the RGB565 snapshot into it while holding the service lock, then expands RGB565 to RGB888
backwards in place after releasing the lock. The output scratch is fixed at 100 KiB, matching the
upstream 320 x 240 RGB888 example; output beyond that bound safely drops the frame and a newer frame
can recover. The application-owned heap-caps peak falls from about 614,400 to 332,800 bytes. The real
codec uses about another 46,080 bytes of PSRAM outside that application peak.

The focused software gate recorded through 017 passes in Debug
and ASan/UBSan with leak detection:

| Target | Cases | Boundary |
| --- | ---: | --- |
| DisplayService | 24 | Production capture/JPEG worker, in-place conversion, 100 KiB bound, allocation failures, locks, callback/stop and recovery; hardware/codec/LVGL scheduling edges use explicit fakes |
| Home UI | 43 | Existing real-LVGL Home and partial-flush display capture regression |
| Photos / ImageLibrary | 24 | Production UI/loader, real LodePNG/BMP and retained ARGB8888 ownership; ESP JPEG remains a fake |
| Camera / FileService | 21 | Production service/adapter; final-owner and unexpected-stop frame release, concurrent snapshots and stop publication; V4L2/JPEG/board dependencies are fakes |
| LodePNG overlay | 11 + 2 | Eleven valid 8/16-bit, RGBA filter and Adam7 variants including synthetic 471 x 423 RGBA8; two geometry/stride rejections, allocation-budget and three OOM/retry scenarios |

The 016 focused DisplayService target passed all 24 cases in both Debug and ASan/UBSan with leak
detection. Its complete release host runner passed 30 suites and 42 CTest cases, plus four Python
groups of 17, 15, 8 and 12 cases. ESP-IDF 6.0.2 built successfully. The separate 017 run is recorded below.

Historical package 015 used source `bf1bf248056a47e9a902b111f163afb60b0c2718` and directory
`build/packages/ota/20261007-100828`, task `media-png-inplace-015`, version `0.1.2-dev.1`. Its main
image is **7,108,624 bytes**, SHA-256
`3cf1e0b8566cf8f40f5fb9b0fefe9be699c26fe69bd8faecd4fac2be908f9eaa`; package ZIP SHA-256 is
`474d62301da21dbf94f10a9350d891852cfcedf35cf565d13824a4c03baf9b86`. A3 decoded successfully in
190 ms on first open and 260/185 ms on two Retries. Once its ARGB8888 image was resident, repeated
JPEG-stat windows reported 12 or 13 attempts with zero encoded and every attempt failed; only one
frame encoded during an image-replacement gap. Package 015 therefore proves PNG display, but fails
the concurrent display-stream result.

Historical source commit `dc2bff4d25b7b4637a010f600e68d4e3b90b0ccb` was packaged at
`build/packages/ota/20261007-103517` as task `media-display-stream-016`, version `0.1.2-dev.1`.
The manifest is a development-signed production flavor with Home test population and fault
injection disabled. `build/rodakos.bin` is **7,108,752 bytes**, within the 13,959,168-byte `ota_0`
slot; SHA-256 is `95ee90d27e97e39956e56f557e4d76c1ed1a4881df40315cc5bfb789151d0126`.
The package ZIP SHA-256 is
`97e48b12385c62fca24e3dd6bbbd7a61e38a22b00d832f098367030bc46b6c7c`. Recovery SHA-256 is
`ffa412ebe30c714c691bba73c8ab6e4efcaaab14fce5229f595707a8a08f75fd`, unchanged from package 015.

The identified COM3 device passed package verification, then received a non-Erase incremental flash
that preserved NVS. Recovery → main, Home startup and local OTA confirmation passed. After a clean
restart, five `A3.PNG` decodes completed successfully in 186, 180, 202, 191 and 184 ms. Screenshots
confirmed the first open, two Retries and two additional pressure repetitions all displayed A3.
Display JPEG statistics accumulated **34 attempts / 34 encoded / 0 failed**, with no abort, panic
or reboot.

This passes the targeted 013 abort, 014 error-83 and 015 display-stream regression sequence. It does
not establish complete resource stability. With the PNG resident, PSRAM free was about 532 KiB and
the largest block was usually 360-426 KiB, but one pre-replacement sample was 229,376 bytes, below
the 230,400-byte RGB888 allocation. The retained PNG remains ARGB8888; this change does not add
RGB565 retention. Keep arbitrary LVGL/CLIB OOM, SD failures, camera/voice/MQTT concurrency, resource
return under broader operation and the eight-hour identified-build soak open.

## 2026-10-07 Camera exit and PNG allocation validation

Source `860752e44474625fdbcd589b71e47b36374f4d21` fixes two independent memory-lifetime and
allocation boundaries. CameraService now frees its last RGB565 frame and clears `has_frame` when
the final local/remote preview owner stops, or when dequeue failure ends the worker. A surviving
owner retains preview access, and caller-owned frame snapshots remain valid. The worker publishes
its stopped handle only after its final service-state read and log, so Stop/destruction cannot race
that tail access. This does not claim that the real camera driver or IDF task cleanup can recover
from every low-memory condition.

The source-checked LodePNG overlay reserves 260 spare bytes with the known decompressed size,
matching the reviewed Huffman loop's requirement even after its end symbol. A 471 x 423 RGBA8
image has 797,355 bytes of filtered scanlines; the former exact-size reserve could still grow by
about 50% to 1,196,213 bytes at the end of inflation. The corrected request is 797,615 bytes, with
checked additions and error 83 on reserve failure. Unknown-size and custom-zlib paths are unchanged;
the PNG still owns an ARGB8888 draw buffer.

The Camera target passes 21 tests in Debug and ASan/UBSan with leak detection. The old production
source fails six updated cases; a mutation using vector `clear()` without releasing capacity also
fails six. LodePNG passes 11 valid variants and two geometry rejections, plus IDAT, inflate-reserve
and adopted-descriptor failure/recovery checks. Under a 1,081,344-byte contiguous allocation budget,
the old reserve really requests 1,196,213 bytes and returns error 83; the corrected decoder succeeds
with a largest request of 797,615 bytes and releases all tracked allocations. The synthetic PNG is
9,673 bytes; its 818,804-byte peak allocation ledger is not a physical peak measurement for the
69,200-byte A3 file. The complete release host runner separately passes **30 suites / 42 CTest**
and **17 + 15 + 8 + 12 = 52 Python** cases; key source hashes match before and after the run.

ESP-IDF 6.0.2 built the ordinary development-signed package at
`build/packages/ota/20261007-112648`, task `media-camera-png-recovery-017`, version `0.1.2-dev.1`.
The main image is **7,108,864 bytes**, SHA-256
`507eab226775a848afef1b4df3657b7d02f80d8d812b7a0597844ac052497175`;
package ZIP SHA-256 is
`3db39d56efcb388c71baa12493cde68c6de07b60dc9d0ca48dba37fb027a60eb`.
VerifyOnly and the non-Erase incremental flash preserved NVS, followed by successful
Recovery → main → Home and local OTA confirmation. Immutable assets match 016, including the
development Recovery/root; production-key migration was not performed. The original device ID,
bound state and tokenVersion=4 remain unchanged.

The first 017 attempts started Camera while screen sharing was already active. Both failed DVP DMA
allocation; the largest DMA blocks before the attempts were **6,656 / 4,352 bytes**. An A3 decode
that then succeeded in 185 ms followed a camera that had not actually started, so it is excluded
from the successful Camera-to-Photos result.

After stopping screen sharing, Camera really started. Screen sharing was reopened during the
preview, which ran for **32.304 seconds / 467 frames** before exit. A3 first open and two Retries
then decoded in **201 / 203 / 191 ms**, with device logs confirming display. The last Retry produced
a desktop control timeout before the eventual successful decode; this is not an all-ACK pass.
A second actual Camera run, again starting Camera before screen sharing, lasted **21.960 seconds /
332 frames**; after exit, A3 decoded and displayed again in **185 ms**. These are four successful
decodes after two genuine Camera previews, separate from the earlier failed-start path.

The same-board 016 comparison ran Camera for **24 seconds / 327 frames** before A3 returned
error 83; after stopping, PSRAM free/largest was **2,411,168 / 1,081,344 bytes**. The first completed
017 cycle's MQTT sample reported **2,561,940 / 1,507,328 bytes**. Both 017 cycles ended back at Home
with screen sharing and remote control stopped. Recorded periodic JPEG totals were **106 attempts /
106 encoded / 0 failed**; these are periodic sums, not a complete per-frame ledger. The continuous
capture contains no abort, panic or reboot.

The final two MQTT health samples reported PSRAM free **2,558,664 / 2,557,636 bytes**, largest
**1,507,328 bytes**; internal free **24,971 / 24,935 bytes**, largest **7,680 bytes**, and MQTT worker
minimum free stack **2,828 bytes**. The final voice health showed enabled=1 / listening=1, historical
internal minimum **131 bytes** and PSRAM minimum **418,800 bytes**. The internal minimum had been
235 bytes after the first cycle and fell further in the second; the final 7,680-byte largest block
also remains below the soak's 8 KiB threshold. These windows do not prove sufficient headroom,
absence of leaks or long-term stability. Final desktop state retained the original ID / bound /
tokenVersion=4, with MQTT connected and voice inactive. COM3 was released. Evidence is in Rodak
`.codex-temp/camera-resource-017/serial.log`, `result.json` and `device-final.json`.

Keep screen-first DMA/resource admission, late input and ACK/timeout handling, arbitrary OOM,
broader Camera/media/voice/TLS concurrency, long-term resource return, real SD/slow-card behavior,
physical touch/acoustics, actual power cuts and the eight-hour release soak open. This targeted
Camera-exit/PNG improvement does not change the production-release **NO_GO** decision.

## 2026-10-07 resource and ACK diagnostics 018

Source `d2517914e22337880b1ba38ee5013f143f994733` combines Camera allocation recovery (`5c973fd`),
original-peer ACK retry/cancellation replies (`c48d55a`) and JPEG-stage heap diagnostics. Camera
snapshot/codec/callback/state failures release their locks and owned resources; frame-copy failures
requeue driver buffers, and photo result strings are allocated before committing the file. This
does not cover arbitrary CameraApp/LVGL allocations, exhausted exception emergency storage,
real DMA/storage failures or IDF task-cleanup allocations.

ACK retries apply only to `WOULD_BLOCK`/`NO_MEM`, preserve FIFO and original instance ownership,
and stop after one second from the first send attempt or 50 attempts. They do not retry input
actions or promise a one-second browser-to-device result. Incomplete JSON is never sent; enqueue
OOM/overflow, permanent send error or budget exhaustion terminates the original peer. JPEG yields
to pending ACKs while the peer loop continues. Cancellation callbacks execute outside the input lock.
Pointer acceptance remains input-sample admission, not proof of a completed application click.

Focused tests pass in both Debug and ASan/UBSan/leak: **Camera 33, ACK 21, input 20, DisplayService 24**.
Six selected Camera cases fail against the old 017 production TU; ACK/input old-source controls
fail 8/4 cases respectively. These do not imply all old tests fail. The independent complete 018
runner passes **30 suites / 42 CTest / 52 Python**; adding the two ACK/input suites gives
**32 suites / 44 CTest**. All **682** recorded source hashes are identical before/after the run,
which completed before 019 source changes. Evidence is WSL
`~/.cache/rodakos-release-media-018/result-018.json`, `runner.log` and
`source-hashes-before.json` / `source-hashes-after.json`; older copied files in that directory do
not establish new results. ESP-IDF 6.0.2 build passed.

Development package `build/packages/ota/20261007-120809`, task `media-resource-ack-018`, version
`0.1.2-dev.1`, contains a **7,116,480-byte** main image, SHA-256
`d2e4b2447834bd018a25530c7f93f57f8c81e12bb8948e50a8b730ab3a490db8`; ZIP SHA-256
`8d10f136bf99280d446ead87245f2f454c44d41c0d6fde4943c20ec49529fff2`.
Home test population and fault injection are disabled. VerifyOnly, NVS-preserving incremental
flash, Recovery → main → Home and OTA confirmation passed. Production flavor is not production
signing-root acceptance.

The first screen-first Camera window reached first frame at device time **34,767 ms**. Its last
serial line at **77,897 ms** was `Closing app: camera`; output remained silent until a controlled
RTS reset. No panic line was captured, but Camera exit was not confirmed and the failure is not
classified as a proven deadlock or a successful recovery.

In the separate post-reset window, Camera ran **18.080 seconds / 243 frames**, stopped, and A3
decoded/displayed in **193 / 190 / 192 ms**. For the third attempt, down83 was accepted after
**7.745 s**, up84 was rejected with `control_disabled` after **7.741 s**, and automatic disable85
was accepted after **4.741 s**. Earlier down/up79–82 replies took **475 / 608 / 932 / 1,086 ms**.
Three decoded images do not prove three accepted click sequences. The relationship between
cancelling a held pointer, a synthesized release and a later LVGL click requires a separate fix
and real-LVGL/device validation; it must not be silently attributed to ACK transport loss.

The first JPEG stage sequence observed DMA free **14,983 → 6,899 → 6,863 → 14,947 bytes** across
before-open/open/process/close, with largest **8,192 → 5,376 → 8,192 bytes**. The open/close pair
accounts for an observed **8,084-byte** transient internal allocation. This identifies an active
resource competitor, not a unique explanation for every Camera failure. **No JPEG allocator
change is present in 018.** Periodic heap minima are independent fields; only the largest-DMA
minimum has the recorded timestamp/sequence. Output scratch remains live at after-close.

After returning Home and stopping screen/control, the two MQTT samples report internal free
**36,075 / 36,039 bytes**, largest **11,776 bytes**; PSRAM free **2,552,400 / 2,552,436 bytes**,
largest **1,409,024 bytes**, and worker minimum stack **2,956 bytes**. Wake remains listening;
its historical internal minimum is **47 bytes**. The changed final largest block follows a
different reset/exercise history and is not evidence of a DMA-capacity fix. Original device ID
`c78845a8-06c9-4dcd-b7ff-d33e599f23ff`, MAC `44:1b:f6:c3:b4:30`, bound/tokenVersion=4 and MQTT
connection are retained; voice is inactive.

Evidence: Rodak `.codex-temp/resource-window-018/serial.log`, `after-reset/serial.log`,
`after-reset/control-final.json` and `device-final.json`; the earlier 017 complete-RX comparison
is in `.codex-temp/resource-window-017/`. Its Retry2 seq72/73 had no ACK or third device load,
distinct from 017's earlier delayed-but-eventually-decoded run. See [media browsing](media-browsing.md).
Camera-exit stalls, delayed input, cancellation/release semantics, JPEG/DMA pressure, broader
OOM/media/voice/TLS/storage concurrency and eight-hour soak remain open. Release stays **NO_GO**.

## 2026-10-07 cancelled-gesture correction 019

Source `42b12ccd183577a331433a69218c8fcc6191a0ee` fixes a real-LVGL reproduced cancellation
boundary: once a remote down has been read, dropping its queued up and emitting a normal RELEASED
sample can still dispatch CLICKED. The explicit rejection ACK added in 018 does not itself reset
LVGL's held gesture. This mechanism does not identify the cause of every earlier transport/UI delay.

Every cancellation now advances its generation, including cancellation between dequeuing up and
final admission. The controller delivers a cancellation release before dequeuing a new press;
the bridge resets the previously delivered remote gesture on the LVGL thread and lets prev_state
return to RELEASED before another read. Physical takeover also resets the old remote gesture when
the physical sample is published before OnLocalTouch. Normal up/click, physical-only clicks,
reenable/replacement and original-instance isolation retain separate tests and admission semantics.

Production input/bridge with real host LVGL passes **30 cases in Debug and ASan/UBSan/leak**.
ACK **21** and Home **43** pass ASan. A conditional-generation mutation fails **2** cases; removing
the LVGL reset fails **8**. Frozen source hashes and logs are in WSL
`~/.cache/rodakos-cancel-019-evidence/`; mutation logs are in `rodakos-cancel-019-negative/`.
The accompanying `13b8d3f` adds Camera destruction/worker/STREAMOFF/fd-close/device-release phase
logs, with **33 Camera Debug cases** passing. These logs do not establish a Camera-exit stall fix.
The complete 018 **32-suite / 44-CTest / 52-Python** run cannot be reused as a complete 019 run.

Package `build/packages/ota/20261007-122610`, task `media-control-cancel-019`, version
`0.1.2-dev.1`, contains **7,119,152 bytes**, main SHA-256
`665fffea5212885d839290bebaec20500d7690367b80cfb89436281cd454e278`; ZIP SHA-256
`e842df1f271f8c413a678ca479de4f37ebd64427a4789019a11811ed6fd99799`.
It is a normal production-flavor, development-signed package with Home test population and fault
injection disabled. VerifyOnly, the NVS-preserving refresh and Recovery/main/Home/OTA confirmation
passed with the original bound/tokenVersion=4 and MQTT connected. No JPEG allocator migration
is included.

The 019 screen-first Camera attempt still failed with a **6,656-byte** largest DMA block. After
screen sharing stopped, Camera recovered and ran **56.779 seconds / 792 frames**. Its exit completed
UI cleanup, STREAMOFF, fd close, device release, worker stop and audio release. This successful
exit does not locate or close the separate 018 stall.

A3 first opened in **189 ms**. In a targeted cancellation trial, held-down93 was accepted after
**1,188 ms**, then UI disable produced up94 rejected as `control_disabled` after **20 ms** and
disable95 accepted after **22 ms**, with no additional photo load/decode. After reenable96,
normal retry97/98 was accepted after **800 / 860 ms** and A3 decoded in **190 ms**. This establishes
the bounded cancelled-gesture correction and subsequent normal-click recovery on the device.

The next pressure Retry still timed out: down99/up100/disable101 were logged at the controller's
parsed-input entry at the same device time **187,345 ms**. Down/up were rejected as
`control_disabled` after **3,870 / 3,869 ms**, disable was accepted after **868 ms**, and no third
PNG load occurred. Host receipt of the matching serial entry logs was **3,861 / 3,860 / 857 ms**
after TX, followed by ACKs **9–11 ms** later. There was no recorded `control ACK retry`; this
narrows the dominant wait to before the controller entry, with SDK/network/scheduling still to
separate. USB/log buffering is included in host receipt timestamps, so these are not exact
packet-network timings. It is not an all-input pass.

Evidence is Rodak `.codex-temp/resource-window-019/serial.log`, `serial-timing.jsonl`,
`control-final.json`, `result.json` and `device-final.json`. Recorded periodic JPEG totals are
**148/148/0**, not a complete per-frame ledger. No panic/abort/reboot was captured, and no extra
restart was used for closeout. The device returned Home with screen/control stopped and COM3
released, preserving its original ID/bound/tokenVersion=4, MQTT connected and voice inactive.
Final internal free is **33,159 / 33,071 bytes**, largest **7,680 bytes**; PSRAM free
**2,553,000 / 2,553,032 bytes**, largest **1,343,488 bytes**. MQTT/supervisor minimum stacks are
**2,956 / 2,424 bytes**, wake is enabled/listening, and historical internal minimum is **467 bytes**.
The final internal largest still misses the **8 KiB** soak threshold. Release remains **NO_GO**
for outstanding control latency, the 018 Camera-exit diagnosis, DMA/resource concurrency,
physical, signing and soak gates.

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

## 2026-10-07 peer timing diagnostics 020

Source `92eb23878c7611ce4d06154851ad90cb50f386ec`, package `20261007-130521`,
task `media-peer-timing-020`, version `0.1.2-dev.1`:

- Main: **7,125,184 bytes**, SHA-256 `cf64d594ce3e8d785ecc2600d39ec15b3c55cd6d27067d06aec08592127d46bb`.
- ZIP SHA-256: `f71e66f6dd66ff2b2f8610b891d62eca3c43dce16bfa5567806571eccd0c2b1c`.
- Five immutable assets match 019; original development signing root retained. VerifyOnly,
  NVS-preserving flash and Recovery/main/Home/local OTA confirmation passed.
- ACK/timing 28 and production MQTT command/stream 57 each pass Debug and ASan/UBSan/leak;
  independent source review and ESP-IDF 6.0.2 build pass. This is diagnostic software,
  not a change to timeout, authorization, reliable ordering or JPEG allocation.

Pure Photos reproduced 7,908/7,904 ms rejected replies and the later disable's 4,900 ms
reply. Three device callback entries span only 3.092 ms; each entry-to-return is about
1.5 ms. The relevant independent maxima are loop gap 80.100 ms, SDK 62.950 ms,
service wait 24 us and API wait 6 us. No 100-ms MQTT slow-gate sample appeared.
This narrows the measured wait to delivery before the application callback, not a proven
UDP/WiFi/DTLS/SCTP cause. Browser/USB arrival/device clocks are distinct; nested phase
maxima are not additive. Camera is not required to reproduce this timeout.

Camera then ran 77.997 seconds / 1,197 frames and completed all exit stages, but a screen
start during preview caused AES allocation failure, MQTT queue drops and reconnect.
After Camera, static Photos produced one pre-open dropped frame and no usable screen;
this failed attempt did not exercise remote pointer cancellation. Final Home/screen-off
MQTT reports internal free 30,203 B / largest 7,680 B and PSRAM free 2,550,776 B /
largest 2,359,296 B; historical internal minimum is 151 B. Original binding/token4
remain, MQTT is connected, the voice connection is inactive and wake listening remains enabled; COM3 is released. **NO_GO** remains.

Full sequence and boundaries are in the [media record](https://github.com/rymcu/rodak/blob/master/docs/media-browsing-verification.md#020-控制入口分段与资源失败).
Raw evidence: Rodak `.codex-temp/resource-window-020/`; build/package/verify/flash logs:
`build/logs/media-peer-timing-020-*.log`. Neither this run nor its separate allocator
proposal closes 018's stall, arbitrary OOM, physical touch/audio or eight-hour soak.

## 2026-10-07 scoped screen JPEG PSRAM validation 021

Source `6cb19f5bd3b0a50e30d6047c7edfc5673be876bc`, package `20261007-134645`,
task `media-jpeg-psram-021`, version `0.1.2-dev.1`:

- Main: **7,125,712 bytes**, SHA-256 `702cdb576c2fce3092b0e8857439afcbbc77dd3f561b2c200d2ad65132d98d8b`.
- ZIP SHA-256: `d7259288b9baed0c6cc223dcf7dd6e0eda8761de81348815a9c9afa5530eda3b`.
- Original development signing root and all five immutable assets are retained. Production
  flavor, Home hardware-test population and fault injection OFF. ESP-IDF Build3, final
  ELF/map gate, VerifyOnly, NVS-preserving flash and Recovery/main/Home/local OTA confirmation pass.
- DisplayService **30**, Home **43** and allocator **10** cases each pass Debug/ASan/UBSan/leak;
  checker **26** cases and independently reviewed real final-ELF positive/bypass-negative
  checks pass. Native TLS is **32 aligned bytes per task**, delta **0** against frozen 020.
  These focused checks do not replace 018's separately identified full host run.

Only `DisplayService::EncodeJpeg` enters the task-local PSRAM-only allocation scope. All four
codec allocator entrances are covered; PSRAM failure does not fall back to INTERNAL. Camera/
decoder calls outside the scope retain their original behavior and `task_enable=false` stays
unchanged. Final-link inspection verifies actual calls and archive identity; it is not a
whole-program control-flow proof or a hardware resource guarantee.

In the first hardware window, JPEG seq2 has same-frame DMA free samples of
**14,527 → 14,527 → 14,563 → 14,563 bytes**, with largest **8,192 bytes** throughout.
Open decreases sampled PSRAM free by **38,688 bytes**, and close increases it by the same amount.
These shared-heap net samples support no DMA-free loss at this frame's open, not exclusive
allocator accounting. Periodic `JPEG heap min` columns remain independent minima.

With screen already active, Camera starts at logger **69,689 ms**, with DMA free/largest
**32,311 / 16,384 bytes**, and its preview screenshot is normal. Switching to Photos initializes
the new page, but Camera teardown stops at **133,299 ms / `CloseStream: STREAMOFF begin`**.
The final observation records **127.583 seconds** of serial silence; no STREAMOFF completion,
device release or preview-stopped marker follows. A controlled RTS reset is required.
This is a **failed Camera exit and Photos transition**, not a successful exit inferred from
page initialization or a previously captured image. The first window contains no pointer TX,
so it does not verify Photos clicks/cancellation. Its boot's historical internal minimum is **59 bytes**.

The separate post-reset window loads A3 successfully twice, in **188/187 ms**. A later Retry's
down124/up125 are rejected after **3,535/3,533 ms**, with no additional PNG load. Held down128
is accepted in **117 ms**; cancellation rejects up129 in **22 ms** without an extra load.
After reenable131, down132/up133 still time out and are rejected after **3,197/3,193 ms**.
There is no successful normal Retry after that reenable. The allocation change does not fix
UDP/SCTP delivery latency or establish static-page first-frame availability.

The recovery window returns Home at logger **170,618 ms**, stops screen/remote control,
restores diagnostic hooks and captures through **269,818 ms** (about 99 seconds of closing
observation); COM3 is released. Final MQTT health at **246,478 ms** reports internal
free/largest **21,083 / 8,192 bytes**, DMA **19,307 / 8,192 bytes**, PSRAM
**2,569,488 / 1,507,328 bytes**, and MQTT stack minimum **2,772 bytes**. Main health at
244,838 ms reports internal **21,307 / 8,192 bytes**. Wake at 243,688 ms remains enabled/listening,
with supervisor stack minimum **2,424 bytes** and this boot's historical internal minimum
**651 bytes**. The original device ID, bound state and tokenVersion=4 remain; MQTT is online
and the voice connection is inactive. The first boot's 59-byte and recovery boot's 651-byte
minima are separate histories, not evidence of an improvement in a shared low-water mark.

**NO_GO** remains. Recovery after reset does not erase the first-window stall. Camera STREAMOFF,
late control, static first frame, arbitrary OOM, full audio/TLS/media concurrency, physical touch
and eight-hour soak remain open. The bounded first-frame DMA observation and screen-first
Camera startup are accepted only within this identified package's measured window.

Raw evidence: Rodak `.codex-temp/resource-window-021/first-window/` frozen inputs and
`after-reset/` final result/control/serial/device files. The final silence duration comes from
`first-window/stall-final.json`, superseding the earlier 49-second observation without erasing
it. Build/package/verify/flash logs are `build/logs/media-jpeg-psram-021-*.log`; linked evidence
and host source hashes are in Rodak `.codex-temp/jpeg-allocator-020/integrated/` (the directory
name reflects its proposal origin, not a claim that the change was present in 020).
See [media browsing](media-browsing.md#021-屏幕-jpeg-作用域-psram-分配与实测边界).

## 2026-10-07 Camera teardown diagnostics 022

Source `dc847b203af53ff25d1fabcf266d442fa2f063d7`, development package
`20261007-150728`, task `media-camera-teardown-022`, version `0.1.2-dev.1`:

- Main: **7,126,768 bytes**, SHA-256 `f5253bdc01060be93a76b315599c4bba590475700de91efb2d112e7567ba0dda`.
- ZIP SHA-256: `96631f18d121ee92d6609bb1b61003c83722b618e54227afef1cb8a800d7d6ae`.
- ELF SHA-256: `a610db7144e48058306f8fc90123a6d399f2e1ddd153b66ab977c0d133e66db4`.
- All five immutable assets match 021; original development signing root, production flavor,
  authority-v3 implementation and disabled fault/Home-test flags remain.

ESP-IDF 6.0.2 Build3 and the post-commit build pass both final-ELF gates. The new recorder
uses a **536-byte internal-DRAM object at 0x3fca84e8** and a 95-byte IRAM function with
37 reachable instructions, one CAS, no calls or backward branches. This exact address is
valid only for the frozen ELF above. Three analysis-only ELF mutations are rejected.

Focused Debug/ASan/UBSan/leak checks pass: the original 33 Camera tests plus four service
blocking cases, 12 recorder/ABI cases, and 26 driver-overlay CTests including 11 generator
tests. The linked checker has 18 Python cases. Recorder TSan passes with process-local
`setarch x86_64 -R` after the initial WSL runtime-mapping failures; no firmware or global
system setting changed. See the [diagnostic contract](camera-teardown-diagnostics.md).

The complete local release runner and four additional suites pass **37 independent suites /
91 CTests** under ASan/UBSan/leak. Python checks total **107**: 55 inside CTest (camera checker
18, overlay 11, JPEG checker 26) and 52 standalone. All four JPEG source-negative controls
are detected. Eight isolated cold-downloaded components pass content hashes without changing
the lock or project manifest. The original run identity records the pre-commit HEAD; separate
source/manifest checks tie the tested code to `dc847b2`, without rewriting that earlier identity.

COM3 `VerifyOnly` first matched the installed Bootloader, partition table and Recovery
digests. The subsequent NVS-preserving 022 flash exits 0 in `flash-022.log`, with
Recovery → main, OTA confirmation and Home boot verified. The device ran the frozen
022 ELF above in these windows; no NVS erase, unbind or credential rotation occurred.

The earlier missing-MI02-GUID/OpenOCD-init failure is historical. Administrator installation
of the official candidate succeeds (PnPUtil exit 0, MI02 `oem26.inf`, debug GUID enabled),
while MI00/COM3 retains usbser. Authenticode/catalog membership validation passed, but the
recorded `/kp` check still exits 1; successful installation must not be relabelled as WHQL
or kernel-policy validation. OpenOCD requires the exact uppercase serial
`44:1B:F6:C3:B4:30`. Its capability probe on 021 exits 0 after reading both cores and fixed
DRAM; a controlled reset then restores Home/OTA confirmation and fresh MQTT. This establishes
access capability, not disturbance-free recovery. See `driver-capability-result.json` and
`driver-review/repair-20261007-075245.json`.

The hardware windows are separate:

- **First window:** Camera runs **28.300 s / 380 frames** before Camera→Photos. Serial
  confirms STREAMOFF, fd close, device release and worker-stop completion. Only afterward
  does JTAG read **24 committed records, 0 pending and 0 drops**, including IOCTL_RETURNED,
  BEFORE_LOG and AFTER_LOG. Two new post-read halts occur at CPU1 **0x420045D0** and CPU0
  **0x422EBE91**. Offline symbolization of the frozen ELF identifies the cache-error panic
  busy-wait branch and the `ESP_SEMIHOSTING_SYS_PANIC_REASON` breakpoint. Serial then becomes
  silent and screen sharing disconnects. Final `running` samples do not establish healthy
  recovery; the first panic's specific trigger is not proven. The original failed
  `decoded.json` is retained; `decoded-fixed.json` accepts the real multiline registers but
  explicitly reports `health_after_resume=not_established_by_decoder`.
- **Second window:** an RTS reset attempt has **0-byte serial/timing logs**. It is not a
  Camera run or another fault reproduction. The subsequent `recovery-reset-esptool.log`
  records standard USB ROM/stub connection, flash-ID query and reset, without Flash writes.
- **Third window:** a separate recovered boot has **no JTAG attach**. Camera runs
  **88.744 s / 1,171 frames** and exits through STREAMOFF and subsequent cleanup normally.
  After screen stop and the Home command, serial data continues for **147.311 s**. This is
  the exact difference between the Home host timestamp in `actions.jsonl` and the last byte
  timestamp in `serial-timing.jsonl`, not an eight-hour soak. Main, Touch, Voice and MQTT
  health plus new PC-status messages continue. The 08:30:42 UTC server snapshot records new
  08:30:31 UTC MQTT state, the original ID/bound/tokenVersion=4 and inactive voice connection.
  Late MQTT health has internal free/largest **33,787 / 14,336 B** and PSRAM free/largest
  **2,555,596 / 1,605,632 B**; this boot's historical internal minimum is **107 B**.

022 changes observation only, not driver teardown lifetime. Two normal exits do not erase
021's **127.583-second** STREAMOFF stall/reset, establish its root cause, or prove resource
sufficiency. The first window's post-capture panic is debugger-perturbed evidence, not a
reproduction of that pre-existing stall. Normal windows are no longer repeatedly attached.
Debugger halt modifies watchdog state, and this OpenOCD version's poll can process a
semihosting breakpoint and resume internally. A future observer should reject any new
post-capture halt as successful recovery and avoid additional per-core resume attempts;
that alone does not prove the first panic can be prevented.

Evidence is in Rodak `.codex-temp/resource-window-022/`: frozen artifacts, package/immutable
comparison, driver/flash/reset records and the three windows. `first-window/jtag-resume-audit.{md,json}`
records exact PC/ELF/source identities without changing raw or the old observer. The multiline
decoder repair has **28 offline decoder tests + 5 Tcl mocks**, separately counted from hardware.
At the end of 022, **NO_GO** remains for Camera teardown, resource recovery, late control,
static first frame, audio/TLS/media concurrency and eight-hour soak. The subsequent 023
section records the bounded static first-frame fix without closing the other gates.

## 2026-10-07 Static screen first-frame validation 023

Source `8238a5022724f1562c22bae6c3669717ab988a4b`, development package
`20261007-164049`, task `media-static-first-frame-023`, version `0.1.2-dev.1`:

- Main: **7,128,192 B**, SHA-256 `02bbe03e6a74d57035f6971e237a47dd4414416d0ae506112a9c23bf313bd2d4`.
- ZIP SHA-256: `1fdcea8783da8a375fde8d727e28ccb2ae5a5b18ba8998692f3997a04a7a9695`.
- Frozen ELF SHA-256: `3f141a42e08b0a9eb5ceb17579b79f4c0fca2d883ffa3e7b26247f972d6e48a4`.
- All five immutable assets and the original development signing root match 022. Production
  flavor retains disabled Home-test/fault flags. `flash-023.log` exits 0, preserves NVS and
  verifies Recovery/main/OTA-confirmation/Home; original ID, bound state and tokenVersion=4 remain.

The change retains one latest pending compact JPEG before the video data channel opens.
It moves ownership, replaces old pending storage and returns capacity before Stop publishes
completion. One JPEG can now reside during the handshake and overlap another encode; this
is not a zero-memory-cost claim. MQTT supplies the same actual stream lease to WebRTC and
control. Null/revoked leases fail Start; each video fragment/retry and the ACK after JSON
encoding checks the original peer/stream/lease immediately before SDK entry. An already
admitted SDK call may complete. Input authorization, timeout, ordering and cancellation
rules are not relaxed, and video open does not enable remote input.

Directed checks compile the complete production WebRTC TU: **45/45** cases pass in Debug
and ASan/UBSan/leak, including real vector deletion/capacity, replacement, no-new-frame and
in-flight Stop/Start. Each build detects **5/5** source-negative controls: old preopen drop,
retained `clear()` capacity, missing video/ACK final lease checks and an ACK check incorrectly
moved before encoding. The MQTT suites pass **104 cases**.

The complete local 023 host regression passes **37 unique suites / 92 CTests / zero failures**:
33 release suites and three extras ran anew; the ACK suite reuses the same-source final
directed ASan records for its two CTests, without double-counting. Positive suites use
ASan/UBSan/leak. Python checks total **107** (52 standalone and 55 within CTest), with
**21 CI-helper cases** counted separately. All four JPEG allocator Debug negative controls
are detected. LF hashes for 12 related production/test files match commit `8238a502`.
Initial missing-PyYAML/component-manager logs are retained; temporary Linux dependencies
were installed and only the helper/negative tail reran, without changing production code
or repeating completed suites. Counts and source identities are recorded in
`host-checks/summary.json` and `host-checks/source-verification.json`. These software checks
do not establish remote delivery or rendering.

Hardware uses the same static Photos page and a configured **2.5 s offer delay**:

| Identified window | Device transport | Browser evidence |
| --- | --- | --- |
| 022 baseline | rx=1, sent=0, drop=1 | Both video/control channels open, zero JPEG messages, existing 15 s first-frame timeout, no image |
| First 023 Start | rx=1, sent=1, drop=0 | One 13,094-byte message, 39.9 ms after video open; 13,089-byte JPEG plus 5-byte header; actual 320×240 render |
| 023 Stop then fresh Start | rx=1, sent=1, drop=0 | One same-length message, 77.0 ms after the new video open; actual 320×240 render again |

Normal Stop separates the sessions, no extra source frame is needed, remote control remains
off and there is no JTAG attach. Browser event/image records and saved pixel/page images
provide delivery/render evidence beyond SDK counters. These two latencies are observations,
not a general network bound or another Camera/A3 decode test.

After final screen stop/Home, serial continues for **323.086 s**, with no detected reset/panic
markers. Late Main health reports internal free/largest **20,875 / 8,192 B** and DMA
free/largest **16,507 / 8,192 B**; Voice health reports PSRAM free/largest
**2,565,444 / 2,490,368 B** and this boot's historical internal minimum **2,311 B**.
Main/MQTT/Wake minimum free stacks are **2,640 / 2,860 / 2,424 B**. These separate samples
are not a controlled heap comparison with 022, which had prior Camera use. The final
08:52:19 UTC server snapshot has fresh 08:52:15 UTC MQTT state, original bound/tokenVersion=4
and inactive voice. Screen sharing is stopped, control disabled, browser hooks restored and
COM3 released.

The 023 serial log still contains **15** `MQTT message dropped: worker queue full or allocation failed` messages;
the combined error does not distinguish queue pressure from allocation failure. The bounded
static first-frame regression passes, while Camera teardown root cause, late control,
arbitrary resource pressure, audio/TLS/media concurrency and eight-hour soak remain **NO_GO**.

Evidence: Rodak `.codex-temp/resource-window-023/` contains `package-evidence.json`,
`directed-test-evidence.json`, `hardware-result.json`, flash/build records,
`baseline-static-022/`, and `static-023/` with its independent `restart/` browser records.

## 2026-10-07 MQTT queue diagnostics and pacing comparison 024

Flashed firmware source is `aebd5e6c60886ab1245220dcde880cd1f4a7f4cd`, package
`20261007-180119`, task `media-mqtt-diagnostics-024`, version `0.1.2-dev.1`:

- Main: **7,128,640 B**, SHA-256 `cfc4cc731fe4927f54fed085af1e57b8672d3e9ee9d53b34f6347456cd53a795`.
- ZIP SHA-256: `aecb34038688e70d1418083d5c52ab523bd6e3dbc91d3b7ee2be13bdbd9ea478`.
- Frozen ELF SHA-256: `db19c4221768c39921105b27f210548c2284ccda62fed86f8d7c9306e3bcdf04`.
- The five immutable assets and original development signing root match 023. Production flavor
  does not mean production signing; Home-test and fault-injection flags remain disabled.
- `flash-024.log` confirms NVS-preserving flash and Recovery/main/OTA-confirmation/Home checks.
  The original device ID, bound state and tokenVersion=4 remain. `package-evidence.json` was
  frozen before flashing and retains its original `firmwareFlashed=false`; the later flash log
  and identified device windows supply the hardware evidence.

The firmware change adds diagnostics only. Inbound drops distinguish `object_alloc_failed`
from `queue_send_rejected`, using the actual nothrow allocation and single zero-timeout send
results. Topic/payload byte lengths are captured before move. Object size, monotonic timestamp,
queue depth and default-heap free/largest are failure-adjacent samples, not one atomic snapshot
or a guarantee of allocator capacity. Outbound diagnostics distinguish `count_limit` from
`byte_limit` under the existing lock, retaining count-first precedence. The 8-slot inbound
queue, 8-item/128 KiB publication budget, 64 KiB single-publication limit, RAII, connection
epoch and stream-lease checks are unchanged. Logs contain no message body or credentials.

Directed MQTT validation passes **115 positive cases** in each Debug and ASan/UBSan/leak
build, with **five CTests per build** and all **six source-negative controls** detected.
The 11 new cases exercise exact object-allocation failure, real queue saturation, a worker
drain before the depth sample, successful admission without false drop logs, release/recovery,
and outbound count/byte limits. Host object size is not assumed to equal the ESP32-S3 size.
These are focused checks: a fresh complete 024 local release runner has not been run, and no
new 024 GitHub CI acceptance is claimed here. Earlier 023 full-run totals remain attached to
023; later CI-only commits do not change the identity of the flashed 024 firmware.

Four separate static-Photos sessions use the complete browser candidate set: **18 candidates
(9 UDP, 9 TCP) plus one SDP command**, retaining candidate order. The paced experiments only
insert a temporary **200 ms** candidate interval; no candidate, interface, address family or
port is filtered. The 023 pair shares its 023 boot; the 024 pair is a separate 024 boot.

| Window | Command snapshot: ACK / only delivered | Inbound drops | Outbound full-limit logs | Failed-to-ACK logs | SDK remote-candidate-limit logs |
| --- | --- | --- | --- | ---: | ---: |
| `baseline-023` ordinary burst | 10 / 9 | 8 legacy combined warnings; reason unknown | 1 legacy combined limit warning | 1 | 0 |
| `paced-023` temporary 200 ms | 19 / 0 | 0 | 0 | 0 | 8 |
| `burst-024` ordinary burst | 8 / 11 | 4 `queue_send_rejected`; 0 `object_alloc_failed` | 10 `count_limit`; 0 `byte_limit` | 7 | 4 |
| `paced-024` temporary 200 ms | 19 / 0 | 0 | 0 | 0 | 8 |

All four windows render a complete **320×240** image and contain no detected reset/panic
markers. `delivered` is the saved command status at the snapshot, not a successful device ACK
or an inferred final failure. Counts come from each exact session; the larger unfiltered
command collections also contain other sessions and are not used as the table denominator.

In `burst-024`, every inbound rejection has an adjacent depth sample of **8**, with
`object_bytes=64`, default free **1,659,204–1,662,780 B** and largest **1,474,560 B**. The four
events are queue-send failures by their actual branch, not object allocation failures inferred
from heap values. The ten outbound events record count **8** and only **820 or 837 B** of
queued payload, confirming the count limit rather than the 128 KiB budget for those events.
This does not reclassify historical 023 merged warnings or rule out other allocation failures.

The paced windows support investigating backpressure, but fixed 200 ms is an experimental
control, not the product solution. Nineteen ACKs prove control-plane software handling;
`Remote candidate over limited 10` still appears eight times, so SDK candidate admission and
capability remain a separate unresolved boundary. No protocol/address/port filtering is
inferred from these tests. The independent production-path result is recorded below.

The `paced-024` final snapshot at **10:14:15 UTC** has fresh **10:13:53 UTC** MQTT state,
original ID/bound/tokenVersion=4 and inactive voice. Late Main internal free/largest is
**20,927 / 8,192 B**, DMA **16,179 / 8,192 B**; the separate Voice sample has boot internal
minimum **1,979 B**. These bounded observations are not a controlled heap comparison across
boots or proof of sufficient concurrent headroom. Camera teardown, delayed remote input,
arbitrary OOM, audio/TLS/media concurrency and eight-hour soak remain **NO_GO**.

Evidence: Rodak `.codex-temp/mqtt-diagnostics-024/` contains `package-evidence.json`,
`test-evidence.json`, build/flash logs, `baseline-paced-comparison.json`, and the four named
windows with `summary.json`, browser images, serial logs and device snapshots. Exact command
records are in each identified window's `exact-session-commands.json` where present; the 023
pair's scoped count comparison is also frozen in `baseline-paced-comparison.json`.

### Production ACK-paced session and separate restart failure

The unique desktop instance was restarted to load the identified main bundle SHA-256
`3e6a279760b75c7f707902d0076d5cffbc3f17d7ddf11e1805707285db1a374f`, based on Rodak
`02b8b81dff55f5cd2214ffb6ce4a2d9f52e6c679` plus the files in `desktop-production-freeze.json`.
This bundle identity is separate from the unchanged device firmware `aebd5e6`.

The initial desktop-restart window `production-024` must remain a failure record. MQTT
credential recovery logs `MQTT client reconnected during refresh; restart required` and
`Restarting to isolate refreshed MQTT session`; the device restarts itself and old persistent
signaling commands are replayed after reconnect, producing **41 `queue_send_rejected`** events.
There is no new flash or manual reset in that window, and binding/tokenVersion=4 remain.
This is separate from the four earlier comparisons and the following fresh signaling session.

`ack-bound-024`, session `97fbf71e-2c53-40fb-95e7-2b403d9b7599`, uses the production
main-process queue with **zero test candidate delay**. It retains all 18 candidates
(9 UDP, 9 TCP) and one SDP. Metadata records **19 waiting / 19 acknowledged** and confirms
each previous ACK precedes or equals the next waiting timestamp, with one request in flight.
The sequence runs from **10:23:11.051 UTC** to **10:23:30.400 UTC**, or **19.349 s**.
A complete **320×240** frame is actually rendered. This is not a general latency bound.

That window has no inbound/outbound queue-limit, object-allocation-failure or failed-ACK log,
but still has **eight** SDK `Remote candidate over limited 10` messages. After Stop and Home,
serial observation continues for **79.967 s**, with no detected reset/panic markers. Remote
control stays disabled, no JTAG is attached and COM3 is released. The final **10:25:29 UTC**
snapshot has new **10:25:11 UTC** MQTT state, original ID/bound/tokenVersion=4 and inactive voice.
Late MQTT health reports internal free/largest **20,295 / 8,192 B** and PSRAM free/largest
**2,567,820 / 2,490,368 B**; Voice reports this restarted boot's internal minimum **2,131 B**.
That low-water mark must not be compared with older boot histories as an improvement.

Evidence is `ack-bound-024/hardware-result.json`, `exact-session-signals.json`, the metadata-only
ephemeral audit, browser image and serial records. This bundle does **not** include the later
legacy persistent-signal replay cleanup. Its 19/19 result verifies the new session's bounded
control-plane delivery, not historical replay removal, SDK support for all candidates,
physical control/audio, Camera teardown or long-duration capacity. **NO_GO** remains.

### Legacy replay cleanup and final desktop verification

The subsequent desktop build rejects persistent camera/display signaling at creation, HTTP
pull, MQTT replay and final publication. It follows the firmware's command/type precedence;
ordinary commands and existing terminal records retain their behavior. In `legacy-cleanup-024`,
the actual database's 315 known signal records change from 148 acked / 41 delivered / 126 failed
to 148 acked / 167 failed. All 41 carry the replay prohibition reason, and the original 274
terminal records are unchanged. No database record was manually edited or deleted. That
reconnect window has no queue-drop or failed-ACK log, but still includes one firmware-initiated
credential-refresh isolation restart. Desktop cleanup does not fix that firmware behavior.

`final-ack-024` uses desktop main bundle SHA-256
`bcc18f44f64fb473f71301668b90bc917790d90a3374d08c6b2fc25604ca8859` with unchanged 024 firmware.
With zero candidate delay, it again has 19/19 sequential ACKs and a rendered 320×240 image;
the sequence takes 19.304 s, and eight SDK remote-candidate-limit logs remain. Stop/Home is
followed by 98.936 s of serial observation without reset/panic or queue-drop/failed-ACK markers.
Final MQTT internal free/largest is 21,287 / 8,192 B, PSRAM is 2,568,632 / 1,572,864 B, and the
boot's internal minimum is 1,711 B. The original ID/bound/tokenVersion=4, MQTT connectivity and
idle voice state remain at 10:37:59 UTC. Control stays disabled; no JTAG is attached and COM3
is released. The evidence records exact desktop source hashes; these are bounded observations,
not a comparison of capacity across boot histories or acceptance of the remaining hardware gates.

### First 024 CI candidate failure

Main `b961cf4` host run `37607779353` and PR test-merge `a6e2bf5` host run `37607860538`
fail while linking `voice_identity_integration`: the shared MQTT `host_runtime.cc` calls
`mqtt_host::ResetDiagnosticLogs()`, but the integration target did not link the new
`diagnostics_support.cc`. MQTT's own five CTests already pass. This is a cross-suite host
harness dependency omission, not an infrastructure failure or a firmware runtime diagnosis.
The earlier source-reuse review missed that dependency; a focused integration rerun and fresh
candidate CI are required. The flashed application, immutable assets and signing root are unchanged.

The integration target now links `${MQTT}/diagnostics_support.cc`. A cross-repository reference
check finds no other target missing the shared dependency. Fresh Debug and ASan/UBSan/leak
builds both link the test and fixture executables, pass **four cases / one CTest**, and emit the
expected fixture snapshot. Evidence is Rodak
`.codex-temp/mqtt-diagnostics-024/voice-identity-ci-fix/test-evidence.json`. This focused repair
does not turn the earlier failed runs green; the next exact main/PR candidates need fresh CI.

## 2026-10-07 MQTT credential client replacement 025

Production source `8d5cf99f95b59d45b0a1e66fc3a02d1a502618a6` replaces the full SDK client
for automatic refresh within the existing effect authority. It revokes the old generation,
confirms SDK stop/destroy, then checks exact current persisted credentials before attaching
and starting the next instance. Old-generation rejection work is coalesced; a new-generation
rejection survives. The normal recovery path no longer requires a device restart merely because
its old client was connected or had an outbox. Changed authority and unconfirmed SDK stop
retain restart isolation. Stop remains subject to SDK/media blocking limits; FreeRTOS task
storage may await idle reclamation. See the [contract](mqtt-credential-refresh.md).

The named original-development-root package is `20261007-200215`, task
`mqtt-credential-refresh-025`, version `0.1.2-dev.1`:

- Main: **7,132,272 B**, SHA-256 `938de5fadbaf3181c9b8e885e93055d53ad5485210804e9d4c641c8ba991039e`.
- ZIP: `8a46f46a664fe2da3834252e5fdaf465f6a5ee4292e4c644b931358c2c4ff971`.
- Frozen ELF: `2fdb73706ead914c7bb5907d42c4b7b3ea7be369a84be3c946b49fd794790416`.
- ESP-IDF 6.0.2 build, partition fit, allocator/Camera final-ELF gates and package verification pass.
  Five immutable files match 024; Home-test and fault-injection flags remain off.
- NVS-preserving COM3 flash passes Recovery/main/OTA-confirmation/Home. It preserves original
  device ID `c78845a8-06c9-4dcd-b7ff-d33e599f23ff`, bound state and tokenVersion=4.

Fresh local full-repository validation passes **38 unique suites / 98 CTests**, with
ASan/UBSan/leak checks, **107 Python cases** (52 standalone, 55 embedded), and **21** separately
counted CI-helper cases. MQTT has 133 positive cases including 18 replacement cases; its four
new complete-service negative variants and six existing diagnostic variants fail their intended
assertions. Real Cloud has 44 cases, and the combined real MQTT/Cloud target has six cases and
two full-Cloud negative variants. Shared voice integration passes four cases. Directed Debug
and sanitizer builds are recorded separately. Source checks confirm 28 compilation manifests,
82 raw hashes and 76 freezes; 15 recorded production paths match the commit after explicit LF
normalization. This is local software validation; current exact main/PR GitHub artifacts are
tracked by [#26](https://github.com/rymcu/rodakos/issues/26), without borrowing earlier green runs.

The valid-token window stops the same desktop server for **5.044 s**. Generation 1 reconnects
**1.454 s** after server restart without replacement. Telemetry uptime advances
4,970 → 36,935 → 66,966 ms, shadow advances 1042 → 1044, and identity/binding remain unchanged.
Serial capture spans **128.158 s** with continuous log ticks and no detected boot/reset/panic;
the final device snapshot is earlier than capture end. This is normal reconnect evidence,
not proof of an authentication-triggered replacement.

The independent authentication window keeps the existing 600-second server TTL. Initial enrollment
to rejection spans 694.810 s; this matches the expiry background, but JWT iat/exp and the per-token
server rejection reason were not exported. The directly verified event is SDK credential rejection.
Generation 1 rejects at log ticks 700581 and 703651 ms, with the second inside HTTP refresh
(702631–704821). The old request is coalesced at retirement 704851; stop/destroy completes 705661,
generation 3 attaches 705701 and connects 706721. There is one HTTP refresh, no follow-up refresh
in the window, and 94.922 s of post-CONNECT serial observation. New telemetry uptime
705073/735136/765116 ms continues from 666966 ms, with shadow 1044→1046 and original identity.
Server restart to CONNECT takes 8.099 s; retirement to CONNECT takes 1.870 s, without a hard-bound claim.

A separate 65.042 s outage triggers one HTTP attempt after three TCP failures. HTTP fails while
the server is down; generation 3 reconnects 3.721 s after it returns, with four new telemetry
events and 129.326 s of subsequent serial observation. This does not repeat the client-replacement
proof. Final MQTT internal/DMA free samples are each 4,988 B below the authentication-window
samples; no leak or full-resource-return conclusion is drawn.

The following screen window renders real 320×240 Home pixels and records 19 waiting/acknowledged
signal pairs. UI Stop closes the peer first; final JPEG/peer cleanup precedes processing the
supplementary Stop, which returns `display_stream_not_found`. Preserve this failed receipt rather
than claiming a successful Stop ACK. Home completes, the UI has no active screen and control stays
off, while MQTT/wake remain live. Candidate limits and resource gates remain open.

Evidence is Rodak `.codex-temp/mqtt-refresh-025/` and
`.codex-temp/project5-mqtt-refresh-025/hardware-analysis/`; the paired
[hardware record](https://github.com/rymcu/rodak/blob/master/docs/mqtt-credential-refresh-verification.md)
keeps each window separate. The 024 restart failures remain historical failures. SDK candidate
capacity, Camera STREAMOFF, late control, real SD/touch/audio, arbitrary OOM, production root,
OTA power-cut and eight-hour soak remain **NO_GO**.

## 2026-10-07 exact stream Stop 026

Firmware source `cc776c1d007abe7e415ce9b386dea887eefe304e` adds exact-instance Stop using
the original `startCommandNo` and `sessionId`. The latest successfully started instance can
produce `already_stopped` only after its native Stop returns in the same MQTT generation,
epoch and authority. Cached successes additionally retain the original instance nonce; an
unknown or replaced instance still fails without stopping a new peer. Legacy Stop without
the start identity retains its active-session behavior and cannot claim same-name isolation.
See the [wire contract](rodak-aiot-contract-v1.md#exact-stream-stop-026).

The original-development-root package is `20261007-231703`, task `stream-stop-026`, version
`0.1.2-dev.1`:

- Main: **7,134,160 B**, SHA-256 `d1748efc4019da8df6257999b7248563accb85a0e9b6d9ebb607ef038de66ced`.
- ZIP: **9,483,957 B**, SHA-256 `56314f048f9ba242104b209c33e0fa14153d3d206d7b775527ab460bf02773ed`.
- Frozen ELF: `024d49ec2a1cdb353ee10952b5da09ffd76fa88eae88fd30ed67e644ba36c669`.
- ESP-IDF 6.0.2 build, partition fit, signature, ZIP contents and allocator/Camera final-ELF
  gates pass independent checks. The image embeds the same frozen ELF hash. Five immutable
  files are byte-identical to 025; Home-test and fault-injection flags are off.
- The Recovery-safe COM3 refresh writes the OTA data and main slots without erasing NVS or
  replacing Recovery, bootloader, partition table or trust root. Recovery/main/OTA confirmation/
  Home checks pass. Original device ID `c78845a8-06c9-4dcd-b7ff-d33e599f23ff`, bound state and
  tokenVersion=4 remain. A production application flavor is not a production signing root.

Local MQTT tests pass **143 positive cases / 7 CTests per Debug and ASan/UBSan/leak build**,
including 70 command cases. Separately, six existing diagnostic source negatives and four
credential source negatives pass per build. The four affected shared consumers separately pass **7 CTests / 85 normal
cases and 3 embedded Python cases per build**: real MQTT/Cloud 6, voice integration 4, remote
input 30 and display ACK 45. Two Cloud, five display and one header-compile negative controls
are independently checked. Cloud/voice use the final core-v2 service; their earlier core-v1
runs are retained as intermediate evidence. Remote-input/display checks use the unchanged
StreamLease header. This targeted matrix is not another complete 38-suite run.

The initial desktop main/preload implementation is
`41c96efeb7e1ff2a5d5c438a6dd862e9be379063`. The later automatic-terminal renderer correction
is `25b8a41f07b348fd33792372ccabc1d43fd9a65b`; its frozen production inputs match the last two
device windows below. The initial local coverage result and later renderer/test-fixture changes
are separate runs. Final desktop test/build outcomes belong to the paired
[Stop verification record](https://github.com/rymcu/rodak/blob/master/docs/video-stop-confirmation.md);
the earlier full-run pass must not be represented as a pass of every later change.

Six independent captures use the same 026 package and preserve their own action, identity,
serial timing and result records:

| Window | Direct result and boundary |
| --- | --- |
| Display active Stop | No browser peer was created. Stop API waiting was 1.116 s and returned the matching `stopped`; a repeated API call returned the same desktop record, not a second device command. Serial observation continues 56.786 s after Stop response queuing. |
| UI Stop | A real 320×240 Home frame is displayed. With control enabled, Stop removes the image in 22 ms and confirmation is observed in 1.361 s; the matching result is `stopped`. Later control is off and no image remains; observation after response queuing is 76.196 s. Eight SDK candidate-limit messages remain. |
| Initial peer-terminal UI failure | The experiment closes the connected browser peer without intercepting MQTT Stop. The matching device result is `already_stopped`, but the automatic renderer terminal path does not display it; the 18 s UI wait exits with failure. The 94.082 s post-response observation and successful device audit do not turn this UI failure into a pass. |
| Camera active Stop | No browser camera peer was created. Stop API waiting is 1.257 s; native logs show a first frame, four frames in a short 311 ms preview, STREAMOFF and worker exit, with a matching `stopped`. Worker exit follows Stop request by 100 ms and observation continues 69.573 s after response queuing. This is not remote camera-image acceptance or closure of the older intermittent STREAMOFF stall. |
| Corrected peer-terminal UI | The separate renderer correction is exercised by closing the browser peer again. The page now displays the matching `already_stopped` confirmation, with control off and zero images. The capture spans 193.004 s, including 145.520 s after the Stop audit; eight candidate-limit and connection-negotiation warnings remain. The original failed window stays unchanged. |
| Server outage and new-session Stop | The server is down for 5.046 s. The old Stop becomes `unknown`, not success. Two generation-1 authentication rejections lead to one bootstrap/enrollment, coalesced old work, confirmed stop/destroy and generation 3; disconnect-to-CONNECT is 12.090 s, and server return to received CONNECT is 7.081 s. This is credential replacement, not a simple reconnect. A new session obtains `stopped`; image removal is 32 ms and UI confirmation 1.373 s. The complete capture is 657.728 s, with 509.943 s after the new Stop and 483.903 s after Home completes. |

The last window has contiguous serial byte offsets/timestamps and retains original identity,
bound/tokenVersion=4, final available telemetry uptime **1,703,356 ms** and shadow **1055**.
Its serial log and 287 relevant events from the latest-2,000-event snapshot show no identifiable
old command requeued after generation 3. This is a bounded observation, not proof that no old
packet existed anywhere; two incomplete MQTT fragment warnings are preserved. Captured windows
have no detected reset/panic marker, but the post-flash boot is new: older 025 uptimes are excluded.
Touch-poll health and wake listening do not establish physical touch or real speech acceptance.

Camera lowers the boot-cumulative `internal_min` from **5,079 B to 331 B**; that historical
minimum persists in later windows. The Camera window's last MQTT sample is
`internal_free=15215`, `internal_largest=7680`, `dma_free=14319`. These are sampled values,
not a sustained free-memory floor, leak classification or proof of complete resource return.
The target ABI adds 24 B per ledger entry (104→128 B; 1,536 B for 64 entry payloads), while
StreamLease grows 56→80 B. Two closed slots add 16 B of members and can retain two leases;
shared-pointer control blocks, strings, deque blocks and allocator overhead are additional.

026 does not change candidate capacity. Read-only inspection finds the current ESP32-S3
library default is 10, despite the header comment of 16. A future explicit capacity of 32 would
add 5,984 B per peer for candidate/pair tables, including 176 B extra internal-preferred sort
storage; pair capacity remains 64 and allocation fallback can consume internal memory.
No such expansion was implemented or deployed. MQTT signal ACKs do not prove that all
candidates entered the SDK's ICE table.

Evidence resides in Rodak `.codex-temp/stream-stop-026/`: the separate MQTT and shared-consumer
summaries, independent package verification, frozen firmware and six sealed hardware analyses.
All Stop results are software acknowledgements (`physicalVerified=false`); native joins retain
their blocking limits. Production-root migration, physical power-cut, Camera STREAMOFF,
late control, full resource recovery, real SD/touch/audio and eight-hour soak remain **NO_GO**.
The current workflow remains local tests/builds/device evidence: no new Actions dependency,
repair, rerun, billing or required-check work is introduced, and historical CI retains its own scope.

## 2026-10-08 candidate capacity and Camera failure 027

Source `3ff55ab7ec3d46cd7a1e2c41fca5d700505c0a06` uses the public 32-candidate setting
for Camera and Display, preserving the 50 ms timeout, 400 KiB caches and SDK binary. Two fixed
32 B resource observers add 64 B to final internal BSS. Their local generation/call counters and
sampled heap minima are diagnostics, not MQTT epochs, admission counts or continuous peaks.
The native-Stop sample occurs before peer-task self-deletion.

Debug and ASan/UBSan/leak each pass 3 CTests, 55 positive cases and 9 source negative controls.
The actual Camera/Display translation units use real SDK headers. ESP-IDF 6.0.2 and final
Camera/JPEG ELF gates pass. Rodak's test-only commit `8a0b3883` passes 5 focused files / 130 tests
and ESLint; the running desktop production baseline remains `d28370c8`.

| Artifact | Identity |
| --- | --- |
| Package / task | `20261008-004814` / `candidate-capacity-027` |
| Main image | 7,139,328 B; SHA-256 `543384d49558150178f46a4b72e6d269b3f8782f65f75bb5c83f8d74d858f4a8` |
| ZIP | SHA-256 `21b8cd50e017c0e5be9b36cb8bafbb94c700c5ef935938ecb03458a4779825b5` |
| ELF | SHA-256 `a3104057f18a032d31b32dbb978e31d92cef869709bc6d54cc90392324f6c4bd` |

Independent checks confirm signature, all ten ZIP entries, final build flags/partitions and the
five unchanged immutable-package files from 026. The guarded NVS-preserving refresh passes
Recovery/main/Home/local OTA confirmation, preserving MAC, device ID, bound and tokenVersion=4.

The normal Display window matches 18 native candidates to 18 SDK Add endpoint/type/order records
with no over-limit warning, receives a real 320×240 Home image and returns `stopped`. UI image
removal takes 33 ms; device confirmation is observed at 1,378 ms. The selected useful endpoint
is candidate 4, so this does not prove a path beyond candidate 10 or the 32/33 boundary.
Retain the bind/remote lookup and close errors; this is not an error-free window. The serial
capture spans 206.240 s, including 115.551 s after Stop response queuing. Sampled post-Stop
internal free/largest is about 18,163/8,192 B; complete recovery or a leak is not established.

The independent normal Camera window receives no remote image or signaling offer at the desktop. After preview,
before peer open, DMA free/largest is 1,907/1,792 B; after open it is 943/832 B. AES allocation
fails 900 device ms later, TLS write returns `-0x0084` and MQTT disconnects. Stop remains unknown.
The native first frame at 87 ms is not a remote frame. Following STREAMOFF begin, the unperturbed
serial window remains silent for 492.499 s until the capture-close request. Stale telemetry does
not prove continued progress.

Only after that capture closes, JTAG reads 23 committed diagnostic records with zero pending/drop:
ioctl-return phase 2 is 0, phase 3 before the following completion log is present, phase 4 is
absent. The original failure therefore cannot be described as ioctl still blocked; the actual
log-lock owner remains unproven. The separate debugger window subsequently enters a panic path,
despite a single SMP resume. A direct RTS attempt fails to restore output/MQTT. Official USB reset
then boots the same confirmed 027 directly into main/Home/OTA/MQTT, with no new flash or erase;
the first-flash validator's exit 5 is retained because its only missing marker is Recovery.
A fresh 17:40:33 UTC snapshot confirms original bound/token4 and uptime 455,201 ms.

Evidence lives in Rodak `.codex-temp/candidate-capacity-027/`, with separate sealed analyses for
normal Display, Camera failure, fault DRAM and USB recovery. See the
[complete cross-project record](https://github.com/rymcu/rodak/blob/master/docs/video-candidate-resource-verification.md)
and [diagnostic contract](camera-teardown-diagnostics.md).

After USB recovery, eight separate capacity-only Display sessions test both trickle and SDP:
18 unique owned endpoints produce 18 SDK Add/0 limit, 32 produce 32/0, 33 produce 32/1, and
32 plus a repeated first input also produce 32/1. The corresponding software ACK counts are
19/33/34/34 for trickle and 1/1/1/2 for SDP. Full-table checking precedes deduplication; no extra
non-owned Add occurs in these capacity windows. Retain the large-SDP fragment warnings.
A ninth window puts ten nonresponding owned sinks in one Answer, then forwards all 18 native
candidate strings unchanged. The selected useful endpoint is overall candidate 14, with its Add
after the first ten and before the first SDK Select pair; DTLS and a decoded 320×240 Home JPEG
follow. An earlier unselected browser peer-reflexive candidate and the final pair's in-progress
state are retained. The nine correlated Stops all return stopped, followed by 60.016–60.031 s
of observed serial and original bound/token4 snapshots per window. The first v2 tool's pre-Answer
SDP/ufrag comparison failure is preserved separately; v3 changes comparison only, not forwarded
strings. Independent analysis SHA-256 is
`4808d5142aa884827a8b8e3af571c264ff30c19756339f128d6bf51d359720b9`.

Camera remote frames, cooperative worker cleanup, five normal video cycles, concurrency and soak
remain open. Capacity-only sessions have no expected video and cannot satisfy normal video cycles.
Preserve 026's boot minimum 331 B; a new boot's minimum is not evidence of improved capacity.
Release remains **NO_GO**.

## 2026-10-08 cooperative DVP worker validation 028

Source `6666a3e52e50ddab989019517f237b2633e7c168` changes the pinned generated DVP
overlay to request cooperative shutdown, wait for the worker's final controller-lock release,
and delete the parked task through owner-side `vTaskDeleteWithCaps`. A callback cannot delete
its own worker. The 3,072 B worker stack is PSRAM-only, without an internal fallback; the event
queue remains 3, priority 23, and DMA ring configuration/actual payload remain 8,192/7,680 B.
No SDK binary, candidate setting or frame-format change is part of 028.

Debug and ASan/UBSan each pass 13 worker cases and six source negative controls. They cover
empty/full queues, requests around receive, admitted callback/capture-start completion, final
lock release, early task execution before handle publication, allocation failures and self-delete.
The old 027 worker's direct-delete boundary fails while a real host stdout mutex is still held;
the host does not asynchronously kill a C++ thread or establish the device's actual lock owner.
The worker, existing teardown overlay, Camera capture and teardown-diagnostics targets pass eight separate
Debug/ASan configurations. The local unified runner now registers the worker target; its syntax
is checked, but the whole runner was not rerun. ESP-IDF 6.0.2 builds the frozen production sources.

| Artifact | Identity |
| --- | --- |
| Package / task | `20261008-021356` / `camera-worker-028` |
| Version / flavor | `0.1.2-dev.1` / normal production app flavor, original development signing root |
| Main image | 7,139,552 B; SHA-256 `b88259da929d7c353f6de91a6756ed6ae0ee500d2af17000eb2aba1443094e11` |
| ZIP | SHA-256 `b481dca82508ddff5d2d560796264d6092b4e3323b37200a2ea3290720c821cf` |
| ELF | SHA-256 `a68ebb351a41246c7efa9f4e4d9babfb3ea052ded1a7e6717eabfa30196d3ce2` |

Independent package validation executes the signature, JPEG allocator and Camera final-ELF
checks. Commit blobs, generated translation units, build flags, partitions, binary-embedded ELF
hash, ZIP contents and five immutable files match the frozen evidence and 027 baseline. This
verifies payload/source identity; it is not production-root deployment or hardware acceptance.
The validation report SHA-256 is
`eb24fed091c43208ceb72bce8a6d87b99a467ca2767d9f7ab2a09be418f62b62`.

Final-ELF memory comparison is more than a BSS-size check: IRAM text grows 72 B and end padding
184 B, making a 256 B aligned change. DRAM data/BSS sizes are unchanged but their addresses and
the internal heap start move by 256 B; heap end and aligned native TLS delta remain unchanged.
IRAM/DRAM can alias the same SRAM, so these views must not be added as independent occupancy.
Target DWARF shows controller 144→148 B, flags at offsets 96/97 and the internal TCB still
100 B. Moving a 3,072 B stack allocation to PSRAM is not measured net internal/DMA recovery.

The guarded NVS-preserving flash exits successfully and verifies Recovery → main, local OTA
confirmation and Home. A separate 70-second cold-baseline capture is closed. The fresh device
snapshot retains ID `c78845a8-06c9-4dcd-b7ff-d33e599f23ff`, MAC `44:1b:f6:c3:b4:30`, bound,
tokenVersion=4 and MQTT connected. This is a new 028 boot; earlier 027 uptimes/minima are not
joined to it. No erase, re-binding or new trust root is implied.

The first normal UI Camera session receives a decoded 320×240 remote image. Visual inspection
finds a dark scene; reception/decoding does not establish camera image quality. Stop removes
the image in 55 ms and its matching confirmation is observed in 1,520 ms. Serial shows the full
close path and 108.656 seconds after Stop, through 2026-10-07 18:37:39 UTC; that capture is
closed. Its independent analysis matches 18 native candidates to 18 SDK Add records, selects
candidate 4 and retains 30 succeeded-pair stats samples. The playing audit contains no command
record; the stopped audit contains one, so no additional per-signal ACK audit is inferred.
Retain 27 timestamped warning/error lines; the absence of raw `E:RX` in this window is not a
global no-overflow claim. Independent analysis SHA-256 is
`5f03676494665fb2b14d6e580c344e3bb260eb169b420603b32fdf2b512a5358`.
Its success does not retroactively diagnose the 027 lock owner or close all STREAMOFF failures.

The remaining closed capture combines Camera02–05 and Display01–05 with the independently
sealed Camera01, establishing **five Camera/five Display finite normal UI Start/image/Stop
cycles** on the same boot. All use the normal product UI, without diagnostic SDP changes; each
window matches 18 SDK Add records to its native candidates. Start and later playing snapshots
decode 320×240 images, but identical dark/static bytes do not prove motion quality or uninterrupted
frame delivery. Only Display01 has a separate visual Home review. The 027 capacity-only sessions
are not counted as normal video cycles, and these sequential cycles do not prove every
Camera-first/Display-first startup order or simultaneous dual-stream operation.

Ten correlated Stop results are **nine `stopped` and one `already_stopped` (Display03)**.
Display03's UI Stop is at 18:53:01.614 UTC; native close completes at device 1,238,153 ms,
the stopped state at 1,238,193 ms and original Stop ACK at 1,238,203 ms. The typed response at
18:53:03.049 UTC is the exact-instance idempotent result, not a tenth `stopped` or an unknown
session success. The selected audit scans 2,000 events without truncating the ten-Stop scope.
Each repeated window retains 60.573–60.612 seconds of post-Stop observation; Camera01 has its
separate 108.656-second post-Stop capture.

The repeated serial capture is closed with exit 0: 250,360 bytes across 30,892 timing chunks,
contiguous byte coverage and nondecreasing host/device timestamps. No panic, reset or MQTT
disconnect is found within those captured records. Preserve **214 timestamped warning/error
lines** and three raw `E:RX` records: Camera02 `153600-76800`, Camera04 `153600-53760`, and
Camera05 `153600-126720`. These successful finite cycles are not warning-free or camera-quality
acceptance and do not erase older failures.

Camera01 records a boot-cumulative internal minimum of **563 B**. The later same-boot value
**555 B** is first reported during Camera02 quiet at device 545,213 ms / 18:41:28.768 UTC;
this is the report time, not proof of when or why the historical low occurred. The final Main
sample is internal free/largest **15,491/7,680 B**, DMA free/largest **14,387/7,680 B**. The
separate final MQTT sample is internal free **15,475 B**, DMA free **14,371 B**, largest blocks
**7,680 B**, and PSRAM free/largest **2,493,572/1,900,544 B**. Do not combine these distinct
sampling times into one measurement or classify their differences as a leak or net recovery.

The final device snapshot reports uptime **1,595,076 ms**, shadow version **1066**, original
bound/tokenVersion=4 and MQTT connected. After serial closure, the UI has zero images and
control disabled; that later UI check does not extend the captured hardware time window. No
full NVS byte-equivalence, sufficient memory floor or complete task/DMA/IRQ reclamation is
claimed. Cache-off/NVS/OTA and media/audio/TLS coexistence, arbitrary memory pressure, real
touch/acoustics, camera quality and eight-hour soak remain open. Resource and release decisions
remain **NO_GO**.

Local evidence is under Rodak `.codex-temp/camera-worker-028/`: `software-verification.json`,
`runner-supplement.json`, `target-abi.json`, `package-independent-verification.json` and the
independent source review. Flash/cold-baseline/Camera01 and the repeated capture are separate
closed windows. The repeated analysis rechecks 115 input hashes and reuses the prior Camera01
review without counting another run. Its SHA-256 is
`65c3cebe56a786f06613f3d1eaa5d20c2c294be65464e9e26bc7698adce6da03`; seal SHA-256 is
`27b64e4943ce2d5a71d256c72532e2b21df8a34f8e88c0891c4424b59b5ab556`.

## 2026-10-08 AES DMA allocation cleanup 029

Firmware source `83cab8021c265ed162d9bbf827b59299dbdfc546` fixes one confirmed
ESP-IDF 6.0.2 allocation-failure path. In `esp_aes_process_dma_ext_ram`, a successful input
bounce allocation followed by an unsuccessful output bounce allocation previously returned
before freeing the input. The pinned overlay changes only that return to `ret = -1` followed
by the existing cleanup. Original full-output zeroization, error logging, allocation caps,
1,600-byte chunk limit and successful processing remain unchanged. The real CBC caller still
releases its AES hardware lock. Neither the IDF installation nor managed sources are edited.
Later documentation commits do not change this firmware/package source identity.

This corrects a demonstrated ownership defect; it does not establish that the 027 Camera
failure entered this branch. It also does not reduce normal simultaneous bounce-buffer
requirements, prove sufficient internal/DMA margin or fix the separately observed Home
enqueue failure below. See [the dependency correction](dependency-maintenance.md).

Debug, Release and ASan/UBSan/leak each pass six focused CTests: four C groups with twelve
invocations, complete-upstream-source failure/normal-parity controls, and six Python generator
tests. The host compiles complete AES core/caller/common translation units and the real public
context/API header. Tests enter the real CBC failure path, verify output zeroization, buffer
release and lock/clock balance, then verify successful retry. Seven normal-path cases have
identical output checks and allocation traces with and without the patch. SDK/RTOS/HAL are
host dependencies; the predictable HAL byte transform is not AES known-answer or physical DMA
validation. Only the intentionally leaking old-source negative subprocess disables leak
detection; all positive and normal controls retain it. The negative requires exit 1 and the
specific outstanding-allocation assertion, never a crash or sanitizer error.

The generator rejects drift in nine pinned inputs and seven CMake target/source/chip variants.
The local runner registers the target and passes its syntax check; no full-run or Actions result
is inferred. Related Camera teardown checks pass 25 C scenarios in each Debug/ASan mode. Their
first generator run rejects a dependency graph before reaching the intended duplicate-source
negative check. Both later serial reruns pass all eleven Python tests. The first logs remain;
the bytes read at failure were not saved, so the cause is unestablished and is not assigned to
parallel execution. The initial harness compile failure and first genuine old-source red are
also retained separately. Software verification SHA-256 is
`5f8941265ce1376aac18475bd7405042ecad1a2903afa68801385d13f0d85f4b`.

| Artifact | Identity |
| --- | --- |
| Firmware source | `83cab8021c265ed162d9bbf827b59299dbdfc546` |
| Package / task | `20261008-040342` / `aes-dma-cleanup-029` |
| Version / flavor | `0.1.2-dev.1` / normal production app flavor, original development signing root |
| Main image | 7,139,584 B; SHA-256 `2795b7a660fa8c81c81cc4b46fb9e523c01002a3faa7fae441bafbf76fdd0d10` |
| ZIP | SHA-256 `017fa4b0c20abc7c5ffa37d98afa7a04b343e0c8122532545eadbacb51774b1b` |
| ELF | SHA-256 `ec71c9e09b5a385eda4048d352b5809711365036cfb9a934211d15b4b5da3dc5` |

The committed IDF build passes. Freeze verification matches all fourteen changed source files
to commit blobs after LF normalization, rehashes all 63 software-artifact paths, and links the
unique generated AES translation unit through `tfpsacrypto` to the retained map/ELF symbol at
`0x42277b64`. Independent signature/ZIP/merged-image checks pass; bootloader, partition table,
OTA selector, Recovery and public key are byte-identical to 028 package `20261008-021356`.
The normal app fits its `ota_0` partition. Final Camera/JPEG ELF checkers pass again; the
536-byte diagnostic object is derived from this ELF at `0x3fca85e8`. The existing Recovery-name
partition warning and the freeze script's first CRLF-parsing failure remain recorded; neither
is rewritten as an application failure or hidden. Independent package-verification SHA-256 is
`022dbc87b7bfa889f2d88868e2c1067d0a711040c70ad3b5ec4ad591b8594cd4`.

The separately labelled **029 resource-concurrency experiment ran 028 source `6666a3e` and
package `20261008-021356`**, before the AES package was installed. It combines a local native
Camera preview with one remote Display peer; it is not two simultaneous remote video peers.
Startup/exit order has four cells:

| Startup / cleanup order | Bounded result |
| --- | --- |
| Local Camera first / Home first | Finite pass; 78.464 s after successful cleanup |
| Display first / Display stop first | Finite pass; 79.377 s after successful cleanup |
| Local Camera first / Display stop first | Finite pass; 60.193 s after successful cleanup |
| Display first / Home first | `RODAK_APP_LAUNCH_RESULT {"queued":false}`; Camera remains running |

The failed Home attempt leaves a saved Display image showing Camera/Ready. After Display
stops, one separate Home retry succeeds and the native Camera close path completes. Preserve
that recovery without counting it as a passed fourth cell. Existing logs do not distinguish
the LVGL-lock deadline from `lv_async_call` failure, and do not establish OOM, AES failure or a
specific lock owner. Two desktop exclusion windows reject the opposite remote-stream start
while preserving the original session and decodable images; they do not exercise a firmware
busy ACK or actual simultaneous remote peers. Across these windows, six correlated Stops are
three `already_stopped` and three `stopped`, retained as distinct results.

The closed captures preserve warnings/raw diagnostics and show no captured panic/reset/MQTT
disconnect. The same-boot internal minimum remains 555 B. Switching-recovery final Main
internal free/largest is 15,827/7,680 B and DMA 15,107/7,680 B; the separate exclusion window's
final Main samples are 16,731/7,680 B and 15,715/7,680 B. Different sampling times and workloads
do not form a leak test or demonstrate net recovery. Final device state retains bound/token4,
MQTT online and uptime 5,675,116 ms. These are 028 observations, not 029 AES hardware results.
The independent startup-order analysis rehashes 78 inputs; its SHA-256 is
`2732f6417698fd9774a0c26c9a9ed9d1c70c5eb6d32fa71032bf8636c2571654`.
See [the full cross-project startup-order record](https://github.com/rymcu/rodak/blob/master/docs/video-startup-order-verification.md).

The guarded NVS-preserving flash writes the 7,139,584-byte main at `0x2a0000`, verifies its
hash and passes Recovery/main/local OTA/Home boot gates. The closed cold-baseline capture
contains 6,275 bytes spanning 70.039 seconds. Preserve its two warnings and trailing partial
one-byte `I`; no missing suffix is invented. The original device ID, bound/tokenVersion=4 and
MQTT online remain. This is a new 029 boot, separate from all preceding 028 observations.

The separate normal-video capture is also closed: 56,677 bytes spanning 287.543 seconds, with
contiguous received-byte coverage and nondecreasing host/device timestamps. One Camera and one
Display smoke each show decoded 320×240 images at start and while playing. Both actual typed
Stop results are `stopped`; the independent review correlates `sessionId`, `startCommandNo`,
`commandNo` and `streamKind` to the unique original-connection serial ACKs. The saved two-Stop
audit is complete for its requested range and both retain `physicalVerified=false`.

| Stream | UI image removal | UI confirmation | Captured serial after Stop ACK | Snapshot after Stop ACK |
| --- | ---: | ---: | ---: | ---: |
| Camera | 71 ms | 1,340 ms | 61.272 s | 61.640 s |
| Display | 25 ms | 1,354 ms | 60.163 s | 61.954 s |

Post-Stop intervals use the serial receipt time of the queued original Stop ACK; UI latency
and the later device snapshot have separate timing boundaries. Root and the independent
reviewer inspect a nearly dark Camera image and a Home Display image. These establish observed
decodability/content only, not motion continuity or camera image quality. Each stream has
eighteen SDK Add records with no over-limit warning. This smoke has no complete browser
candidate observer, so it does not claim one-to-one browser/SDK candidate correspondence.

Retain **44 timestamped warning/error lines**: 27 in Camera and 17 in Display. No raw `E:RX`,
panic, reset or MQTT-disconnect marker is captured in this normal-video window; this is not
an error-free or general no-overflow claim. The boot-cumulative internal minimum is **547 B**.
The final Main sample is internal free/largest **16,515/7,680 B**, DMA free/largest
**14,419/7,680 B**. Do not compare this boot's minimum with 028's 555 B, infer a leak from
different samples, or attribute a headroom improvement/regression to the cleanup patch.

The final 2026-10-07 20:22:18 UTC snapshot has uptime **365,136 ms**, shadow version **1070**,
original bound/tokenVersion=4 and MQTT online. At 20:23:03 UTC, the final UI has zero images
and control disabled; that later UI observation does not extend the captured device window.
All serial captures and observers are closed. The independent hardware review rehashes
37 inputs; SHA-256 is
`62bcf140725f17f1d7b8cfe1648c1d53fc9ef0bb5129f6efb4310f5e96621df4`.

Normal smoke does not prove the AES allocation-failure branch was triggered on hardware;
that branch is verified by the identified host fault injection and linked code. It also does
not replace the earlier 028 fourth-cell Home failure or its separate single-retry recovery.
Resource/concurrency recovery, arbitrary OOM, DMA/IRQ/cache-off behavior,
camera quality, audio/TLS coexistence, production signing, power cuts and eight-hour soak remain
open. Release remains **NO_GO**.

Local evidence lives under Rodak `.codex-temp/aes-dma-cleanup-029/`, including
`software-verification.json`, `package-evidence.json`, `package-independent-verification.json`,
`frozen-029/` and `independent-hardware-review/analysis.json`. Package evidence SHA-256 is
`b90cdf2ab9ec42b600335bf134fc6f2dc2fe8f0c0c89776a02b97bda5f940001`; package seal SHA-256 is
`ad73f01dcf868d7a4cb7ea32600d9b13c964c37e89448d59d3f4dd744cd08e19`.
The separate 028 startup-order windows remain under `.codex-temp/resource-concurrency-029/`.

## 2026-10-08 video task retirement and navigation 030

Source `34c9e6453344b8eb7896ab5476ef222504c12a94` is installed as package
`20261008-055334`, task `task-retirement-030`, version `0.1.2-dev.1`. Five video workers use
generation-owned external WithCaps retirement; serial/Camera Home use the precreated navigation
queue. The [implementation contract](task-retirement.md) records the focused software checks.
This section adds the subsequent package and device evidence; the preceding 028/029 sections
remain unchanged history. Detailed images, timings and limitations are in the
[cross-repository 030 record](https://github.com/rymcu/rodak/blob/master/docs/video-task-retirement-verification.md).

The development-signed main image is **7,147,584 B**, SHA-256
`ee0223caef8588250b17e6f08b0f8765b732ca8618621071bbd37c2c54f31637`;
ELF SHA-256 is `6ac7a3ed563614d7e335a9b5ca3917fe287cfa2324ec2fff8e7d7f8d8c816022`.
The independent audit checks the clean candidate commit, compilation/object/map identities,
image-embedded ELF hash, direct public-key and official verification, ZIP entries and partition
bounds. The five immutable files and merged bytes outside the app partition match 029;
authority v3 and the existing AES/Camera generated patches are retained. This is not a
reproducible-build proof or a production-trust-root deployment.

Local evidence paths below are relative to Rodak `.codex-temp/task-retirement-030/`:

| Independent record | SHA-256 |
| --- | --- |
| `software-review/seal-34c9e645-20261008/software-verification.json` | `4a229beba08f8c708ebe3805c9461cd9ed4e2b1553cc679943fcd14db3cace51` |
| `package-review/candidate-20261008-055334/review.json` | `30f4e387646ac10a54fe7d92faaa4e61047b91aa98a4378474955d0687f1fe4a` |
| `hardware-review/final-1/analysis.json` | `e09ce2650bfcb460e3c26367bcba5f2bd930a4c88a8399a6ba6c6deb91fd2227` |

Guarded COM3 refresh verifies the installed bootloader, partition table and Recovery, then writes
only otadata and app. Both write hashes and the Recovery → main → OTA confirmation → Home
handoff pass. Device `44:1b:f6:c3:b4:30` retains its original binding and tokenVersion=4.
The cold helper exits successfully at its **70 s monotonic deadline**; its actual received-byte
span is **68.831 s**, not 70 s of continuous serial data. Retain its two invalid MQTT fragment warnings.

Normal Camera/Display observations and five matrix/button Display sessions yield **7 correlated
`stopped` receipts**, all still `physicalVerified=false`. Each Stop has at least 60 seconds of
actual raw serial coverage. Normal Camera captured 656 frames, but saved JPEGs remain nearly
dark and do not pass image-quality acceptance. Normal Display shows Home; that window has no
serial Home request and does not substitute for the matrix.

| Actual order | Startup / shutdown | Stop ACK to last serial byte before quiet snapshot |
| --- | --- | --- |
| Normal Camera / Display | Separate normal streams | 84.689 / 102.564 s |
| m2 | Display first / Home first | 90.893 s |
| m1 | Camera first / Home first | 148.471 s |
| m3 | Camera first / Display Stop first | 89.682 s |
| m4 | Display first / Display Stop first | 89.824 s |
| b1 | Display, local Camera, remote-pointer Home, Stop | 131.762 s |

The four cells run **m2 → m1 → m3 → m4**, followed by b1. Their eight serial Camera/Home
requests plus b1's ninth Camera request each have `queued:true` and `complete ok:true`.
m2's early `home-requested` image still shows Camera, even though capture began 33 ms after
serial completion; retain it and use only the later `home-confirmed` image as displayed-Home
evidence. m3 and m4 Home are observed in the subsequent m4 and b1 first frames respectively,
before the next Camera request and without another Home command.

b1 uses normal renderer pointer handlers at device `(292,20)`: seq 6 enable → 7 down → 8 up,
`Return home requested`, a Home image, and seq 9 disable are recorded. It adds no serial Home
command. This checks the remote Camera-page button path, **not physical GT911 touch**; Stop
returns control to disabled. Local Camera plus remote Display does not prove two simultaneous
remote video streams.

Normal/matrix capture lifecycles are **301.453 / 1059.968 s**; their received-byte spans are
298.606 / 1059.245 s. Preserve **44 normal + 89 matrix + 2 cold** warning/error lines, including
ICE/DTLS messages. No fatal/reset/MQTT-disconnect marker appears in these closed windows.
The same boot's internal minimum is **359 B**; final Main internal free/largest is
**16,319/8,192 B**, DMA **16,039/8,192 B**. These do not establish sufficient headroom, no leaks
or an improvement/regression against 029's minimum from another boot.

The final `2026-10-07T22:30:21.989Z` snapshot retains bound/token4, MQTT online and voice
disconnected, with latest uptime **1,504,523 ms** and shadow revision **1074**. The final UI has
zero images and control disabled; all serial captures are closed. The hardware audit rehashes
106 inputs, preserving the normal intermediate report, early image and raw timings.

Three voice WithCaps self-delete paths remain. No complete physical task/stack/resource census,
arbitrary OOM recovery, DMA/IRQ/cache-off, audio/SD/TLS concurrency, Camera quality, physical
GT911 touch, eight-hour soak or production-root gate is closed. Focused software checks and
these finite device observations leave resource and production release **NO_GO**; they neither
diagnose the original 028 Home failure nor rewrite any 027/028/029 result.

## 2026-10-08 Voice task retirement 031

Source `7ad01b7452102fd1a6a2f64d4102ae42031509b2` moves Assistant I/O, frontend Capture and
wake supervisor to the existing external WithCaps retirement mechanism. Capture returns through
its function-local AFE vector destructor; Stop/Deinit retain the captured task or operation
generation. Ordinary Deinit remains restartable. See the [031 contract](voice-task-retirement.md).

Local production-TU checks pass: Assistant 24, Capture 13 and Wake 35 cases in both Debug
and ASan/UBSan/leak; identity integration passes four cases in both modes. Three complete
legacy source/header pairs trigger the expected real IDF cleanup-creation rejection and SIGABRT.
Six Assistant mutations and the skipped-vector-destructor mutation are independently detected.
The shared fixture passes 14 Debug and 13 sanitizer CTests; compatibility reruns pass Camera
7, Display 1, peer/ACK 1 and default MQTT 5 CTests. These counts have different suite units
and are not a single whole-repository test total.

ESP-IDF 6.0.2 local build succeeds: main **7,148,928 B**, SHA-256
`cd3942e01f201569534a0deab7ed0b86680ce1094894986fa0903dd832fd5c07`; ELF SHA-256
`92eefcb8402b793ffa19a421733926f4ae877b3bacb4ec99ca0af3ad92b2c203`.
The eight production source/header inputs match their build-time hashes. This image has not
been packaged or flashed; the installed package remains 030 `20261008-055334`.

Evidence is under `D:/workspace/rodak/.codex-temp/voice-retirement-031/`, including
`assistant/sealed-final/software-verification.json`, `frontend-evidence/review.json`,
`wake/verification.json`, and `idf-build-result.json`. No Actions query or repair was performed.

Actual voice-task Deinit, complete task/stack/hardware resource return, heap headroom,
audio/TLS concurrency, acoustic quality, arbitrary OOM, physical power cuts, production root
and eight-hour soak remain open. Resource and production release remain **NO_GO**.

## 2026-10-08 Voice lifecycle diagnostic and restoration 032

Source [`514ebb8c47b0409e4bc1ac6dff95e57af6cc615e`](https://github.com/rymcu/rodakos/commit/514ebb8c47b0409e4bc1ac6dff95e57af6cc615e)
adds a test-only USB lifecycle diagnostic while retaining all three 031 voice services and the
shared retirement implementation byte-for-byte. The command is compiled only with
`RODAKOS_RELEASE_TESTS=ON`; a fixed single slot admits one canonical ID and the permanent
internal-stack main task performs Wake Deinit, task-name observations and one Wake Start.
The [contract](voice-task-retirement.md#032-诊断与普通固件的边界) and
[host target](../tests/voice_lifecycle_diagnostic/README.md) define accepted versus complete,
three-task Listening versus idle results, restoration checks and sampled busy boundaries.

Fourteen real diagnostic-TU cases pass in Debug and ASan/UBSan/leak. Separate wire checks
exercise the real JSON printer and require an empty macro-off object symbol table. The OFF
guard object alone disables sanitizer compiler-registration stubs; the diagnostic TU remains
instrumented. This is not a full target-hardware scheduling or acoustic test.

Both packages passed local ESP-IDF 6.0.2 builds and independent source/object/map/ELF,
signature, ZIP, partition and trust checks. Version is `0.1.2-dev.1`; both use the original
development signing root, with `developmentPackage=true`.

| Identity | Diagnostic test | Ordinary OFF |
| --- | --- | --- |
| Package | `20261008-080207` | `20261008-081054` |
| taskNo | `voice-lifecycle-032-test` | `voice-lifecycle-032-production` |
| Build flag / flavor | `RODAKOS_RELEASE_TESTS=ON` / `release-fault-test` | `RODAKOS_RELEASE_TESTS=OFF` / `production` |
| Main filename / image type | `rodakos_release_test.bin` / `hardware-test` | `rodakos.bin` / `app` |
| Main size | 7,155,280 B | 7,148,928 B |
| Main SHA-256 | `fea8d8d2bc9519eaad702041b9dbde79529c7656cfc52edffcda06a43ec3ed5b` | `a7efbc12243ca0897b152b0d6b084061813af064b663f711688b0b1129fd7638` |
| ELF SHA-256 | `1fb107c329e74f8a17ede2603bef75446f32c000f4cc37ff0b393818bcca39d6` | `a187c6d8379b382a2a81077ae0a5bc2376a8a433a1f2d0ba587028f5ad225fe9` |

The ordinary compile database/project description excludes the diagnostic TU, its link map
contains no diagnostic object, and main/serial compile commands have no release-test define.
Main/serial objects and final bin/ELF contain no diagnostic hooks, `voice_cycle`,
`RODAK_VOICE_CYCLE` or fault marker. The test and ordinary outputs are frozen separately;
decoding test hardware evidence must use the test ELF, even after rebuilding ordinary firmware.

For both packages, bootloader, partition table, initial otadata, Recovery and public key match
the installed 030 baseline byte-for-byte, as do merged bytes outside the Main partition.
Authority v3, AES/Camera generated patches and the original development root are retained.
The shared retirement pool remains a 448 B PSRAM request and 21 B internal global symbols;
the three voice instances add 16/16/32 B against 030. The test controller adds 44 B plus an
8 B static guard, excluding other pointer/mutex/allocation metadata. None of these static
measurements establishes net free-heap gain or permits comparing minima across boots.

Local independent records are under Rodak `.codex-temp/voice-lifecycle-032/`:

| Record | SHA-256 |
| --- | --- |
| `verification.json` | `6ce665a0335f283eb94c03d7e8b6f0513657debfa78b37cb01c87b571555b7b9` |
| `package-review/test-20261008-080207/review.json` | `c75abb3b1434939e7b7f69a7a03dca4770bd6dc2c2d0181eeb5852aef1a935ad` |
| `package-review/production-20261008-081054/review.json` | `4695d94adf0cb0e0e9fedb894ea6ef977b47b17459e5296c2e6407ab8a0e3197` |
| `hardware-review/hardware-review.json` | `02f18c3ed68dedd66f915a0ea831bad774f710dcb51358dff01384280a1532ab` |
| `hardware-review/matrix-review.json` | `f0e5d0d5bf1c3c9348ec398d63f9dec71c89d8baaadd407ff5f5d39529ef8f8b` |
| `hardware-review/production-review.json` | `93024ad5a833ed6368d587c55cf6a78ecf2f57c5b1f0c8f7d35251a908256998` |

### Closed test matrix

The test package was installed through guarded COM3 refresh. The `matrix-a` collector has
81,419 raw bytes with contiguous RX offsets, complete-line/last-byte timing, 14 transmitted
commands and 30 matched replies. All four requests have ordered accepted/before/after/recovered/
complete records. Listening sessions each reload 256 synthetic samples as one 512 B zero-PCM
block, establish a distinct WSS session with ready/input.start and host capturing, then call
the cycle directly without first stopping the session.

| ID / target | Before → after → recovered tasks | Internal free before → after → recovered | Complete to last actual RX before next TX | Fresh post-cycle telemetry |
| --- | --- | --- | --- | --- |
| 3201 idle | 011 → 000 → 011 | 20,379 → 30,403 → 18,175 B | 73.625 s | 2 |
| 3202 Listening | 111 → 000 → 011 | 6,339 → 30,679 → 20,111 B | 92.265 s | 3 |
| 3203 Listening | 111 → 000 → 011 | 6,255 → 30,779 → 20,191 B | 199.890 s | 7 |
| 3204 Listening | 111 → 000 → 011 | 5,803 → 30,863 → 20,227 B | 85.828 s | 3 |

Task-bit order is assistant/capture/supervisor. Each three-task before is enabled, Listening
and not stopping; all after snapshots have all three task names absent, and all recovered snapshots
retain enabled/listening with assistant absent and capture/supervisor present. 3201 reports
`idle_cycle_pass`; 3202–3204 report `three_task_pass`. Binding/token4 and MQTT remain present
in the supplied snapshots, and post-cycle session lists are empty.

Durations use actual RX monotonic timestamps, not collector lifetime. The old first-two values
74.828/92.594 s included next-command ACK bytes sharing the coarse Windows monotonic tick.
Finer UTC timestamps and raw offsets exclude those post-TX bytes, yielding the corrected table;
the original script/results remain preserved. Collector lifetime is 726.844 s and actual total
RX span is 725.563 s. One 10 ms cross-task firmware-log prefix reversal is retained; phase and
telemetry uptime checks remain separate from log ordering.

Preserve **2 MQTT fragment warnings, 4 MultiNet `commands not initialized` errors, 3 AFE
ringbuffer-empty warnings and 2 AFE fetch-rejected warnings**. All four MultiNet errors occur
during the before/after cleanup interval. Source review locates the error at empty command-
registry clearing; it does not establish who first cleared the registry inside the prebuilt
model destructor or prove a double-free. AFE rejection marks PCM discontinuity and is not
acoustic/AEC success. The test boot's diagnostic internal minimum is **4,087 B**; notification
stack minima are **2,204 / 2,204 / 508 B**. These small margins remain open resource concerns.
No fatal/reset/watchdog or RX-read error appears in the closed capture; filtered host event
lists do not prove the absence of every host warning. Capture ends explicitly with the port
closed, no outstanding request, no retries and no implicit cleanup commands.

### Ordinary OFF restoration and separate session window

The ordinary package was subsequently restored with guarded flashing. The recorded writes
are 8,192 B otadata at `0xf000` and 7,148,928 B Main at `0x2a0000`, both verified. The
Recovery → Main (`a187c6d83…`) → local OTA confirmation → Home handoff completes. Recovery's
own software reset/version/ELF belongs to the flash handoff, not a reset during the later window.
The boot log retains nine SD/NVS/WiFi warnings separately from session diagnostics.

The closed `production-smoke` window has 32,139 raw bytes and five commands/replies, with
151.125 s of actual cold RX before the first audio command. Synthetic-silence session
`3a527ac9-e247-43ee-98b7-4a34778d34cf` has ready/input.start and host capturing. After the
Stop reply, actual `Interaction stopped` and wake rearm are recorded. Actual RX coverage to
before the next audio_clear is **73.203 s from stopped** and **72.656 s from rearm**, with
three fresh telemetry samples after stopping. One AFE empty and one fetch-rejected warning
remain; no fatal/reset/watchdog or RX-read error appears in this closed window.

The ordinary boot's internal minimum is **4,391 B**. Its later wake-health sample records
internal free/largest **21,127/6,400 B** and supervisor-stack minimum **1,796 B**; these are
samples, not a final complete allocation census. They are not compared as improvements over
the test boot's 4,087 B or historical 030's 359 B. Collector lifetime is 392.250 s and actual
total RX span is 391.625 s; the port is explicitly closed with no outstanding request.

The final `2026-10-08T00:27:49.598Z` device snapshot remains bound/tokenVersion 4, MQTT online
and voice disconnected. The `00:27:50.150Z` UI snapshot has serial disconnected, zero camera
images and zero Stop buttons; the preview is not playing. An unrelated image element is not
counted as an active video stream. Both capture lifecycles and final process evidence are
retained in the hardware review.

Test-flavor task disappearance and restored wake listening remain evidence for that precise
test image. Ordinary OFF boot/smoke does not execute the
absent diagnostic, and source identity is not a second direct hardware trial. Retain synthetic
silence versus physical microphone/recognition distinctions. Complete physical resource
recovery, heap headroom, arbitrary OOM, DMA/IRQ/cache-off, media/audio/TLS concurrency,
acoustic quality, physical power cuts, production root and eight-hour soak remain open.
Resource and production release remain **NO_GO**; 030's 359 B and 031's unflashed build retain
their original identities and are not 032 measurements.

## 2026-10-08 Voice health and credential refresh 033

Source `78917fae1b9acb02010cef026facefc96a008c5c` retains the 031 task retirement mechanism.
The audited ESP-SR 2.2.2 `mn5q8_cn` model owns command-table creation/destruction; the caller
no longer duplicates alloc/free and rejects unreviewed model identities. AFE fetch returns
are classified under the lifecycle mutex: current failures retain warning/continuity handling,
cancelled results are counted separately, and cancellation must keep draining until the
active feed lease ends. The existing 100ms argument and external deletion order remain.

Credential refresh now uses sequential noinline stages and shorter NVS-key temporary lifetimes,
retaining TLS, generation, pairing, transactional rollback and cooperative timeout checks.
The 6,144-byte internal wake_notify stack is unchanged; no extra task or PSRAM-stack HTTP/NVS
migration was introduced. The merged full frontend passes 24 Debug and 24 ASan/UBSan/leak cases;
Debug CTest is 4/4 with two retirement negatives, two MultiNet old-TU probes and three AFE
old/mutated-TU probes. Server-trust passes 47 cases in both modes; MQTT full-TU passes six in
both modes, including header isolation and exact credential fence negatives (3/3 CTest each).

### Software and separately reviewed packages

| Identity | TEST | Ordinary OFF |
| --- | --- | --- |
| Package | `20261008-091207` | `20261008-092316` |
| taskNo / flavor | `voice-health-033-test` / `release-fault-test` | `voice-health-033-production` / `production` |
| Main bytes | 7,151,680 | 7,144,816 |
| Main SHA-256 | `e8d3b3733106f23e049f0c33a9be5a1ac57412dfad7aea7e435d055147850806` | `b3522e088ada9badb46d4d83ebc36e8dc12edd0c605b768ba8694f0cfa3e6296` |
| ELF SHA-256 | `12e5623be080f3355c8dc91420c671810051435d27733da8fb69a1e4c15ef7bf` | `39336d2c08955a90bf17ef0fa0a86c2873de9b4fb4b9108bf8faf4a931c25243` |
| Package review SHA-256 | `bf922d9b8467eaad382242608280f80a0b5112c24dfd58ddd900e7aaae68adb9` | `6de703f7bb779b1d3d6215668875b6c7e2b7943900130c46e61745e6de20d32c` |

Both retain the development signing root, authority v3 and immutable non-Main regions. OFF
symbols/objects exclude the lifecycle diagnostic and release-fault entry. TEST r1 was rejected for a report-field mismatch. TEST r2 was rejected after Windows ar
stdout conversion changed the extracted bytes; the actual model member had not drifted.
Read-only archive verification then passed in r3. Both failed review reports remain; these
tooling failures are not corrupted-package findings or accepted checks.

Both final ELF reviews confirm entry frames of 304 B for RefreshAiot, 304/336/608/464 B for
Exchange/Pair/Parse/Persist, and 112/160/272 B for the three NVS helpers. Application subchains
are bound HTTP 1,472→912 B, pairing HTTP 1,472→1,248 B, MQTT NVS 1,904→1,040 B and old-snapshot
Load 1,664→1,264 B. These exclude outer callers, SDK frames, register spills and interrupts;
they do not establish complete worst-case stack or a hardware watermark gain over 032's 508 B.

Evidence root: Rodak `.codex-temp/voice-health-033/`.

| Report | SHA-256 |
| --- | --- |
| Software seal | `1d12b2678d4a81fa82b69a58edc49250d7ae37de7414b9af1a5904a4fa232d4c` |
| Merged frontend | `4c5244a4dbf2a7c97cf4579c5be39f13cba076ad5e003462981d0704c1063bb0` |
| MultiNet review | `fb574e9ab89e9fb42df7fb4294681eb2aaa5d3108b4e17ef103a770098e2e14d` |
| Cloud host validation | `ca76d4cf9577ba707e620a470a49363e83896d4c94186c38e99da13e3bc2d70c` |
| Static stack review | `43789bf9325c958b0e32edc6dd7f92f2763bfd716b41b15ab1bffae099a45b04` |
| Independent cloud review | `0f3c870a8caafc398eda49188f46d34a4fc585491c1b0ecb2d22939e34e42aa6` |

### Closed TEST matrix and separate natural-refresh session

Independent review matched 104,343 raw bytes, 1,319 lines, 6,958 RX chunks and 15 commands.
The idle and Listening categories, all five phases, task-name snapshots, byte continuity and
QPC final-byte times were checked. Task bits are assistant/capture/supervisor; all Listening
before snapshots are non-stopping, and all recovered snapshots restore enabled/listening.

| Cycle | before → after → recovered | Internal free / B | Actual RX before next TX | New post-snapshot telemetry |
| --- | --- | --- | --- | --- |
| 3301 idle | 011 → 000 → 011 | 20,611 → 30,435 → 20,519 | 112.862 s | 4 |
| 3302 Listening | 111 → 000 → 011 | 5,851 → 30,671 → 20,807 | 75.596 s | 3 |
| 3303 Listening | 111 → 000 → 011 | 6,151 → 30,935 → 21,139 | 76.014 s | 2 |
| 3304 Listening | 111 → 000 → 011 | 5,651 → 30,695 → 20,863 | 76.957 s | 2 |

Coverage is complete's last byte through the final actual RX before the next explicit TX.
The initial enrollment is missing between approximately 4.5s of guarded-flash boot logs and
23.5s when continuous capture starts. The 01:21:18.349Z snapshot supplies only a conservative
latest upper bound. The accepted v2 anchor is the first measured RX at least 100ms after it;
the third wake has 634.6890717s of actual pre-wake QPC coverage with no later enrollment.
TTL600/margin30 remains unchanged without manual invalidation, rebind or tokenVersion
rotation. Device uptime then records refresh at 685845ms, enrollment at 687795ms, WSS ready
at 688595ms and the USB callback watermark at 688615ms. This is bounded natural-refresh
evidence, not an exact first-token issue/expiry timestamp.

The third session `9e429518-9b62-4f7e-9720-f94caefe44ff` received no cycle; its watchdog at
809215ms ended the session naturally. The fourth explicit wake created
`090b2348-0df6-48b8-973b-03bfd506a00d` for cycle3304. It reused the third wake's acknowledged
256 samples / single 512-byte zero PCM, without another audio_begin/chunk; exhausted input
continues as zero padding. Four wakes are not four cycles or four fresh fixture loads.

| Wake / AFE generation | current_failures | cancelled_results | Persistent wake_notify watermark |
| --- | --- | --- | --- |
| 1 / 5, cycle3302 | 1 | 1 | 2,204 B |
| 2 / 10, cycle3303 | 1 | 1 | 2,204 B |
| 3 / 15, natural refresh and watchdog stop | 2 | 0 | 1,100 B |
| 4 / 19, cycle3304 | 1 | 0 | 1,100 B |

Keep all 12 matrix warnings: seven SDK AFE empty, four current-rejected and one watchdog.
The summaries count five current failures; wrapper warnings are throttled and are not the
failure count. No MultiNet duplicate-cleanup error or fatal/reset was seen in this closed
matrix. Ten first-boot SD/test-entry/NVS/WiFi warnings are retained separately. Same-boot
internal minimum is 4,411 B. The fourth 1,100-byte sample is the same task's cumulative mark,
not another refresh measurement; do not compare independent boot minima as memory gains.

Two tooling deviations remain explicit: an earlier PowerShell JSON DateTime reparse lost
the UTC offset and selected a pre-snapshot anchor; it was rejected before expiry-dependent
action. listening4-before used a from about 2.331s in the future, so its zero telemetry count
is excluded. Its direct device/session read remains valid, and two post-complete records
independently establish cycle3304 telemetry coverage.

Closed TEST review SHA-256: `8a5414d70491134b00055a1fe71b5403102eb88ad9b63445424e9a33e6783f1a`.
SDK startup empty remains unresolved. This limited task/refresh observation adds no acoustic,
full physical-resource, arbitrary OOM or long-soak acceptance; resource/production **NO_GO**.

### Ordinary OFF restoration and final state

Ordinary package `20261008-092316` passed guarded restoration: immutable images were verified,
only otadata/main were written, NVS and the development signing root were preserved, and
Recovery/main/OTA/Home handoff completed. The closed smoke contains 18,953 raw bytes,
1,335 RX chunks, 264 lines and five explicit commands. Cold actual QPC RX is 69.121s with
three new telemetry records.

Session `8ac92a74-7ea5-4103-8e61-bb7ddd3cd0d0` matches server capturing. The explicit stop
ACK is followed by the same session's input stop, Interaction stopped and wake rearm.
Before the next clear, actual RX after stopped is 73.539s and after rearmed 73.322s, with
two new telemetry records. AFE generation3 reports current_failures=1/cancelled_results=1.
The raw retains three warnings (two SDK AFE empty, one current rejected); first boot retains
nine more. No MN duplicate-cleanup error or fatal/reset was seen in the captured window.
This boot's internal minimum is 4,683 B and wake_notify samples are 2,204 B; TEST/OFF boot
minima cannot be combined into a resource-gain claim.

No voice_cycle was sent on OFF. Ordinary stop/rearm does not prove capture/supervisor Deinit,
and this short OFF window does not test natural-expiry refresh. The TEST natural-refresh
observation does not transfer into an ordinary-refresh pass. Boot-to-capture gaps remain;
absence of observed errors is bounded to the captured windows.

The final 01:42:36.413Z device snapshot preserves the original ID, bound/tokenVersion4,
MQTT=true and voice=false. UI serial is disconnected, screen playback is stopped, with no
stop button or camera frame. Both captures were explicitly closed without in-flight or
blocked state; the final process snapshot has no serial/JTAG helper. Electron PID2140 is
the existing 026 process, labelled `d28370c82ee8818cb47924ccc5d091a3164e5761`; this is not a
fresh executable attestation, and documentation HEAD does not replace runtime identity.

| Closed-window review | SHA-256 |
| --- | --- |
| TEST | `8a5414d70491134b00055a1fe71b5403102eb88ad9b63445424e9a33e6783f1a` |
| Ordinary OFF | `daf2889bffe87828ecb6b1d349c5cd73a73433ee129b2cdfaaeb070c32ca81a4` |
| Combined hardware | `e310190daf371bdc04911425ab66c577e91816b82884ff45883ede36ef8bee67` |

Software, package/signature/ELF checks, flash logs and closed capture evidence are correlated
separately, beyond caller-provided capture labels. This is not reproducible-build or runtime
binary attestation. No Actions were queried, relied upon or repaired. Resource/production
**NO_GO** remains: startup AFE diagnostics, actual headroom, complete physical-resource return,
arbitrary OOM, acoustics and soak remain open.


## 2026-10-08 AFE output readiness and voice recovery 034

034源码 `66ab25cd7d1b2af8aa0fa1de8d4012fbc3d51781` 修复固定ESP-SR提前读取不足一帧
PCM的风险，保留单次错误后的自动恢复。TEST一格idle、三格受控Listening及同第三次
Listening会话的自然TTL600刷新已封存；普通OFF包 `20261008-113338` 已保NVS恢复，
独立cold及明确WSS stop/rearm窗口闭合。TEST仍出现一次running stall，资源与生产
保持**NO_GO**；030—033历史不改写为034结果。

### 实现与软件证据

固定1MIC/MR/16k/WebRTC路径中，device AEC开启时feed每通道256样本，关闭时160；
fetch固定512单声道样本/1024B。开启AEC时正常前几个feed返回320/640/320输出字节。
原033在feed前置started，且一次feed完成仍不足以保证完整输出就绪；SDK短读超时
可能已消费partial，只向调用者返回-1/0字节，发生在VAD之前。

034累计成功输出bytes并预扣完整1024B才进入current fetch，原100ms参数不变。单次
异常fetch后credit保持库存保守下界，继续自动恢复并标记PCM/AEC gap。不确定库存
达到四帧、已有不确定库存时feed返回0，或非零非法feed返回/credit溢出，才触发重同步。
feed0本身仍作为可见的输出drop；并非任何一次feed异常都立即重置。重同步阻止新lease，
旧feed继续排空，由唯一reader在feed归还后reset_buffer+reset_vad、清账本并推进epoch。
跨reset旧raw read及本地尾部不再feed；同代未AppendRaw的被拒read单列raw gap，已保存
raw只丢feed尾部则仅影响AFE连续性。WebRTC仍可保留不足160样本残余及AEC状态，不称
全新DSP。一般warmup/等待/resync保持Running；三次可观察reset失败才终止，Assistant
清理前复核interaction/transport双代次、录音阶段及非stopping状态。

USB/wake阶段观察改用同锁GetPhaseSnapshot，保留原准入与wake代次检查；Supervisor
仍读取一致的完整snapshot。HTTP/TLS/NVS留在原internal-stack wake_notify，无加栈、
新增常驻任务或迁移到PSRAM栈。持续100ms无完整输出仍报告warmup/running stall，
新日志区分首feed开始/完成、首fetch、首output及退出计数，不以隐藏WARN获得通过。

| 软件范围 | 结果及边界 |
| --- | --- |
| Frontend完整TU | 35 Debug、35 ASan/UBSan/leak；4/4 CTest；8项AFE精确负控，原MN与frontend retirement各2项保留 |
| device AEC关闭host配置 | 仅1项命名格式用例，验证feed160/fetch512/mono16k正常输出及6种错误格式拒绝；不代表完整AEC-off矩阵 |
| Assistant / Wake | 各30/36项分别通过Debug与sanitizer；5项capture/snapshot完整TU负控。原retirement独立报告保留，不跨套件重复加总 |
| 输入来源 | 实际编译live文件；58项列明host输入前后hash一致并留副本。171条跨三配置Ninja依赖记录为补充后验Git/SDK核对，不是171个唯一输入或全量前置hash |
| 固定SDK | CMake核对AFE/processor/ring archive及既有MN关键源/头/库；升级需重新审计。有效handle的buffer reset会吞底层状态，不能由返回1证明任意SDK故障已恢复 |

AFE软件封存SHA为 `b09c97a9d66be1dd653f0a9f709dab95b3a920b4f05dca3f3328a89a76e0102a`，
实现独审SHA为 `9db174efbdfeaef728294cdc7de75d8768387c9cdb502315aeaff21dda35a04d`。
8项AFE负控包含033完整旧TU、仅移post-feed flag仍丢partial，以及epoch/raw-gap守卫
变体；host丢160/640样本只说明机制，不是033真机计数。033没有首feed/fetch的次数与
时点，未证明其每次告警来自first fetch或TLS是唯一调度诱因。

### 两种制品与静态边界

| 身份 | TEST诊断 | 普通OFF |
| --- | --- | --- |
| 包 | `20261008-112651` | `20261008-113338` |
| main | 7,154,608 B | 7,147,776 B |
| main SHA-256 | `2e2d6ab87646ad34be3d0a037fb64f478c1c40e91a4bd9ce366fd50c63cde8a6` | `3bbfe03f5196a7dc5fc919f2baf3398998662549108d84e2a2926a3db90d4a72` |
| ELF SHA-256 | `6bb504a86a3041f9623c4f92f45bd9642f0725bb6001e83cc6bc7ee8599eea3d` | `1e376d536bd223dea1c1cb680583a6121f04847f398e7a79844cfd10d0d25b40` |
| 制品独审SHA-256 | `b1d745d5fc6c22e2c9503fc468fed84bda9b9e52d0a4c5794a0d4bed79f33774` | `f20b411b1f298aa8b93d58200c0e0bf1188973a9735852963d4e90dd9cfae945` |

两包共享上述源码并使用原开发签名根，源码、编译/对象、image内ELF摘要和guarded
部署分别核对，不声称可复现构建或仅凭version字符串证明运行时身份。普通bin/ELF/
对象中lifecycle诊断入口完全缺席。这里OFF指release-fault关闭，不是device AEC关闭。

两包最终ELF均确认USB lambda64B、HandleWakeWordDetected96B、GetPhaseSnapshot48B；
033前两者为176/240B，USB重叠caller固定帧少256B，声学wake路径少144B。AFE实例
528→576B（+48）。034普通对033普通同flavor的`.dram0.bss`和heap起点均+48B；034
TEST对033普通则+112B，是跨flavor布局差值。以上不等于完整调用链最大值、净运行时
free heap或HWM收益，不能把静态差额加到历史1100/2204/508B。

### TEST同boot矩阵与自然刷新

closed matrix为86,496B，raw SHA `3183963f7d6daedacf02dbc1dbac0c9480c81ed02feeafcc405dae258a697c44`。
14条串口命令各自发送/完成，无写错误、inFlight、blocked或cycleBusy，root明确关闭。
三次会话各新加载一块256样本/512B零PCM并取得begin/chunk/wake ACK；EOF继续补零，
真实I2S及参考通道仍工作，不代表真人语音或物理声学验收。

| cycle | session / AFE generation | 结果 | complete最后字节至下一TX前实际RX | 新遥测 |
| --- | --- | --- | --- | --- |
| 3401 idle | 无活动session / N/A | idle_cycle_pass | 70.961s | 2 |
| 3402 Listening | `bcb83bc2-e332-4ee2-80d5-d9d732edc722` / 5 | three_task_pass | 78.313s | 2 |
| 3403 Listening | `ea5c2958-86b1-4941-ab81-80c7d5456a9f` / 10 | three_task_pass | 328.534s | 2 |
| 3404自然刷新Listening | `d07d1f58-af7f-4b44-a210-1420b80e091d` / 15 | three_task_pass | 88.115s | 3 |

四格均有accepted/before/after/recovered/complete。idle before仅capture/supervisor在；
三格Listening before三任务齐全且非stopping。after三任务均无，随后capture/supervisor
与enabled/listening恢复；后者指wake监听，不是继续原assistant会话。实际RX使用QPC
原始字节时间与下一TX排除边界，不能以采集进程存活或UI重读替代。

运行时TTL为600s。初次enrollment落在启动日志与matrix采集之间的缺口；以
`2026-10-08T03:32:46.089Z`成功MQTT快照为保守最迟上界，取其后至少100ms的第一
actualRX（QPC `216359609001100`，offset809）为锚，同boot无后续成功refresh，实际
累计**631.2673767s**才具备新wake资格。等待在assistant idle期，不是维持WSS600秒。

第三次新wake在uptime696195ms开始refresh，698615ms成功enrollment，随后同session
`d07d…` WSS/input.start/capturing，并及时执行3404；本轮没有watchdog结束后拼接新
session的情形。原始enrollment精确时点仍未知，不把保守资格锚改写成实测enrollment。

### AFE输出、保留告警与HWM

三次TEST均ready为feed256/fetch1024B/capacity51200B，首feed返回320B。

| AFE generation | first feed begin/duration ms | first fetch/output相对创建ms | current/cancelled | feed errors / stalls / resyncs | stopped credits / uncertain |
| --- | --- | --- | --- | --- | --- |
| 5 | 41 / 13 | 108 / 109 | 0 / 0 | 0 / 0 / 0 | 256 / 0 |
| 10 | 43 / 13 | 109 / 111 | 0 / 0 | 0 / 0 / 0 | 512 / 0 |
| 15 | 44 / 12 | 105 / 106 | 0 / 1 | 0 / 1 / 0 | 1536 / 0 |

三次first fetch恰观测feeds=returns=3，预扣后credit256，first output1024B；此为本窗
观察，不是固定“三次feed”验收门槛。停止后credit可包含取消时未交付的完整帧，不
泛化为全部不足一帧或资源泄漏。所有resync_discarded为0，未在硬件注入resync失败。

TEST matrix保留**1条W**：uptime698865ms、generation15的`phase=running count=1
feeds=124 returns=123 credits=256`。它发生在已有首完整输出之后、自然刷新过程附近；
没有逐次feed耗时轨迹，不能确定具体阻塞点或将其改叫warmup。current failure、SDK
empty和wrapper rejected均0也不意味着无输入停滞。启动日志另有10条W（SD delay回退、
测试开关、Settings缺失namespace与WiFi连接/auth阈值）；捕获窗口未见MN重复清理、
panic/reset或watchdog，边界仅限所采窗口，不能覆盖所有OOM。

| TEST会话顺序 | wake_notify累计HWM | assistant_io首帧 / stopping HWM |
| --- | --- | --- |
| 3402 | 2460B | 25772 / 25740B |
| 3403 | 2460B | 25964 / 25740B |
| 3404自然刷新 | 1356B | 25764 / 25732B |

TEST同boot internal历史minimum为4507B。supervisor HWM在初始lifetime为2388B，
cycle重建后的观测为2700→2396、2700→2396、2636→2396、2440→2392B，按各次
lifetime解释。wake_notify可跨frontend Deinit存续，1356B是累计历史minimum，不是
当前SP；assistant_io重建各自计数。不同boot、任务lifetime和静态帧差额不拼为净收益。

### 普通OFF恢复、独立窗口与终态

普通113338保NVS guarded刷写、写后校验与Recovery→main→OTA确认→Home已核。
独立raw为22,219B，SHA `b659bd24056ff9377b03bdced710d182e278c74c80a9f87b2184545dc6dddc1f`，
5条命令begin/chunk/wake/stop/clear均取得ACK，没有cycle。新普通session为
`65e8793d-df0f-4d79-b3a7-0ee53fff4e04`，AFE generation3：first feed44/11ms、written320；
first fetch89ms（3/3、预扣credit256），first output91ms/1024B。current/cancelled/
feed errors/stalls/resyncs/resync_discarded/uncertain均0，停止credit1536B保留原值。

普通cold从after-flash快照后的第一actualRX至cold-post前最后RX共**78.6965894s**，
有3条新遥测。显式stop后的Interaction stopped至stopped-post前最后RX为
**75.0805431s**，重新armed至同一末RX为**74.8730344s**，对应2条新遥测（uptime
184946/214926ms）。共同末RX QPC为217455406325900；这些是明确快照截止前的覆盖，
不是笼统把整个采集时长当stop后观察。

普通matrix无W/E，启动W单列为9条W（SD delay、Settings缺失namespace、WiFi连接/auth阈值）。wake_notify为2460B，
assistant_io首帧25964B、stopping25740B，supervisor初始2388→stop/rearm后1796B；
该普通boot internal历史minimum4219B。普通未做自然TTL到期刷新，不能与TEST1356B/
4507B跨boot计算容量收益；普通stop/rearm也不等于capture/supervisor Deinit。

`2026-10-08T03:51:31.812Z`最终快照：原设备ID/绑定与tokenVersion4保持，MQTT在线、
voice=false、session空，Wake enabled/listening；shadow1121，遥测uptime递增。
audio_clear已ACK；UI串口disconnected、无活跃屏幕/相机媒体，capture明确关闭、无
inFlight/blocked、无COM/JTAG helper。桌面运行身份按进程/既有构建证据解释，不以
本轮仓库HEAD替代运行时二进制，也不因已有Electron PID认定可复现构建。

原件根为`D:/workspace/rodak/.codex-temp/voice-health-034/`。TEST硬件独审SHA
`8dff6fc8548fe9dd56b48b305a1bc0965f12f9e6c3923c1550d22e83be845669`；
普通硬件独审SHA为`0a94af3a6f0816d6f77f8d44168176be0e576756210202822a4ea6c7a9a976e2`，
双窗总审SHA为`7d55b9798f3d95ae1bdf470c9bb3f00300f3d1b7441effe67db32b452cde9141`，
共49份使用证据已复制冻结且原件/副本hash一致。

本轮未查询或依赖GitHub Actions。已完成有限矩阵、自然刷新同session cycle与普通
恢复，仍需定位generation15的running stall和实际余量；全PCM声学、全部DMA/IRQ/
codec/TCB/heap资源归还、任意OOM、更广取消/并发、实体触摸及八小时长稳未关闭。
资源和生产继续**NO_GO**。

## 2026-10-08 AFE stall observability 035

035源码 [`b5c17a9e5d35e07714b8f3b07e9160be0a319512`](https://github.com/rymcu/rodakos/commit/b5c17a9e5d35e07714b8f3b07e9160be0a319512)
只补齐034 running stall的分段观测，不把`124 feeds / 123 returns`改写为DSP内部阻塞。
原100ms、完整帧credit、取消排空、保守库存、epoch/raw gap、有限reset与terminal判断
保持；034包、会话、水位和一次running W仍归上一节，根因与资源/生产**NO_GO**不变。

### 已验证的软件与观测合同

Capture用独立portMUX发布generation/epoch/stage/seq及阶段时点，返回后先发布
`returned_wait_publish`，再获取业务锁记账。`raw_read`包含输入锁、调度和codec调用；
`api_boundary`包含标记后的抢占、SDK内部计算或等待；return-to-publish包含诊断发布、
调度与业务锁调用。它们都是墙钟区间，不能自动归因TLS、DSP或互斥锁竞争。
`credit_published`只表示返回校验/记账结束，不保证credit增加。

每个W保留count和首W的稳定gap_id，失败fetch后重复W不抑制。仅同generation/epoch
的完整有效fetch闭合为recovered，取消/reset另列；stall与closed都在业务锁内冻结
独立producer元组、再采观察时点，锁外输出。closed保留完成后的read/API/记账最大值
和首W前后、fetch返回再次取锁的consumer最大墙钟。producer最大值属于整个epoch，
可能早于该gap；不匹配元组标stale。u32耗时等于4294967295时为饱和下界。

| 检查 | 结果与范围 |
| --- | --- |
| 完整Frontend TU | 49项Debug、49项ASan/UBSan/leak；原35项加14项观测用例 |
| 五组CTest / 14个精确负控 | retirement2 + MultiNet2 + AFE8 + 新观测2；返回标记移到锁后与日志放回锁内均以专属断言拒绝，编译失败/超时不算检出 |
| 输入冻结 | 三配置各84个非系统输入before/after一致，实际Ninja非系统依赖无遗漏；系统输入只记构建后provenance |
| device AEC OFF | 仅命名格式/启动输出1例，含6种错误格式；不宣称全OFF suite或设备验收 |

测试直接运行真实TU，host私有锁/计数仅用于定点门闩；新观测测试单TU使用
`-fno-access-control`，生产正常编译，没有新增生产测试回调。覆盖SDK返回先于业务锁、
日志阻塞时producer进入第二次SDK调用、旧raw跨代stale、consumer延迟、同gap多W、
快照后采时及日志阻塞后仍保持冻结tuple。codec、DSP与FreeRTOS调度仍是host替身。
软件封存SHA：`6a9eee95769ee2516d418b25d5391d4c4239170e5aadfd9f30a92ce706549f1c`。

### 已测静态成本，不是运行时余量

固定034普通编译参数的隔离完整TU检查，普通/TEST布局相同：

| 项目 | 034 → 035 |
| --- | --- |
| Frontend实例 | 576→640B，+64B：56B payload + 8B portMUX |
| Capture固定帧 | 176→240B，+64B |
| Fetch固定帧 | 112→368B，+256B |
| gap / producer日志helper | 128B / 112B；非tail调用相叠，不能只看Fetch一帧 |

已知Fetch入口32+Fetch368+gap128+producer112为640B应用帧叠加，仍不包含完整logger、
ROM、port/RTOS链，更不是HWM或整栈最大值。快照临界区有固定56B memcpy；正常每个
conversation read有6次Publish、每feed另4次，存在持续timer与临界区成本。未增加任务、
调整优先级/核或使用64位atomic。正式包最终ELF已核对如下；实际水位与时序不能由静态帧推定。

### 035制品与闭合设备观察

两份主构建/包均使用本节源码和既有开发签名根；签名、不可变区、输入前后hash及
最终ELF已独立核对。普通OFF包未包含TEST生命周期诊断入口，保留本节AFE观测。
制品审核只证明包内容；实机来源仍以root的刷写/启动记录和对应闭合窗口关联。

| 制品 | Main bytes / SHA256 | ELF SHA256 | 制品独审SHA256 |
| --- | --- | --- | --- |
| TEST `20261008-134654` | 7,160,688 / `c8e98083de8511939461aafb32178ba7bccc27e9f18cc7a75fb20adeb4a7d5a3` | `7a36d53ac4ca7ee67966c9498baa223a0ce8076a50ff33ecb8468cc4a595c5b4` | `5bae7ae01a2728a2732b9c2b2bf6d283114bef56a04037ecbefc8dbf69721bb9` |
| 普通OFF `20261008-140725` | 7,154,016 / `8e8aacdd9a6c4c86e38ffdf45cf689fc98e1db1b8d99928ccac07523a3535f24` | `79061e6f20483cbe76e4ae5d7409aaf839b4337c3238ffbd6265468d0e9111e0` | `a4c9d642b179181301d343657fb83758b86a7ebf669bf7b42b354fe760cc9740` |

最终两份ELF均确认Capture240B、Fetch368B、gap/producer helper128/112B；已知应用帧
叠加640B仍排除完整logger/ROM/RTOS链。普通OFF相对034普通的`.bss`与heap起点增加64B；
TEST相对034普通增加128B，后者是跨flavor比较。它们均不表示净空闲heap或实际HWM。

| 独立wake窗口 | session / AFE generation | 首次完整输出 | 显式停止后到下一TX前的实际RX |
| --- | --- | --- | --- |
| TEST同boot未到期 | `27cfd426-c308-40e2-8d2e-2de8d8a89c10` / gen3 | 79ms / 1024B | 552.4325703s；至下一TX的边界间隔552.5384871s |
| TEST自然到期后的新wake | `1934799f-8541-486e-b130-89dd0679b002` / gen7 | 85ms / 1024B | 84.9470201s；至下一TX的边界间隔86.2447437s |
| 普通OFF独立wake | `2d869f75-9560-45bb-a036-df1d017495fc` / 本boot gen3 | 740ms / 1024B | 130.8833851s；Wake重启后实际RX130.2058565s |

五组gap均发生在所属显式wake尝试的后续唯一WSS session ready之前，GUID用于尝试关联，
不表示这些AFE行发生于已ready的WSS内。三个尝试分别正常显式stop，随后Wake重新启用；
闭合会话窗口没有watchdog记录，不合并
不同session。停止汇总的current failures、feed errors、resync、discard与uncertain均为0；
末credit分别1472/960/768B，并不宣称停止时库存归零。普通OFF的完整冷窗口在首次命令前
有75.1242935s实际RX。三个stop后窗口分别收到19/3/5条新遥测，OFF冷窗口另有3条。

TTL600资格使用较晚的保守上界：初始enrollment发生在boot/capture缺口，
`2026-10-08T06:02:50.816Z`的已在线快照之后，首实际RX至资格末RX为640.5252232s，
无中间refresh/enrollment/reset标记，资格快照为空闲。这段凭据年龄下界包含首session，
不是连续idle640秒，也不是同一WSS存活600秒。首stop至expiry-begin为552.5384871s，
至expiry-wake为585.0383592s。新wake的AFE先输出，随后凭据刷新；enrollment完成在
uptime703840ms，第三个gap在此之后、session ready之前。仅此次TEST新wake有自然到期
资格，不将该结论移植给普通OFF窗口，也不假称测到了原token签发时刻。

五个gap均为epoch0、scope=current，stall/producer/max及closed/producer/max配对完整，
每个gap只有一次W并以同代完整fetch闭合recovered。下表全部耗时为微秒墙钟。

| 窗口 / gen / gap | W时stage与关联序号 | wait → closed elapsed | consumer最大观察 / 最大取锁调用 | 本gap已完成API边界或证据限制 |
| --- | --- | --- | --- | --- |
| TEST未到期 / 3 / 1 running | api_boundary，seq5，age202594；feeds5/returns4 | 225235 → 241883 | 204102 / 8 | closed补齐seq5 API213131；return-to-publish最大14 |
| TEST自然到期 / 7 / 1 running | api_boundary，seq4，age636167；feeds4/returns3 | 637036 → 668587 | 637024 / 5 | closed补齐seq4 API646182；return-to-publish最大14 |
| TEST自然到期 / 7 / 2 running | api_boundary，seq6，age677955；feeds6/returns5 | 669168 → 699652 | 629032 / 224 | closed补齐seq6 API691280；该API可以早于首次不足credit观察 |
| TEST自然到期 / 7 / 3 running | between_reads，read_seq1840，age329577；feeds12/returns12，feed_active0 | 425016 → 451725 | 333975 / 7 | epoch最大API691280仍属先前seq6，不能当作本gap的SDK调用；return-to-publish最大35属seq12 |
| 普通OFF / 3 / 1 warmup | api_boundary，seq3，age634066；feeds3/returns2 | 678184 → 698849 | 624356 / 39996 | closed补齐seq3 API640988；return-to-publish最大13；首fetch735ms |

四个API边界gap均同时出现较长消费者观察间隔；未到期与普通OFF也可复现，故不能只归因
自然到期刷新。`between_reads`一项证明不同停滞不一定有进行中的feed。普通OFF的39996us
是业务锁获取调用的墙钟，仍包含调度，不能直接等同互斥锁持有时间。上述数字不证明DSP、
TLS或输入驱动为根因；保留所有W，不用一次完整fetch恢复宣布根因修复。

两个采集窗口均已`portClosed=true`，无未完成TX/写错误/尾部残行；TEST为9条命令/9个
匹配ok回复，普通OFF为5/5。未执行旧`voice_cycle`。会话捕获内TEST4W、OFF1W，无额外E；
启动日志的TEST10W、OFF9W独立保留，不能以会话窗口无额外E掩盖boot告警。
TEST raw为72,688B，SHA256 `4daa8aac7eff62b71b668d116e5842e50b32b12123ac4b521667279deff535b2`；普通OFF为26,310B，
SHA256 `667fad6528bd450a0259ff70e7e349b0e4261ed930557e3412d3308d19168f5a`。完整采集存活时间不充当stop后的实际RX。

普通OFF终态快照`2026-10-08T06:24:04.446Z`仍为原ID
`c78845a8-06c9-4dcd-b7ff-d33e599f23ff`、MAC`44:1b:f6:c3:b4:30`、bound、activated、
tokenVersion4，MQTT=true、voice=false、sessions=[]；UI串口断开、屏幕同传停止，
串口助手进程在稍后的`06:26:48.434Z`检查为owners=[]，不回填成设备快照时刻的结果。
API事件是有限列表，不以单次快照证明全部历史或物理资源释放。

闭合硬件独审为`PASS_LIMITED_CLOSED_WINDOWS_WITH_STALLS_NOT_RELEASE_ACCEPTANCE`，
openBlockingFindings=[]；SHA256
`aaf588c1046c667e6b118ffc13c2a6ace951592ac693490a1eb39324e8b35d6c`。
TEST/OFF观测internal最小值分别4499/4743B；既有USB诊断worker的生命周期栈最小值分别
为2460/2460/1308/1308B与2348/2348B，重复值为累计水位，不是独立测量，也不表示
实际余量充足。两flavor属不同boot，不能相减为净收益。独审通过范围是闭合证据完整性，
不是零stall、声学或物理资源验收。原件根`D:/workspace/rodak/.codex-temp/voice-stall-035/`。

后续只在现有五个gap证据上规划有界的任务调度/中断/cache停顿与API内部等待观测，
区分API执行前抢占、内部等待、返回后发布及两次read之间的producer推进；先定义成本和
退出条件，不调整100ms、TTL600、优先级、核绑定或缓冲参数来消除日志。全PCM声学、
实际heap/stack余量、完整物理资源、任意OOM、并发与长稳尚未关闭，资源/生产继续**NO_GO**。

## 2026-10-08 Bounded tick and cache observation 036

源码 [`7aa58fc54bfc5f7f487e96d07f577860c3c0235f`](https://github.com/rymcu/rodakos/commit/7aa58fc54bfc5f7f487e96d07f577860c3c0235f) 已推送。在 035 的 API 边界、消费者观察与 `between_reads` 墙钟记录之上，036 增加仅 TEST 编译的双核 tick/scheduler/cache 摘要；不调整 100ms、TTL600、优先级、核绑定、credit/reset/terminal 合同，也未修复或关闭既有真实 gap。

独立模块占 320B 内部 DRAM（两核各 128B，控制 64B），不新增任务或 frontend 条件成员。每 generation 从 AFE 起始计固定 20 秒 deadline，轮窗和 epoch 切换不续期。20 秒到期不会注销 tick hook；inactive/expired 仍有回调前置开销，只有普通 OFF 完全缺席。ISR 用真实 timer 记录相邻入口间隔及 getter 包络，单次 try-lock 发布失败仍推进私有 predecessor；前景三锁任一失败不部分切窗。异常计数、最后异常 sample-end、已发布端点及 prefix/crossing/drop/deadline 等标志保留，冻结后才打印。双核非同时采样；零异常、零已发布 drops 或区间相交均不能推出完整覆盖或根因。

### 软件闭合范围

| 检查 | 结果 |
| --- | --- |
| 完整 OFF AFE TU | Debug 49 / ASan、UBSan、leak 49；Debug 五组 CTest 通过 |
| 真实 TEST observer 模块 | Debug 11 组 / ASan、UBSan、leak 11 组 |
| 精确负控 | 原 AFE 14 + observer 4 = 18，均命中特定断言 |
| 隔离 OFF 目标对象 | 全部 ALLOC sections/relocations、undefined symbols 与 035 基线一致，observer 引用缺席 |
| OFF/TEST ABI | frontend ABI 对象一致，实例保持 640B；observer 内部状态 320B |

60+60 是两套 host suite 的合计，不代表完整 TEST frontend 跑过 60 项集成测试。隔离对象不代替最终 linked ELF 或实机 HWM。

两处旧断言经固定 035/036 OFF 受控对照后修正：gap-stop 用例只禁止旧 generation 被 recovered，同时强制新代确实恢复；非法 feed 返回用例明确门控 resync 排空，核验允许的 partial drain、lease 退出后 reset 和后续完整帧，保留零返回、不重叠 reset 与保守 discontinuity/VAD 约束。两版本对照行为一致。原失败报告保留，初次 `/tmp` 二进制未留存的限制已记录；不将它们改写为设备 gap 修复。

软件总报告 SHA-256 为 `e78e99a3b0ad90fc58fa83a13afc56e1c7b80b5ef3fc8f2d5c8a5e530089fff0`，构建/实机前的软件独审快照标识为 `PASS_SOFTWARE_CANDIDATE_HARDWARE_PENDING`，其后制品与硬件按下文独立报告闭合。证据根为 `D:/workspace/rodak/.codex-temp/voice-scheduling-036/`；observer 原 seal 继续保持 `PASS_NEW_OBSERVER_MODULE_ONLY` 与原历史备注。

### 两份制品与普通 OFF 恢复

两份制品均来自已推送源码 `7aa58fc54bfc5f7f487e96d07f577860c3c0235f`，沿用原开发签名根及不可变 Recovery/bootloader/partition。TEST 与普通 OFF 的源码、包、签名、ZIP、不可变区和最终 ELF 分别独审；不将 TEST 结果移植为普通运行结果。

| 制品 | 主镜像 bytes / SHA-256 | 最终 ELF SHA-256 | 制品独审 SHA-256 |
| --- | --- | --- | --- |
| TEST `20261008-171300` | 7,164,672 / `1c32425498554a5f3ba1e8134f39c2b443028130d5e6f44e2442228d124fcfc2` | `cd1ab22805510ae76584c2b4fc8ddb4f0b7ccd5bc6e0af4ade0c04e0a10a32d7` | `f5762a930ff38959a90c092dc1fd28f1b1a33120d8d8d83efbef92dbe89d264b` |
| 普通 OFF `20261008-173328` | 7,154,016 / `456fcebb08ebf585d1803bbfa9f63926442519ae73e751db9ccd45d33de6ae5e` | `5caf5d1e97ade369679a8d6ff838b7dc6295c3e88f3f176d6398d181de6d1a13` | `0af24528d20b5c9a3c89a59044da81cd376f18bebefcd7f296647676accd40e6` |

最终 linked 审查 TEST/OFF SHA-256 分别为 `51180e0bb95a703fcafc504d5d0481930da085eea93c6fd97adc2069de84f979`、`b28cc85eb026002fdbb90bcedf6003191fc12a175aecf79a986439fe4a8389c7`。普通 OFF 的 observer 符号/状态缺席，已核静态布局、语音实例及 heap 边界相对 035 普通 OFF 差值均为零；设备 AEC 仍启用。

TEST 相对 035 普通 OFF 的 `.iram0.text` +768B、`.dram0.data` +320B、`.dram0.bss` +64B、`_heap_start` +1152B。64B 属既有 TEST flavor 差，不归 observer；320B 仅是模块静态存储，不能当作全部链接成本或净运行时 heap 减少。静态布局与有界正常调用链审查均不代表实际 HWM、完整 SDK/ROM 错误路径或安全余量。

### 闭合设备窗口与全部 gap

TEST 保留未到期和自然到期后新 wake 两次独立尝试，普通 OFF 恢复后另做一次 wake。均为 USB 受控静音语音流程，各自建立唯一 WSS 后显式停止；不代表真人唤醒或声学验收。四个 gap 均为 epoch0、scope=current，各有一次 W 并以完整 fetch recovered。它们都在所属 wake 尝试的后续 WSS ready 之前，session GUID 用于尝试关联，不表示 gap 发生于已 ready 的 WSS 内。

| 尝试 / generation / gap | W phase | wait → closed elapsed（µs） | W 时 API 边界 / 闭合后 epoch 最大值 |
| --- | --- | --- | --- |
| TEST 未到期 / 3 / 1 | warmup | 118653 → 151138 | seq3 age71525；闭合补齐 seq3 API80520 |
| TEST 自然到期新 wake / 7 / 1 | running | 647269 → 689558 | seq59 age627884；闭合补齐 seq59 API645948 |
| TEST 自然到期新 wake / 7 / 2 | running | 725060 → 750279 | seq87 age634464；epoch 最大 API645948 仍属先前 seq59，不能归成本 gap 的 API 耗时 |
| 普通 OFF / 3 / 1 | running | 301677 → 329477 | seq4 age301179；闭合补齐 seq4 API308486 |

对应 session：TEST 未到期 `ba61bc36-f0d6-4db2-a10b-63c4700a2282`、TEST 到期后 `d9a8827c-b069-4885-83f0-274f007733ae`、普通 OFF `2a6c5cd2-8257-4e5e-aa98-3f13dfc4d95d`。第二次 TEST wake 的凭据年龄资格与首 stop 后 idle 覆盖分开记录，不把包含首会话的年龄区间写成连续 idle，也不声称同一 WSS 存活 TTL600。

闭合 raw/QPC 核验确认 TTL600 资格的凭据年龄下界为 **635.2226713s**，其中首 stop 后 idle 实际 RX 为 **573.4262774s**；保守 anchor 至到期后新 wake 为 **636.4128038s**。anchor 与资格点均在闭合原始记录中，期间没有捕获到 reset/refresh；新 wake 后实际发生凭据刷新和 enrollment。enrollment 本身早于该采集，资格来自更晚 MQTT-online 快照和实际 RX，不假称测得签发时刻。

| 独立尝试 | 显式 stop 后实际 RX（s） | Wake rearm 后实际 RX（s） |
| --- | --- | --- |
| TEST 未到期 | 507.6622954 | 507.2146613 |
| TEST 自然到期新 wake | 98.1242954 | 97.3238611 |
| 普通 OFF | 92.2071903 | 91.4694239 |

以上均严格截止到下一条任何 TX 之前，不将后续命令后的串口时间加入；普通 OFF 首命令前 cold 实际 RX 另为 **78.0303288s**。TEST 9 条、普通 5 条命令均匹配发送/完成/成功原始 ACK；未执行 `voice_cycle`。四条串口 W 及刷写启动日志的 19 条 W/E 全部保留，不以 gap 恢复或会话窗口结果掩盖 boot 告警。

### 双核摘要能缩小什么

TEST 实际捕获一次成功 registration（mask3、320B、20,000,000µs）及 token1—8 的八个双核窗口，均保留各核记录；token3 与 token8 是 `flow_stop/status=4`，表示固定 deadline 已到，不能解释为整段停止前仍有连续覆盖。

| token / 事件 | generation / gap | CPU0 / CPU1 最大相邻入口间隔（µs） | 该最大区间与 gap 的关系 |
| --- | --- | --- | --- |
| 1 / stall | 3 / 1 | 10003 / 10000 | 两核最大值均早于 gap |
| 2 / recovered | 3 / 1 | 10005 / 10001 | 均相交；CPU0 只有 7714µs 与 gap 重叠 |
| 3 / flow_stop expired | 3 / 0 | 10013 / 10037 | 无 gap，deadline 后尾段未覆盖 |
| 4 / stall | 7 / 1 | 10007 / 10000 | 两核最大值均早于 gap |
| 5 / recovered | 7 / 1 | 10003 / 10000 | 均相交 |
| 6 / stall | 7 / 2 | 10007 / 10000 | CPU0 相交，CPU1 最大值早于 gap |
| 7 / recovered | 7 / 2 | 10001 / 10000 | 均相交 |
| 8 / flow_stop expired | 7 / 0 | 10020 / 10012 | 无 gap，deadline 后尾段未覆盖 |

getter 最大采样包络为 1—2µs，全部已发布 drops 差值为零，仍不能排除未发表的 private 丢样。token3 两核 last accepted sample-end 至 freeze 的尾段约 4.572/4.577 秒，token8 约 1.022/1.017 秒；这些时间不能填成正常 tick 覆盖。首部、跨窗前驱、freeze 前尾部和双核非同时采样限制继续保留。

摘要确实记录到 scheduler suspended 与 cache disabled 端点，不能把“最大间隔约 10ms”写成状态一直正常。例如 token6 的 CPU0 suspended/cache 计数为 4/2，末次异常点 `666317339µs` 位于第二个 gap 内；这些是离散 getter 端点，不量出 cache 持续关闭时间，也不能归因 NVS。token4 CPU0 的末次 cache-disabled 点 `664867339µs` 早于其 gap 起点 `665188092µs`；不能把整个窗口计数归成本 gap 的异常。

已发表区间中未见与数百毫秒 gap 同量级的相邻 tick hook 空档，因此只缩小“相应已观测范围内长时间不服务 tick”这一假说。tick hook 被服务不证明 Capture/Fetch 或目标任务获得执行，不排除任务饥饿、SDK 内部等待、两次采样间短停或未覆盖区间；cache-enabled 端点也不能证明无 cache freeze/总线停顿。普通 OFF 没有 tick 行与其最终编译审查相符，但无日志本身不能证明 OFF 身份，也不能将 TEST 的观测结论移植给普通 gap。根因仍开放。

### 关闭与终态

TEST/普通两采集均 `portClosed=true`，无未完成 TX、写错误或尾部残行。TEST raw 为 77,954B，SHA-256 `bb483380d928506af3c9f2424f978c87e53eed799b08e704cec285f0be977df0`；普通 raw 为 20,744B，SHA-256 `4623754998750d90862f0f4589ef4987aa58da25554f04d99e6b1e0752bcecdf`。parser 冻结 SHA-256 `852b5ad18d599ce53a6bef91a838d44b30a2c864baa1ec01db8ff4cecf63ae10`，17 项合成自检与 16 项独审通过；真实日志先后与包/boot 身份仍由硬件独审分别核对。

普通包 `20261008-173328` 已恢复。`2026-10-08T09:57:41.302Z` 设备快照保持原 ID `c78845a8-06c9-4dcd-b7ff-d33e599f23ff`、MAC `44:1b:f6:c3:b4:30`、bound、activated、tokenVersion4、MQTT=true、voice=false、sessions=[]、shadow1134。UI 快照 `09:57:41.961Z` 为串口 disconnected、屏幕未播放、无 stop 按钮/摄像头帧；串口进程检查在 `09:57:42.2527094Z` 为 owners=[]，不回填成更早设备快照时刻的结果。

综合硬件独审为 `CONSISTENT_CLOSED_TEST_AND_ORDINARY_RESTORATION_EVIDENCE_NO_GO`，报告 `hardware-independent-review/review.json` 的 SHA-256 为 `8fa3833e8d04e1cc1f53aa3a8311a5c5f4e1e5d330033b27d00c74091564b355`。独审确认上述闭合证据与普通恢复一致，不给出根因或资源验收。TEST USB 诊断 worker 累计最小栈为 1308B、internal minimum 为 3751B；普通包对应观测为 2460B、4787B。它们分属不同 boot，不能相减为探针成本或净收益，也没有测得真实 ISR HWM 和所有任务余量。

035 及更早 gap、失败、水位与制品保持原身份。完整物理资源归还、实际 heap/stack 安全余量、声学、更多取消/并发、任意 OOM 和长稳仍开放，资源与生产继续 **NO_GO**。

## 2026-10-08 Per-feed identity and progress observation 037

源码 [`d852d9fdb6b16a0a34a49eb7555ddd5a9b944396`](https://github.com/rymcu/rodakos/commit/d852d9fdb6b16a0a34a49eb7555ddd5a9b944396)
已推送。037 在 036 双核 tick/scheduler/cache 窗口上增加逐 feed 身份和 current-task opaque
端点观察，补齐 W 所指 seq 与完成 tuple 的归属。100ms、TTL600、优先级、核绑定、缓冲、
credit/reset/terminal 及既有 AEC 合同不变；观察代码不构成真实 gap 修复。

### 软件测试、修正与来源

| 范围 | Debug | ASan/UBSan/leak | 精确负控 |
| --- | --- | --- | --- |
| 完整普通 OFF AFE frontend | 49 项 | 49 项 | 14 个旧 OFF 负控，Debug 执行 |
| 独立 tick observer | 11 组 | 11 组 | 4 个 |
| 独立 feed observer | 12 组 | 12 组 | 8 个 |
| 完整真实 TEST frontend + 两个真实 observer | 7 项 | 7 项 | 4 个 |

合计 30 个精确负控拒绝指定回归断言，不把编译失败当作负控通过。四层测试有不同替身、
编译和运行边界，不能称为一个去重后的 79 项完整硬件套件。TEST frontend 确实覆盖真实
SDK Feed 调用点前 arm、返回后业务锁等待前 close、Stop/Deinit、epoch/resync、到期及
句柄回收流程；SDK 音频与 tick 端点仍由 host 控制，不能证明设备 DSP/IRQ 调度或声学。

本轮发现并修正 read/feed 同值序号混域、倒退时钟洗掉 post-return 端点，以及失败 Close /
任务退役后继续采样复用 opaque 句柄。Close 在完整 immutable 身份匹配后先撤采样，再尝试
拷贝；flow_stop 先 retire。两个 atomic32 gate 只 load/store，目标对象与最终 TEST 链未
引入 atomic helper 调用。全 ELF 其他模块既有 atomic helper 不属于本缺席断言范围。

软件总报告 `software-verification.json` SHA-256
`bb27c18544475752dffe96d48a84155eec396d29c2bb295d073c31d875e28818`；source / host /
隔离 target 独审 `software-independent-review/final-review.json` SHA-256
`01b4b2b1840a57810e0b1c2d14e90ba664538bed8f980949df0302db0992bdf7`。两份报告原有
hardware-pending 状态保留，不因后来硬件取证重写。证据根为
`D:/workspace/rodak/.codex-temp/voice-progress-037/`。

### 固定观察预算与解释范围

按 capture / boot / generation / epoch / feed seq / ticket / target 配对；current
`api_boundary` 以外的 read、between_reads 或未知身份不借用同值 feed。每代 20 秒 deadline
不延长，open + complete 合计最多 8 条，再有一条 flow_stop summary。open arm upper 未知；
post-return、drop、clock invalid、饱和、retired、抑制与缺日志不升级为 strict 结果。

有效 strict counts 仅约束被接受 getter 端点在 marker-to-return 包络内；first/last 中可有
任意采样空洞。other 端点可反驳“这整个已验证包络始终选择 Capture”的强假说，但不能区分
Capture Ready 与 Blocked；target 端点不能证明 SDK 指令执行或前后连续运行。不计算
`counts × 10ms`、CPU 占用或 PCM 时长；W tuple 与取时不原子，age 不当作严格执行下界。

### 真实 SDK 数据流与 036 解释边界

本机链接 ESP-SR 2.2.2 的 1MIC feed 是同步路径，旧 `.ref` 的异步 worker 结构不适用。
feed 私有 mutex `data+180` 与 fetch 私有 mutex `+184` 分开；在已核单 writer/reader、
无并发 destroy 的合同下不互争。真正可能竞争的是主 ring `+28`，其持锁区包括普通 copy
及最多 8192B VAD history copy，不跨数据/空间 semaphore 等待或 AEC/NS/VAD 算法。
实际 `dl_nn_memcpy → dl_tie728_memcpy` 是 225B 叶子复制循环，无显式 RTOS 等待；它仍可
被抢占，不能据此给出持锁墙钟上界。

当前 fresh 实例/单 writer/最大输出 640B 的合同下，036 seq3、seq4、seq59 的主 FIFO
free 下界分别为 49920/49280/14080B，均大于最大需求 8832B，排除了这些具名调用的
主 FIFO 空间不足；不套用 seq87 或任意异常配置。036 已完成 API 包络分别为 TEST fresh
seq3 80520us、TEST expiry seq59 645948us、普通 seq4 308486us。645948us 属 seq59，
不能填成 later seq87 的完成时间；日志和 epoch maximum 的原身份必须保留。

剩余三类假说仍不能由 036 tick 证据判定：Capture Ready 但未获调度；Capture 阻塞在实际
主 ring mutex；Capture 执行同步 DSP/复制/内存路径并可能交替被抢占。没有 SDK 锁 owner /
真实阻塞时刻证据就不能指定根因。WSS 默认更高优先级或 WiFi/timer core0 只是竞争条件；
四个 gap 的各自 WSS-ready 之前恢复也不能排除其他后台活动。SDK 报告 seal SHA-256
`5fd9b1a5417b47b9fd8b4c409f2e0d0b7cae11deceb93fb6b7a46c3573461369` 保留该只读分析范围。

### 目标对象、最终 TEST linked 与成本

隔离对象独审 SHA-256
`090c4bdaebe074f8a7a506d73908968a33fc68830707162fc164bb8390739e3f`；最终 TEST linked
独审 `sdk-probe-review/linked-independent-test-v1/review.json` SHA-256
`cd8fe31d3cc185c459b65e739fe9d6d8b88c504bd8aa5d61d0596530f4612a22`，状态
`PASS_FINAL_LINKED_PROBE_STATIC_NOT_HARDWARE`。最终普通 OFF linked 独审 SHA-256
`7cd59716671e694b92f4c313f05bdfe0d1bcb4e550981f5a5aef2bcb70505eb2`；签包与双包共同引用
按下一节独立封存，不以隔离对象代替最终制品。

| 项目 | 已核结论与边界 |
| --- | --- |
| frontend ABI | 640B，TEST/OFF 相同；不新增条件成员或任务 |
| 两模块静态存储 | feed 144B + tick 320B = 464B；不等于全部链接增长或净运行 heap 减少 |
| 普通 OFF 隔离及最终制品 | 隔离对象 ALLOC sections / relocations / undefined symbols 与 035/036 一致；最终双 observer TU、链接片段、状态和日志缺席，getter 恢复 flash；AEC 保持开启 |
| 最终 getter | `xTaskGetCurrentTaskHandle` 位于 IRAM `0x4038d468`，31B、frame 32B；读 DRAM `pxCurrentTCBs`，恢复经 ROM `0x40001c38`；无 priority getter / xKernelLock 路径 |
| TEST 函数 frame | Capture 432B，Fetch 528B |
| Fetch 局部并存链 | Entry32 + Fetch528 + TickBoundary304 + Log208 = 1072B |
| Capture 局部并存链 | Entry32 + Capture432 + FeedLog272 = 736B |
| 所审 ISR 分支并存链 | Hook32 + Tick64 + Feed48 + Try32 + CAS32 = 208B |
| 应用 ISR 函数和 literal | 函数合计 1307B、literal 16B；code span 1313B 含 6B padding；getter 另 31B，不能混成单一净增长 |

局部 frame 链只加同时存在的 frame，不将先后调用相加。这些数值尚未计入完整 logger / SDK /
ROM / context / 寄存器窗口 spill / ISR 嵌套与实际任务或 ISR HWM，不能宣称安全余量。
新探针 CFG 无重试/回边，既有 systimer 硬件 ready / 高低位一致性 polling 仍存在，不能称
整个 SDK 链恒时或无循环。预算到期与日志耗尽仍保留部分 ISR/前景前置成本。

### 离线 parser 与辅助脚本

最终 parser SHA `c566c43b1f70eb1551ad4005fdc0aecd0626606f363581ed3c9dfe28c5ed7358`，
46 项自测及独立 23 个 closed/raw receive-time 夹具分别通过。新版 seal SHA
`b24f5e92d2aac3f3e6450b3204233478fbeaa3a0dc4620f5449f2665b45f3b1e`。按确切 feed
配对、拒绝 group 矛盾、未知 TEST 前缀与无 flag 饱和；旧 35 项原件保留。

parser 锁定复用 helpers，核字节/行/chunk、RX monotonic 顺序及 partial accounting；
不独立证明 clock/clockInfo 来源或 closed≥lastRX，真实窗口须由 root 另核。辅助脚本
独审 SHA `c040f146a758284518232c1fcddfd900024aacff3c99dd68704e97ee46d2fb3c` 只做
精确 derivation、AST/语法与 FakePort 单写/无重试，所有 helper main/IPC/CDP/串口/硬件
调用为 0。这个软件证据不能替代真实闭合捕获。

### 两份制品与静态成本

| 制品 | Main 字节 / SHA-256 | ELF SHA-256 | ZIP SHA-256 |
| --- | --- | --- | --- |
| TEST `20261008-200607` | 7,168,960 / `5797ec832efa343419dfdc57de8c80447aebb36bd7d395162796566c8e229487` | `81902c01efd546a1e63705a9b19d7a6a3cc56f3ef2f1d1d9bbff69c60b1dde63` | `e73599408eb499d5eec8c550782fbf06fc74e0d26daa3c9b8c6c705b6d065957` |
| 普通 OFF `20261008-202609` | 7,154,016 / `3f884068ccb37500d801be82c96649431fa150505c241a1a312e6da3fc44dd2c` | `1ccecde85090fe99b7668d741bbb9e9c7226b0a28bea9c6a5d188b95b09be8a3` | `54a29137818f953e864fe22f54e2eb6905200a992c7884655b8f54889937aa90` |

TEST / OFF 包审 SHA-256 分别为
`67a70352a17b913569a1219a71cff0859c4d31e8526b5f9ea7da0d828016a5d3` /
`50286fb8d140940deeb9022181539a07b42020da0eb0277450612b8c39b11816`，seal 分别为
`2341d38bd5c4cdc0989e24b8398acfdcaeb4eba6f4a7b8dc089e4405a593c036` /
`8ccf6dd9fe6e892216764b74f930409ebe3d18bf632f471ae6c00788e0c6de05`。双包共同引用独审
`7028f16dbaed9777625a4af7f00432f8cf90ebddcdbe4e38c4a94a94596bc9bb` 复核 125 / 120
份冻结文件，确认相同源码、软件 seal、SDK、签名、公钥、immutable Recovery / bootloader /
partition、authority v3 与 ZIP；development-signed production flavor 不等于生产根部署。

TEST 相对 035 普通 OFF 的 `.iram0.text` +1364B、`.dram0.data` +464B、`.dram0.bss`
+64B、`_heap_start` +1808B。64B 是既有 TEST flavor 差，464B 才是两个 observer 的静态
存储；这些布局值不表示运行时净 heap 收益。普通 OFF 的已核内部 DRAM、语音实例及 heap
边界与 035 差为 0，device AEC 仍开启。两个包的部署、闭合设备观察及恢复证据另见下节。

### 闭合设备窗口、自然 TTL 与全部 gap

最终独立硬件报告 `hardware-independent-review/review.json` SHA-256
`49af0719489ccc1aa43b4e66633cd1739bbcf352ad865c14dc16bd64ce1fb31a`，结论为
`PASS_BOUNDED_037_TEST_OBSERVATION_AND_ORDINARY_RESTORATION_REVIEW_ROOT_CAUSE_UNRESOLVED`。
TEST 81740B / 普通 OFF 22867B 原始字节均已闭合，8 + 4 条显式命令逐条得到成功回执，
没有并发串口 owner、自动重试或自动 cleanup。root 单独核对固定 capture helper 的 QPC
来源、lifecycle metadata 及 `opened <= firstRX <= lastRX <= closed`，独审再核字节与时序；
不能把这些额外检查归给离线 parser 本身。

两次 guarded 保 NVS 刷写与启动分别核得 TEST ELF 前缀 `81902c01e`、普通 `1ccecde85`，
由独立包审绑定完整镜像。原 MAC `44:1b:f6:c3:b4:30`、ID
`c78845a8-06c9-4dcd-b7ff-d33e599f23ff`、bound/token4、authority v3、immutable 资产和
开发签名根保留。

| 窗口 / 本 boot generation | WSS session | 服务端首完整 Opus 帧 | 实际 RX 覆盖 |
| --- | --- | --- | --- |
| TEST fresh / 3 | `07ca71dd-394e-4ea4-8207-52e8455708ba` | 18B，12:33:31.857Z | Interaction stopped 至下一显式 TX 前末次 RX 563.9286487s；rearm 后 562.9416653s |
| TEST 自然到期后新 wake / 7 | `75ab4d55-7f3c-421f-939b-78584996369e` | 18B，12:43:37.593Z | stop 后 91.52203s；rearm 后 90.7349372s |
| 普通 OFF 独立 wake / 3 | `9028edb6-29fc-4486-aa04-813eebde168e` | 18B，12:49:24.192Z | cold 首 TX 前 84.1739743s；stop 后 105.0847866s；rearm 后 104.6374262s |

各行 stop/rearm 覆盖相互重叠，不相加；只计实际接收字节，不计脚本等待或末尾无数据时间。
三个 session 均各自 ready、首帧、显式 stop 和 Wake rearm；18B 是完整 Opus 传输帧，
不是 PCM 帧完整性或声学证明，也不是单 session 六轮验收。

自然 TTL600 资格以已确认 MQTT 在线快照后的首个实际 RX 作保守锚：资格末次 RX 的年龄为
634.4468384s，其中 Interaction stopped 后 idle 为 555.583672s；实际 expiry wake TX
时年龄为 644.3784618s、idle 为 565.5152954s。中间未捕获 refresh/reset，之后新 wake
确有 refresh、enrollment 和 WSS ready。初始 enrollment 在采集前，年龄含 fresh session，
不能写成精确签发时刻、连续 idle600 或一个 WSS 持续 600 秒。

| gap | W wait → recovered elapsed（us） | W producer | 完成归属与限制 |
| --- | --- | --- | --- |
| TEST gen7 / gap1 warmup | 668215 → 709076 | current `api_boundary` seq3，age618870 | 精确 seq3 / ticket1213，API639598us；strict target1/other63 |
| TEST gen7 / gap2 running | 109789 → 160092 | current `api_boundary` seq13，age1231 | open 为 stale、ticket0/target0，未捕获精确 complete；不能借 earlier seq4 |
| 普通 OFF gen3 / gap1 warmup | 691220 → 708852 | current `api_boundary` seq3，age646130 | close epoch maximum 明确属于 seq3 / API649509us；普通无 feed profile，不能派生 target/other counts |

三次 gap 都在各自 WSS ready 之前 recovered；TEST gap1 也早于该次 refresh enrollment
完成与 Assistant I/O 创建。TEST fresh 的 `stalls=0` 只属于该窗口；普通 OFF 仍复现 gap，
不是探针修复的证据。两份 closed 串口分别保留 2 / 1 条 W，均为上表 gap，未捕获 E 或
reset/panic/abort；服务端在每次显式 stop 后的断开 warning 仍保留，不写成全环境无警告。

### 逐 feed 端点带来的信息与覆盖限制

TEST gen7 seq3 的 API marker/return 为 `715338919 / 715978517us`，完成包络 639598us；
被接受端点 target1、other63。seq4 为 `715999211 / 716680358us`，681147us，
target1、other67。它们只否定整个已观测包络始终选择 target 的强假说，不能区分 Ready
未获调度与实际 Blocked，更不能识别某个任务为根因或将 counts 乘 10ms。

`other` 计数聚合所有非 target；seq3 open / complete 的末次 other 句柄分别为
1070350552 / 1070370404，不能把 63 次端点归给同一个其他任务。gap2 起点为
716769134us，seq4 return 比它早 **88776us**，两区间不重叠；W2 的 681147us epoch
maximum 不能当作 seq13 或第二 gap 的耗时。seq13 缺确切完成记录，维持未知。

gen3 / gen7 各捕获 8 条 feed 日志及 1 条 summary，suppressed 分别为 1107 / 637，
active slot 与 sampling ticket 最终均为 0。固定 deadline 分别为 130850964 /
735243013us；预算、日志筛选及日志竞争的未知损失不等于完整逐 feed 覆盖。两个 flow_stop
tick 摘要都已 expired；末次已发布采样到 freeze 的未覆盖尾段约为 17.79s / 10.21s。

六个 TEST tick 窗口各有双核记录，但只是 getter 包络和已发布 interval。尤其 gap1 的
recovered token3 在对应 AFE recovered marker 后 **667968us** 才开始冻结，包含后来的
seq4 活动，不能当作 gap1 的原子联合快照。prefix/crossing、零已发布 drops 背后的私有丢样
未知、deadline 尾段及采样空洞均保留；普通 observer 缺席由编译/符号/包审支持，不靠串口
静默作证明。

### 普通恢复与剩余门禁

设备已恢复普通 OFF `20261008-202609`。12:52:02.375Z 设备快照保持原 ID/MAC、
bound/tokenVersion4、MQTT=true、voice=false、sessions=[]；12:52:02.940Z UI 确认预览
停止、串口 disconnected；12:52:03.1749209Z 独立进程快照确认 serial owners=[]。这些
采样时刻分别记录，不伪装成同一原子终态。桌面运行仍按既有 026
`d28370c82ee8818cb47924ccc5d091a3164e5761` 识别，文档 HEAD 不替代可执行文件身份。

本轮是有限 USB 合成输入及恢复观察，根因仍未确定。继续区分 Ready 未获调度、实际 SDK /
ring mutex 等待与同步 DSP/复制/内存工作混合抢占；若需追加证据，应在已识别路径与确切
feed 身份下选择最小探针，不以调高优先级、换核、放宽 100ms 或关闭 AEC 充当根因结论。
完整物理资源归还、实际 heap/stack/ISR 余量、声学、更多取消/并发、任意 OOM、长稳、生产
信任根与 power-cut 门禁仍开放，资源与生产 **NO_GO**。031—036 历史保留原身份，不用
不同 boot 的最低值相减推算收益。

## 2026-10-08 Snapshot-before-log software correction (038)

源码：[`6c807b87d164c38794b48a97b1632c8c1788ee4c`](https://github.com/rymcu/rodakos/commit/6c807b87d164c38794b48a97b1632c8c1788ee4c)。仅调整
`RODAKOS_RELEASE_TESTS` 中既有观察的顺序：stall 先冻结 tick 与 open feed，再输出
tick/feed/W；recovered、fetch 前/返回后 cancelled 与 resync 先冻结 tick，再输出
closure 日志。冻结仍在业务 mutex 释放之后；Capture 的 SDK 前 arm、返回后 credit
锁等待前 close 不动。原 token/身份失效语义、固定 20s、8 条 feed 加 1 条 summary、
100ms、TTL600、任务优先级/核绑定、缓冲与 AEC 合同保留。停止处在原位置直接调用
Freeze/Log，避免再经 helper 嵌套一份快照；flow-stop 采时阶段、Retire 与 summary
顺序不变，最终栈成本以下述目标审查为准。

完整 TEST 前端 13 项、普通 OFF 前端 49 项分别通过 Debug 与 ASan/UBSan/leak。
六个新增日志门闩用例覆盖 open、recovered、日志跨 deadline、两类取消和 resync；
037 完整旧 TU 只编译一次，再分别运行六个用例，均以指定 marker/assertion 和退出码 1
拒绝。最终源码 v2 只改变被负控固定旧 TU 替换的当前前端文件，其余 69 个输入及
测试/runner/旧 TU 不变，独审确认复用原六项负控，不重复执行。日志阻塞期间 producer
仍能发布 credit，已冻结结果不会借用后续 feed 或 token。
host 替身不验证芯片调度、DSP、声学或物理资源；OFF 的 TEST-block 消去文本一致也不
替代最终目标对象核验。

| 本轮目标产物 | 身份与审查 |
| --- | --- |
| TEST 主构建 | Main 7,168,992 B，SHA-256 `579324a70f015452b275d3d0b216ba0ae4967e311e1d2ef79751bff22981f357`；ELF `f5a92e3219a3d71a7dd6733088f0dfb4b055f1d04c33167575fe6f19c66a7d6a`；清单 `c6e20f1f01bd2ecac83bc6f260f4956d9c37669cd378c8b28067fb82a6c6b8f4` |
| 普通 OFF 主构建 | Main 7,154,016 B，SHA-256 `65d77c37ee76e9e29d2a24f61f7713b05c7df247daf99f83a5479c9e8b62726c`；ELF `80c82d0da185ec28a747c4df3bfe582c0be3823800f949d1787bb3d6fdf4953d`；清单 `3e7ceab0b74212c48dd0ad6ca1152c0e437f43becd75cff9b65a498813680bba` |
| 新 Fetch/Capture 帧与局部调用链 | 最终对象/ELF独审 `final-target-v2-review.json`，SHA-256 `76d2737f1829a1885c5e78f454cb8f8019c131847ac2193dea12e5b9cd9e8b8a`；仅本地构建静态边界通过，非签包/硬件验收 |

最终 ELF 核验 TEST Fetch 固定帧为 784 B（037 为 528 B），Capture 仍为 432 B；
最大已核局部调用链为 1088 B，比 037 的旧局部链 1072 B 多 16 B。它不是完整
栈上界或实测 HWM，不能据此推算余量。两个 observer 的状态仍为 464 B；所审
ISR/getter 对象与指令字节保持既有边界。普通 OFF 的 AFE allocated sections 与
relocations 等于 037，observer/链接片段缺席，getter 位于 flash，内部 DRAM 增量
为 0；Capture/Fetch 为 240/368 B。上述构建不是签名包或设备部署证据。

038 的定点静态排查约束 scheduler、mutex/日志与 SDK 等待分支的候选解释，未把 opaque
other 端点映射到具体任务，也未识别 037 三个 gap 的统一根因。观察顺序修正只去除
明确的日志先行间隔，不能把 event/freeze 两个时间包络合并为原子事件。
本地证据目录为 `D:/workspace/rodak/.codex-temp/voice-wait-038/`：

| 证据 | 报告 | SHA-256 |
| --- | --- | --- |
| 最终源码 Debug | `snapshot-debug-v2/result.json` | `d105b266c6584cb1225093da92820ceabd9b6c89835f252f064da067cc2b714f` |
| 最终源码 ASan/UBSan/leak | `snapshot-asan-v2/result.json` | `a47d1e01841ce614d408b74e4d0277daf0b98c7a4286576f302adf97cdd7978b` |
| 最终源码与 host 独审 | `sdk-snapshot-independent-review-v2.json` | `b08370972e83c6f10d3fc4528792ddc9fd9c8752d31bbf8c34aed9e5ae451488` |
| SDK 等待链静态分析 | `sdk-blocking-analysis-v2.md` | `620cdaf6829a964a3cdf9b1d5164d4204112868d4b9ed78bb25aac1fa40be5f4` |
| SDK 期号归属勘误 | `sdk-blocking-corrigendum-v2.json` | `10523ee9687ce9d094aebb7961ac56c35deb1255796cd8e636ae7654758d526c` |
| 调度与优先级边界 | `scheduler-review.json` | `fcca8e2b5fdce9dbac2b34d7db438d8e7499f220397b0bd985eae1581bb5ed6f` |
| 同步日志与观察窗口限制 | `interference-review.json` | `e1dea033e44562ac7c9be1cbc3d9c143d29cd6e371e0b841c368027b917b5dc7` |

**本轮没有签包、硬件、串口、刷写或复位操作。** 设备固件记录仍为 037 普通包
`20261008-202609`、源码 `d852d9fdb6b16a0a34a49eb7555ddd5a9b944396`；原绑定/token4、
MQTT/Wake 空闲及桌面 UI/串口关闭终态沿用 037 的分时记录，未在 038 重新测量。
桌面可执行文件/PID也未本轮重新核验。037 的三个 recovered gap、完整软件/包/硬件
证据逐字保留；根因仍 **INCONCLUSIVE**，资源与生产 **NO_GO**。物理回收、实际
heap/stack/ISR 余量、声学、并发/OOM、长稳、生产信任根和 power-cut 门禁不变。

## 2026-10-09 PC-status causal comparison (039)

本轮跨本地 2026-10-08/09，在同一 boot、同一桌面 PID 2140 上完成一次
normal→quiet→normal 对照。**目标 PC 消息流不是这格 stall 的必要条件**；
这个结论不排除该流的贡献、其他 MQTT/TLS、同步日志或 PI，也不确定统一根因。
三格会话并非严格等时，不能用本次单组三格估计统计效应或宣称性能改善/恶化。

固件继续使用普通 037 包 `20261008-202609`，源码
`d852d9fdb6b16a0a34a49eb7555ddd5a9b944396`，Main SHA-256
`3f884068ccb37500d801be82c96649431fa150505c241a1a312e6da3fc44dd2c`。
039 未修改固件、未重新构建/签包/刷写，没有擦除、复位或主动 NVS 操作；原绑定、
tokenVersion4、authority 保留。自然 enrollment 可按既有流程持久化刷新凭据，
未读回比较 NVS 全字节。038 的 TEST 快照修正未部署，不能套用其观察顺序或新栈帧。

### 三格与凭据年龄

| 正式窗口 | generation | wake 前保守凭据年龄 | recovered gap（μs） |
| --- | --- | --- | --- |
| normal 1 | 11 | 1117.444729 秒 | 660319 / 741333 |
| quiet | 15 | 700.4774101 秒 | 1302552 / 660511 |
| normal 2 | 19 | 673.5795886 秒 | 1325188 / 639333 |

prime generation 7 的两个 recovered gap 649983/648692 μs 仅作准备，不算正式 normal。
四个阶段共 16 条命令均按上一成功 ACK 后再发下一条执行，没有命令重试；各正式格
为 audio_begin、单块 512 B 零 PCM、wake、stop。每格采用最新 enrollment 完整行 RX
作为凭据签发时点的保守上界；实际 wake 年龄均超过 630 秒，没有用本机等待替代 RX。
Q 的 651.2946752 秒和 N2 的 653.5380768 秒只是更早资格快照，不是实际 wake 年龄。

Q 的同域资格端点距原目标 forward 排空 19.3821289 秒，已经抑制 6 次。整个 quiet
窗口 110.1013822 秒内目标 suppressed=36、forwarded=0、contamination=0，目标
publish throw/callback error 为 0；Q begin→rearm 的 PC-status received 为 0。
其他 MQTT 发布仍继续。资格端点 publisher 的采样 inFlight=true；这里核的是
gate 原目标 forward inFlight=0，不能称全系统 pending=0。相同 wrapper 在独立
120 秒截止前约 9.9 秒显式恢复 normal，后续证明核到 5 次 forward/completion 及设备 PC 行。
串口 QPC、broker performance 时间与 IPC 时间是不同观察域，不能拼成原子时刻。

### gap 身份与候选机制边界

N1 首 feed API 为 628257 μs；第二 gap 的 producer 为 between_reads，epoch 最大
API seq4=653749 μs 不能借给该 gap 缺失的 feed 身份。Q 首 gap 1302552 μs、首 API
seq1=630028 μs 与 closure between_reads/read_seq5121 是不同端点，不相加也不归到
单次 API；第二 gap 的 stall seq32 与 API 最大值 seq32=640881 μs 身份一致，机制仍未知。
N2 首 gap 1325188 μs 时，首 feed API 仅 14154 μs；read 最大值641057 μs/seq7593
和后续 API 最大值647006 μs/seq3 都是独立观察。第二 gap 的 read_returned
seq7603/previous526118 μs 不能借用先前 seq7593 或 API seq3 的 epoch 最大值解释。

静态分析确认 open_mutex_ 与 refresh_mutex_ 跨同步 HTTPS，可能使短锁竞争触发的
继承优先级保留到外层锁释放；本轮没有观测实际继承、waiter 或锁归属。
PC-status float 日志在应用 mqtt_worker base4，不能归给 SDK mqtt_task base5；
有效优先级也不等于连续 CPU。固定 100ms、TTL600、优先级、核绑定、缓冲与 AEC 合同保持。

### 有限采集与现场收尾

每格均有成功 stop ACK、关联 WSS Stop/Destroy ESP_OK、Wake rearm 与空闲 IPC；
这是串口语音诊断，不是视频 sessionId/startCommandNo 合同，也不证明完整物理资源释放。
N2 stop ACK QPC 为 261781794454600；在其后实际 RX 已达 82.1026418 秒时才发唯一
final-restore。此前一次本地 PowerShell 空属性计数检查未通过且未追加命令，不是设备
失败或命令重试。独审核得各 stop ACK 后、到下一阶段 TX 前或采集结束前最后实际 RX
的端点跨度：N1 643.3635989 秒、Q 603.9823945 秒、N2 145.9083073 秒。它们有原始行
支撑，不表示连续 idle 或每时刻都有数据；N2 rearm 后到最终 RX 为144.1341993秒，
stop ACK 之后实际记录800个RX chunk/12726 B。

controller 于 2026-10-08T16:11:14.547Z 正常退出，原 publish 已恢复；清理验证 holder
移除并关闭自己的 inspector socket。16:11:51.774Z 操作系统监听证明临时 9229 已关闭，
原 PID2140/启动时间及 MQTT 8883 保持。16:11:52.489Z IPC 保持原 bound/tokenVersion4、
MQTT=true、voice=false、sessions=[]。COM3 capture 已正常关闭：16/16 命令、raw
227633 B、elapsed 2755.9107324 秒、trailing0、portClosed=true，writeErrors=[]、
error=null；全量3068完整行/13422 RX chunk/16唯一ACK及8个recovered gap已核，
完整采集中未匹配 reset/fault/pause 标记，结构检查 issues={}。
这些是有界采集和软件终态，不是物理回收验收。

16:12:17.230Z UI 串口 disconnected、无 Camera preview、stopButtons0；
16:12:45.825Z 已知 capture/monitor/controller 进程模式匹配为空，不是系统级 handle 扫描。
桌面 EXE SHA-256 `8fad4b1641ee195f423fc32bb8f4dafbea3b697e8e9c85a600bbd42d22ce179a`，
bundle SHA-256 `acdf8859141f53c4eac04a2651206f823cb8c324ab2f01efb11d4ac7782c106f` 与原记录一致。

### 本地证据

临时 gate 的 22 项纯 host 检查和 controller 的 4 项隔离 VM smoke 已通过；控制器
独审绑定实际执行输入；最终控制审核103次续租、最大间隔15.2668499秒，
目标请求515=转发479+抑制36，错误0。最终硬件与控制两份独审通过各自有界闭合，
不授予物理资源或生产验收。没有查询、依赖、修复或重跑 GitHub Actions。
完整本地证据位于 `D:/workspace/rodak/.codex-temp/voice-wait-039/`：

| 证据 | 报告 / 文件 | SHA-256 |
| --- | --- | --- |
| 串口完整原始字节 | `windows/pc-status-triplet/serial.raw` | `1fb910444ee12fc4d8187406a2a67d5de2e2d513867edd00a937b6aa02f3e046` |
| 控制器执行前独审 | `controller-independent-review.json` | `14f7b4f937f972a150d95cb2c17688ebecacafe3acc11d212e8e6d8289d54fc5` |
| PI 长持锁静态分析 | `pi-hold-review.json` | `bc67f14d7e5817bda0efcfc5b61c2997931db62a085b3f2e20aef84bee5574f8` |
| 最终硬件闭合独审 | `hardware-independent-final-review.json` | `79ed1081b7444751155fe0e18aff5a84f8701d09625fd828f2d0602d958fa32b` |
| 最终控制暴露与恢复独审 | `control-exposure-final-review.json` | `fbd6110d6552246b01b19ec739a279cbb2e287f90cb381fd61a93727371d878e` |

USB 合成输入不等于真实收音、扬声器或声学。根因 **INCONCLUSIVE**，资源与生产
**NO_GO**；真实 heap/stack/ISR 余量、物理回收、并发/OOM、声学、多轮、长稳、生产根
和真实 power-cut 门禁保持，031—038 历史证据不改写成本轮结果。

## 2026-10-09 准备阶段优先级观察与 TEST/OFF 恢复 (040)

040 源码提交为 `d2de3d3392d2f7f21e355911746dbf49bb2f27fb`。TEST Main 为
`7,171,568 B`、SHA-256 `29db69e465568c681a5dc59d006c8e1680b5f2c4aa84431535972deec5617651`，
ELF SHA-256 `8491abf49707cbcfed0fc532ea437226242bae918681df3c4853b02ec17f978c`；普通 OFF
Main 为 `7,154,016 B`、SHA-256 `d24a2e550de32387fd2672f1a6eff5ad928fe8bdec3f2535ce72000573d7ba3b`，
ELF SHA-256 `61bc0d0abbb77969b30cf9b99432161c2032bfaf46eec60fa99b0cf06aa8f307`。
TEST/OFF 均为同一提交的本地构建，未查询、依赖、修复或重跑 GitHub Actions。

TEST 专属观察器只采当前任务的四个端点：`prepare_begin`、`open_acquired`、
`cloud_returned`、`open_released`；单槽完成后由
`RODAK_RELEASE_TEST_V1 voice_prepare_take` 一次性消费。目标静态独审状态为
`PASS_TARGET_COST_STATIC_ONLY`：新增 prepare observer DRAM 200 B，旧 tick/feed 为
320/144 B；函数入口帧是单函数值，不是累计或峰值栈。真实 transport host 9 条返回路径、
真实 Cloud 单次调用、锁持有 `false/true/true/false` 和 OFF 缺席均通过；过期凭据成功刷新、
真实 PI、资源和生产验收不在覆盖范围内。

TEST 包 `20261009-014715` 和普通包 `20261009-014905` 均通过官方签名验包、037 immutable
Recovery 锚定、分区偏移、16 MiB merged、FF padding、ZIP 与目标 collection 绑定。TEST
随后以增量方式刷写 COM3，仅写 `otadata` 和 `ota_0`；恢复时同样刷回普通 OFF 包，未擦除
NVS、Recovery 或 OTA journal。设备 `44:1b:f6:c3:b4:30` 的一次真实 USB wake 记录了完整
四点：scope=1、同一任务 handle、优先级 `4/4/4/4`、`flags=0`，串口 raw `16,608 B`、
无丢行、无尾部残行。TEST 期间 WSS 因 `required_internal_stack=6144` 而启动失败，不能
将端点快照解释为优先级继承或声学/资源通过。恢复后设备仍 `bound`、`tokenVersion=4`、
MQTT 在线、语音连接关闭，普通 OFF boot confirmation 成功。

040 本地证据位于 `D:/workspace/rodak/.codex-temp/voice-wait-040/`，其中目标叙述
`final-target040-review.json` SHA-256 为
`e27442e10cc32913c769dc83980d85f32ecdbb18d1781d78f0a2ac92a362ac0b`，TEST 快照的
`lifecycle.json` 使用采集器 SHA-256
`e2bbe7a7dfc409964de67c8152a1d70d5b0f136aa862feb991d1f8e707b70586`。根因仍
**INCONCLUSIVE**，资源与生产继续 **NO_GO**。

## 2026-10-09 六轮同 session 合成语音观察

本轮没有刷写或复位，沿用 040 恢复后的普通 OFF 包 `20261009-014905`。通过新增的
`tools/run_serial_voice_test.py --turns 6` 在 COM3 注入同一段 16 kHz PCM：首轮执行一次
`audio_begin`、`audio_chunk` 与 `wake`，后五轮只在设备报告 follow-up listening 后执行
`audio_replay`，第六轮播放统计收到后才发送 `stop`，最后执行 `audio_clear`。

设备串口记录了一个完整的 realtime voice session：session ID 为
`5d18440a-a694-4341-bb70-4bfed30127b3`；`Sent speech input start` 六次均使用该 ID，
`Follow-up listening started` 的 `completed_turns` 依次为 1—5，六次
`Playback audio stats` 均有非零 packets/decoded_frames/pcm_bytes 且 `write_failures=0`。
窗口内无 reset、panic、watchdog、内存失败或 transport failure；stop 后出现
`Interaction stopped`、Voice websocket cleanup 完成和 wake monitoring rearm。原始串口与
工具摘要保存在 `D:/workspace/rodakos/.codex-temp/voice-six-turn-20261009/`。
`serial.log` SHA-256 为 `01222b60275392fb3f6708f7b4e3637d6bf4f35ec0d01476d8d23d88801cebd6`，
`serial.summary.json` SHA-256 为 `d756e4a0d8cbdeb212e6bf85543225b63fc3be091903aad3baef1f2103e052e5`；
本地 IPC 事件快照 `server-events-cdp.json` SHA-256 为
`9f5977c4f9023e2b270a7894ae4734c07d5f69e28c8a6549072feee2234fa1e6`。

通过本地 Electron 的 `window.api.server.listEvents(4000)` 复核同一时间窗，得到一次
`session.open` 响应、一次 `wake.detected`、六次 `input.start`、最终 `input.stop`、两条
断开事件以及 `window.api.agentRuntime.listSessions()` 为空。服务端的实时 VAD 会在一个
逻辑 turn 内切分音频，因此同一 session 下保存了六个 `reason=vad-end` 片段和一个很短的
最终 `reason=listen-stop` 片段；这不是七轮对话，也不是六次显式 `input.stop`。该边界
证明了同 session 的设备生命周期与播放恢复；当时缺少服务端尾段终态，后来由下方
[尾段复核](#2026-10-09-六轮合成语音尾段归属复核)补齐。仍不能扩大为真实收音、声学
barge-in、音乐/Recorder 抢占、长稳或生产发布通过。

本轮只使用本地串口、Electron IPC 和 SQLite 只读取证，没有查询、依赖、修复或重跑
GitHub Actions；资源与生产 **NO_GO** 保持。

## 2026-10-09 Follow-up silence 与超时观察

沿用同一普通 OFF 固件和 16 kHz PCM，在 COM3 使用 `--cycles 1 --late-follow-up` 完成一轮
延迟 follow-up：首轮回复进入 follow-up listening 后静默约 28 秒，再 replay 一轮输入；
第二轮回复完成后未再注入音频，设备报告 follow-up window timed out，随后 stop、清理并重新
布防。设备 session ID 为 `d0076cae-d957-4ca1-a5be-b4d9ce2f811c`，串口摘要确认一个
session、无 reset/panic/watchdog/transport failure，两个回复的播放统计均为非零且
`write_failures=0`。

Rodak IPC 事件窗口确认一次 `session.open`、一次 `wake.detected`、三次 `input.start`、
一次最终 `input.stop`、断开事件和空 runtime session。服务端保存了两个 `vad-end` 片段和
一个最终 `listen-stop` 尾段；这是同一 session 的 VAD 分段边界，不代表额外对话轮次。原始
串口、摘要和 IPC 快照位于 `D:/workspace/rodakos/.codex-temp/voice-late-follow-up-20261009/`：
`serial.log` SHA-256 为 `228d2921856e3f64a5b1718c2b62dac1bf8f345210d0e3b9f0b40d1ec935e6f8`，
`serial.summary.json` SHA-256 为 `8099decd31f9f3b3ff6661d152baa1943d5ba7ff3dcb76962433a8a14f457c3e`，
`server-events-cdp.json` SHA-256 为 `10e7697da1b44f23be92504bde64de9234754e386ce81f04c00287b36bd09151`。

该结果只关闭 follow-up silence/timeout 的有界合成门禁；音乐恢复、Recorder 抢占、重复唤醒
抑制、TTS 尾音、AEC/barge-in、真实收音与长稳仍开放，资源与生产 **NO_GO** 不变。

## 2026-10-09 播放期间恢复实体麦克风的有界观察

普通 OFF 上使用 `--cycles 1 --live-mic-playback` 完成一轮 USB 合成输入。设备在回复播放
期间恢复实体麦克风，仍保持同一 realtime voice session；串口未出现 VAD interruption、
reset、panic、watchdog 或 transport failure，播放统计为非零且 `write_failures=0`，随后
stop、cleanup、断开和 wake rearm 完成。Rodak IPC 复核为一次 `session.open`、一次
`wake.detected`、两次 `input.start`、最终 `input.stop`、空 runtime session。

原始串口与 IPC 快照位于 `D:/workspace/rodakos/.codex-temp/voice-live-mic-20261009/`：
`serial.log` SHA-256 为 `7dc91553e45c3fb9d6caec7b0dc2839451b437332bb417b25535d436bda51389`，
`serial.summary.json` SHA-256 为 `33475ce414bb37adefdd3dee8fc7d1d3c4f7340719a8789d87eceb1e928241fe`，
`server-events-cdp.json` SHA-256 为 `7b92404957023fc04c929161bc389cbeb408866434ef2d4537bf76ebd5a4c0eb`。

该结果只证明播放期间麦克风路径能够恢复并完成有界清理，不证明回声消除、真人 barge-in、
音乐或 Recorder 抢占、误接受/误拒绝、声学质量或长稳；资源与生产 **NO_GO** 保持。

## 2026-10-09 合成 barge-in 与播放中断观察

普通 OFF 上使用 `--cycles 1 --barge-in --interruptions 1` 在播放期间注入一轮打断音频。
设备串口记录 `TTS interrupted`、AFE VAD confirmation 和 VAD end 各一次，播放统计非零、
`write_failures=0`，无 reset/panic/watchdog/transport failure；随后 stop、cleanup、断开和
wake rearm 完成。Rodak IPC 复核到同一 session 的一次 `playback.abort`，payload 的 reason
为 `vad_detected`，最终 runtime sessions 为空。

证据位于 `D:/workspace/rodakos/.codex-temp/voice-barge-in-20261009/`：`serial.log` SHA-256
为 `ee50fa076bf1a8d02a2dcd6549a2842e5f6b89ea25ef7cdfd17340ea6857ca87`，`serial.summary.json`
SHA-256 为 `7dc52aad09972826a6b534c16a7c094a3a72a98b65a6d17f1c3d42f2842d9bce`，
`server-events-cdp.json` SHA-256 为 `3f15bcbec33af67e03c7f69f30d03781fe2edc018cc08430185f7f7b786151af`。

该结果关闭的是合成播放中断路径的有界观察；真人收音、AEC 回声场景、误接受/误拒绝、
音乐/Recorder 共存和长稳仍未验收，资源与生产 **NO_GO** 不变。

## 2026-10-09 资源采集器修复与 30 分钟有限观察

普通 OFF 包 `20261009-014905` 在 COM3 上完成了 1,800.20 秒串口观察。原始日志包含 60 个
严格递增的 MQTT health 样本、5 次 Home/Photos/Camera/Home/Music 应用启动，5/5 ACK 和
5/5 completion；`--duration 1800` 本身低于正式 28,800 秒门槛，因此状态只能是
`incomplete`，不能写成八小时通过。原始 `serial.log` SHA-256 为
`fe1fbb7a9d1017b0664ef71278a499d653016509a9eb5d903617684157f5f714`。

本轮同时修复 `tools/capture_release_stability.py`：它现在保留 MQTT、Main、Voice、应用局部
资源样本及 warning/error 原始行，按请求→ACK→completion 检查应用事件顺序，并将不属于
MQTT 的水位纳入摘要。对上述原始日志离线重解析得到：Voice `internal_min=275` B，应用局部
`internal_largest=3584` B，MQTT 最低 `internal_free=20275` B、`psram_free=1715724` B，
3 条 warning 和 1 条 `E:RX:153600-88320`。没有捕获 reset、panic 或 watchdog；这些资源低水位、
错误行和未达到时长的事实共同保持资源/生产 **NO_GO**。`Music app created` 及 SD 扫描 5 首只
证明应用启动，不证明播放、暂停/恢复或声学共存。

新增回归测试覆盖 Voice/Main 水位、应用事件乱序、非致命 warning 与 `E:RX` 分类；本地
Python 测试和离线解析均通过。该门禁不依赖或等待 GitHub Actions。

## 2026-10-09 Music/Recorder 与音频焦点 host 前置

在不打开 COM3、不刷写固件的条件下，本地 Debug CMake/CTest 完成 Music UI **18/18**、
RecordingService **17/17** 和 voice-volume/focus **30/30**。Music 新增用例验证异步
`RequestLibraryScan()` 会发布替换后的曲目列表；其余用例覆盖真实生产服务的扫描、暂停/恢复、
焦点抢占、录音收尾与失败恢复。产物和日志保存在未提交的
`.codex-temp/hardware-next-gate/`，不把 host fake 结果写成设备验收。

设备端仍没有 Music 播放或 Recorder Start/Stop 的串口直控命令；下一次 COM3 门禁必须通过
已建立的 screen-control 远控流，按 enable/ACK、逐序列 pointer down/up、Music 播放→语音
暂停/恢复、Recorder Start/Stop 和至少 60 秒健康观察取证。实体 SD、ADC/DAC、麦克风、扬声器、
触摸、声学共存及八小时 soak 继续保持 **NO_GO**。

## 2026-10-09 Camera DVP 失败清理观察与修复

正式 8 小时采集的中间日志中，第 4 次应用探针完成 Camera → Home 切换。串口显示
`VIDIOC_STREAMOFF` 成功返回，但随后出现 `i2c_master_bus_rm_device: Wrong I2C status`、
`s_sccb_i2c_destroy`、DVP video deinit 和 `DEV_CAMERA_SUB_DVP` 清理失败；同一窗口还保留
`E:RX:153600-84480`。应用最终回到 Home 并回报 `RODAK_APP_LAUNCH_COMPLETE {"ok":true}`，
但这不能视为 Camera 物理资源已正确释放。

审查发现 `dev_camera_sub_dvp_deinit()` 在 `esp_video_deinit()` 失败后仍会释放 I2C 引用并
free board-manager handle，使失败状态可能留下悬空句柄，下一次初始化/重试存在 UAF 风险。
`29aaacd` 已修复为：底层视频或 I2C 引用释放失败时保留句柄和引用，只有两步都成功才释放；
随后 `4e08efa` 让 `CameraDevice::Release()` 返回失败并保留 `release_retry_required_`，
由下一次 `Acquire()` 先重试释放再重新初始化；`51927b0` 让 `CloseStream()` 仅在释放成功时
记录 complete，失败时明确记录 deferred-for-retry；capture fake 已同步错误返回接口。当前 10 项 host source-contract 回归全部通过。
`cc32989` 已补齐外层传播、DVP 分阶段 overlay 和失败引用回归；Camera wrapper Debug/ASan
各 4/4，DVP teardown 各 13/13。另有独立 IDF 6.0.2 RCC overlay 将 DVP deinit 的共享
引用从错误的 acquire 改为 release，Debug/ASan 各 6/6 通过。上述是源码、生成器和 host
所有权边界，不能写成真实 I2C、DMA、共享时钟或 Camera 画面已经通过。
包含这些修复的 043 候选已完成本地构建和签名包校验，身份见下方[候选包记录](#2026-10-09-网络与-camera-修复候选-043)。
当前 COM3 采集的旧包未包含这些修复。原采集结束后仍须保留 NVS，在新的设备窗口核验并刷写
候选，复验 Camera 重复启动/关闭、共享 LCD_CAM owner、实际资源余量和完整设备日志；本条
观察保持资源/生产 **NO_GO**。

## 2026-10-09 正式八小时采集最终结果

普通 OFF 包 `20261009-014905` 已完成 **28,800.17 / 28,800 秒**采集，最终
`status=no-go`、`complete=true`。采集器已退出并释放 COM3。最终结果包含 960 个健康样本，
95/95/95 次应用 request/ACK/completion；这些计数不覆盖应用内部失败。

16 次 Camera 请求中仅 7 次成功，9 次因 DVP DMA ring 无法从连续内部内存中分配而失败。
最低 `internal_min=215 B`、Voice supervisor 栈 1796 B、应用最大连续内部块 5632 B；日志还
保留 `E:RX`、SCCB/I2C 删除失败和 DVP deinit 失败。因此该包明确 **NO_GO**，也不能作为
后续修复包的长稳通过证据。

最终原始文件位于 `.codex-temp/release-soak-20261009-full/`。`serial.log` 为 2,099,134 B，
SHA-256 `f1717ea5ef0f511a171285099186512abfe9c67bddc47e3e50d223720741534d`；
`status.json` SHA-256 为
`c7da28066ee15572489f413dac368a08183463fa86d59078398295cb674a45ce`。
封存摘要见 `.codex-temp/camera-dvp-044/old-soak-final.json`。

## 2026-10-09 网络与 Camera 修复候选 043

043 使用 RodakOS 源码 `abecfb26b24e84b90bc555cf69eed0105d70e375`，配套 Rodak 源码为
`474b2e8587626a1fe1c2461096984b3d276e5c53`。ESP-IDF 6.0.2 本地完整构建完成，开发签名包
位于 `build/packages/ota/20261009-063127`，task 为 `network-camera-rcc-043`，version 为
`0.1.2-dev.1`。包包含 GOT_IP 可信路由刷新、Camera 分阶段失败清理及 DVP RCC 引用修复。

| 制品 | 大小 / SHA-256 |
| --- | --- |
| `rodakos.bin` | 7,157,168 B / `ddc7927ca87eb431eec882656fb76fc9032c95faa680f2feae1ea0c36fafe88d` |
| immutable Recovery | `ffa412ebe30c714c691bba73c8ab6e4efcaaab14fce5229f595707a8a08f75fd` |
| 合并首刷镜像 | `99f1aff4d0f3121301955bae0d62e9ea25f19a5d6304f8437c17ad2478a97444` |

manifest 明确 `developmentPackage=true`、`buildFlavor=production`、
`releaseFaultInjection=false`、`homeHardwareTestPopulation=false`。这里的 production 是普通
运行 flavor，不表示生产签名根或发布验收通过。

2026-10-09 约 06:46（Asia/Shanghai），本地命令
`python tools/ota_security.py verify-package --directory build/packages/ota/20261009-063127`
退出码为 0。它只核验磁盘上的签名包及声明的制品，不能证明当前设备的 partition table 或
immutable Recovery 与候选匹配。与旧包 `20261009-014905` 的本地文件比较确认 bootloader、
partition table、Recovery 和 OTA 公钥四项 SHA-256 一致，结果保存于
`.codex-temp/network-camera-rcc-043-review/immutable-disk-comparison.json`；这仍是磁盘文件比较。
043 的历史窗口**未运行设备 `flash_and_test.ps1 -VerifyOnly`，也未刷写**；
该设备核验会复位目标，必须等当前 COM3 采集退出并释放端口后另开窗口执行。后续刷写不使用
`-Erase`，保留 NVS、原设备 ID、`bound` 与 `tokenVersion=4`，并记录完整 Recovery → Main →
Home 启动证据。资源与生产发布仍为 **NO_GO**。

043 的上述“未刷写”描述只适用于其历史窗口；当前设备后续已使用继承相同 immutable
资产的 044 包完成 VerifyOnly 与保留 NVS 刷写，不能把 044 的结果倒算为 043 实机通过。

## 2026-10-09 Camera/DVP 修复候选 044 实机窗口

044 使用 RodakOS 源码 `a9c68bd454d697df6bc2120bd884fb44daebcf29`，ESP-IDF 6.0.2
完整构建并生成开发签名普通包 `build/packages/ota/20261009-174828`，task 为
`camera-dvp-release-044`，version 为 `0.1.2-dev.1`。包使用原开发签名根并复用 043 的
bootloader、partition table、OTA data 和 immutable Recovery。

| 制品 | 大小 / SHA-256 |
| --- | --- |
| `rodakos.bin` | 7,158,000 B / `54f96dcc539add0d45e3dda38b99e6156f6e422c72e5c4e5bac7c39887d1b747` |
| manifest | `fc145fdda086dcd1b7fa1d3fffaba710bc47aa6571d79f42e662f05b4bdfdf5b` |
| 合并首刷镜像 | `0d1cb096ff1cc5a3017c03b36c03db9dce93f689d6f7d3459971917f1bc90aba` |
| ZIP | `08ea13ef7f9137414d87817558f5b145438642da2b7f47855ea0a21fbb2e6cd3` |

`ota_security.py verify-package` 通过。`flash_and_test.ps1 -VerifyOnly` 在 COM3 逐字节核对
0x0 bootloader、0x8000 partition table 和 0x20000 Recovery，三项均匹配。随后只写入
0xf000 otadata 与 0x2a0000 ota_0，未使用 `-Erase`；Recovery → Main → OTA confirmation →
Home 首次启动通过。DeviceCloud 只读快照确认设备仍为 `44:1b:f6:c3:b4:30`、`bound`、
`tokenVersion=4`、MQTT 在线，固件版本为 `0.1.2-dev.1`。

三个独立 Camera → Home 软件窗口均取得首帧并依次记录 STREAMOFF、fd close、device
release、preview stop、preview destroy 和 audio release 六个关闭阶段；停止后至少 65 秒的
MQTT/Main/Voice 健康样本均新鲜，Voice 恢复 listening。每一轮仍在 STREAMOFF 期间产生
raw partial-frame `E:RX`：`153600-53760`、`153600-126720`、`153600-30720`。严格门禁
因此保持 NO_GO。应用窗口最大连续内部块最低低于
4.5 KiB，仍未达到发布余量要求。Camera 启动前的 DMA largest 为 16 KiB，尚未触发
6144/4096 fallback，故该窗口也不能证明降级 ring 的真实吞吐。

原始证据位于 `.codex-temp/camera-dvp-044/`，首次启动日志为
`build/logs/first-boot-20261009-175420.log`。这些软件首帧不能证明物理画质；raw `E:RX`、
低连续块、任意 OOM、完整资源归还及八小时新包长稳仍是阻塞项。

## 2026-10-09 Camera/DVP 停流修复候选 045 实机窗口

045 使用 RodakOS 源码 `85538b0`。controller stop 现在先于 sensor STREAMOFF，并在 DVP
spinlock 下同步发布 stop flag 与 FSM；worker 对已排队或正在处理的主动停流事件不再回调、
重启采集、重开 VSYNC 或记录半帧错误。真实非停流半帧仍保留错误语义。Camera capture
7/7、teardown 33/33、worker 17/17 加 7 组源码负变异在 Debug 与 ASan/UBSan 下通过；
ESP-IDF 6.0.2 构建、Camera teardown ELF 与 JPEG allocator 审计通过。

开发签名普通包 `build/packages/ota/20261009-193618` 的 task 为 `camera-stream-stop-045`，
version 为 `0.1.2-dev.1`，复用已验证的 immutable Recovery。

| 制品 | 大小 / SHA-256 |
| --- | --- |
| `rodakos.bin` | 7,158,304 B / `65a49fad94eb2903b9e6d32a0fb83336a927d5480125c21dd405b4b097f13481` |
| manifest | `95107c5fef40ddafec2f8863ed73af39ffbab233377506454efa4f63a711ff12` |
| 合并首刷镜像 | `fdd1f30d4cef39095a65d6cd42e2b177d78151a7eec3d13c497c06889e9fbb9a` |
| ZIP | `5d61895761dc4ca45a379d57f4ac2ec56a065382da811da001535b85dda0c7e4` |

签名验包通过。COM3 VerifyOnly 逐项匹配 bootloader、partition table 与 immutable Recovery；
随后只写入 `0xf000` otadata 和 `0x2a0000` ota_0，未使用 `-Erase`。Recovery → Main →
OTA confirmation → Home 首启通过。只读 DeviceCloud 快照确认原 ID
`c78845a8-06c9-4dcd-b7ff-d33e599f23ff`、设备 key `44:1b:f6:c3:b4:30`、`bound`、
`tokenVersion=4`、MQTT 在线和 voice idle 保持。

三个独立 Camera → Home 窗口均取得软件首帧、六个关闭阶段和 65.000–65.094 秒新鲜
MQTT/Main/Voice 健康观察；Voice 均恢复 listening。三份串口日志 error count 均为 0，
`E:RX` 总数为 0，关闭 044 的主动停流半帧问题。

本轮仍为 **NO_GO**。三个应用窗口的最大连续内部块均为 4,096 B，健康窗口的 DMA largest
均为 7,680 B；没有证明 6,144/4,096 ring fallback、充足资源余量、物理画质、任意 OOM、
完整资源回收或长期稳定性。因此未启动新的八小时资格长稳。证据位于
`.codex-temp/camera-stop-045/`，首次启动日志为
`build/logs/first-boot-20261009-193709.log`。

## 2026-10-09 Camera/DVP DMA headroom 候选 046

046 使用源码 `0e29b14`，将非 JPEG DVP ring 从配置值优先改为 6,144 B 首选、4,096 B
回退；JPEG 仍按配置值优先。320×240 RGB565 的 6,144 B 档实际 half 为 3,072 B、每帧
50 个接收事件，非 JPEG 每半区仍为一个 descriptor。Camera teardown 38/38、worker 17/17
加 7 组源码负控在 Debug 与 ASan/UBSan 下通过；Camera capture 7/7、device lifecycle 4/4、
DVP deinit 13/13、DVP RCC 6/6、ESP-IDF 6.0.2 构建和最终 ELF/JPEG 审计均通过。

开发签名普通包 `build/packages/ota/20261009-202156` 的 task 为
`camera-dma-headroom-046`，version 为 `0.1.2-dev.1`，复用已验证的 immutable Recovery。

| 制品 | 大小 / SHA-256 |
| --- | --- |
| `rodakos.bin` | 7,158,464 B / `fe29de1ef0410876bccdb34dfcc4584cf791f7a4facf3758bea407dcdfcccd9d` |
| manifest | `e10a432612cc19942d2532fca27d966ccc48a996429486e03a471d792da250e0` |
| 合并首刷镜像 | `7466fc4c2f80f4f9397ce64fa97a7843645c6b19c21ba80b44a0f6f9f67800a4` |
| ZIP | `5e406f7fbee62240744b34f6e77d75f58b88178293f1eaa6380141eff43684fe` |

签名验包通过。COM3 VerifyOnly 匹配 bootloader、partition table 与 immutable Recovery；
随后只写入 `0xf000` otadata 和 `0x2a0000` ota_0，未使用 `-Erase`。Recovery → Main →
OTA confirmation → Home 首启通过。只读 DeviceCloud 快照确认原 ID
`c78845a8-06c9-4dcd-b7ff-d33e599f23ff`、设备 key `44:1b:f6:c3:b4:30`、`bound`、
`tokenVersion=4`、MQTT 在线和 voice idle 保持。

一个独立 Camera → Home 窗口明确记录
`configured=8192 selected=6144 actual=6144 half=3072 desc_half=1`，取得软件首帧、六个关闭
阶段和 65.093 秒新鲜 MQTT/Main/Voice 健康样本，Voice 为 listening。随后单串口 5 次
循环每次均选择 6,144 B、取得软件首帧和六个关闭阶段。六次合计 `E:RX`、overflow、DQBUF
及 error 日志均为 0；5 次循环的内部 heap median drop 为 0。

本轮仍为 **NO_GO**。单次窗口应用最大连续内部块最低 4,352 B，5 次循环最低 5,120 B；
健康期 DMA largest 均为 6,144 B。该结果证明 6,144 B 实际路径可重复工作，但没有证明
4,096 B fallback、充足连续内存余量、物理画质、任意 OOM、完整资源回收或长期稳定性，
因此未启动新的八小时资格长稳。证据位于 `.codex-temp/camera-dma-046/`，首次启动日志为
`build/logs/first-boot-20261009-202314.log`。

## 2026-10-09 Camera/DVP 4,096 B fallback 故障注入候选 047

源码 `4bf341f` 增加默认关闭的 `RODAKOS_CAMERA_DMA_FORCE_4096` 测试开关。开关只作用于
Camera sensor 组件，非 JPEG 路径跳过 6,144 B 候选并输出
`RODAKOS_RELEASE_FAULT_INJECTION_ACTIVE`；普通构建保持原选择顺序。打包脚本会拒绝未显式
允许的 fault image。generator 13/13、Camera teardown 39/39 在 Debug 与 ASan/UBSan 下通过，
worker lifecycle 17/17 与 7 组源码负控通过；普通 OFF 和 fault ON 的 ESP-IDF 6.0.2 构建、
Camera teardown ELF 与 JPEG allocator 审计均通过。

开发签名 fault 包 `build/packages/ota/20261009-205201` 的 task 为
`camera-dma-fallback-047`，version 为 `0.1.2-dev.1`，复用 046 已验证的 immutable Recovery。

| 制品 | 大小 / SHA-256 |
| --- | --- |
| `rodakos_release_test.bin` | 7,158,512 B / `b0e298cbef5774d1d8382440f4c2a01cab72f1df8e1d5a876e2be988110395b2` |
| manifest | `705367a18ec7a9439c46e0b854393fc009d411c5fa3a82f59bf3739b63025c09` |
| 合并首刷镜像 | `c3cfb9dcabbc75acbddf1d85d5de5b71d7978353181276a39a632d70905d3024` |

`verify-package --allow-faults`、COM3 VerifyOnly 与保留 NVS 的 `otadata + ota_0` 刷写通过，
未使用 `-Erase`。Recovery → Main → OTA confirmation → Home 首启通过。一个单串口 Camera →
Home 窗口记录一次 fault marker，并明确选择
`configured=8192 selected=4096 actual=4096 half=2048 desc_half=1`。该轮取得软件首帧和六个
关闭阶段，预览记录 6 帧；`E:RX`、overflow、DQBUF 与 ESP error 均为 0。随后 65.000 秒内
取得两次 MQTT、两次 Main 和一次 Voice 新鲜健康样本，MQTT connected、Voice listening；
内部 heap median drop 为 0。

该轮仍为 **NO_GO**。应用窗口最大连续内部块最低 6,400 B，健康期 DMA largest 最低
6,656 B，Voice supervisor 最低剩余栈 2,388 B，内部历史最低 2,123 B；采集器因此继续报告
`insufficient_memory_or_stack_headroom`。047 证明 4,096 B 软件 fallback 可取得首帧并完成
关闭，不证明充足资源余量、物理画质、任意 OOM、完整资源释放或长期稳定性。

验证后已重新刷回普通 046 包 `20261009-202156`，再次通过 VerifyOnly、Recovery → Main →
OTA confirmation → Home；只读 Rodak 快照确认原设备 ID
`c78845a8-06c9-4dcd-b7ff-d33e599f23ff`、设备 key `44:1b:f6:c3:b4:30`、`bound`、
`tokenVersion=4`、MQTT 在线、voice idle，COM3 已释放。工作区也已将测试开关恢复 OFF 并
重建普通固件，最终二进制不含 fault marker。证据位于 `.codex-temp/camera-dma-047/`；
fault 首启与恢复首启日志分别为 `build/logs/first-boot-20261009-205256.log` 和
`build/logs/first-boot-20261009-205719.log`。资源/生产发布保持 **NO_GO**，尚未启动八小时
资格长稳。

## 2026-10-09 Camera 切换前释放与 Home 重建边界 048–050

048 源码 `d988c95` 将当前 app 的 `OnPause` 移到候选 `OnCreate` 之前；候选创建失败时恢复
旧 app。049 源码 `686d9bb` 进一步在暂停阶段删除 Camera UI、三个 timer 和预览像素，停止
DVP preview 并释放音频焦点；回滚时重建 UI 并重新调度 preview。app-model 278 项、
navigation Debug/ASan 各 13 项、Camera UI Debug/ASan 各 13 项通过；Camera capture 45 项加
6 个关闭/回收 CTest、device lifecycle 4 项、teardown 39 项、worker lifecycle 17 项及源码
负控通过。普通 OFF ESP-IDF 6.0.2 构建、Camera teardown ELF 和 JPEG allocator 审计通过。

三份开发签名普通包均复用 046 已验证的 immutable Recovery，均先通过 COM3 VerifyOnly，再只
写入 `otadata + ota_0`，未使用 `-Erase`，Recovery → Main → OTA confirmation → Home 通过：

| 候选 | task / 主镜像 SHA-256 | manifest / 合并镜像 SHA-256 |
| --- | --- | --- |
| 048 `20261009-213419` | `camera-app-lifecycle-048` / `6e10c47d846097d5c387f32d38f2f5519d66a9504756a3e7a9abe35671cb778f` | `f051ce2b448f16ff6b1c8a1ec43a6b83741075904b5dc49b081e3a6d22a8bce7` / `d13f6292a4b4af0c64b7614de76a8ac885172d843fbb134315faaee62ae434e0` |
| 049 `20261009-214503` | `camera-ui-release-049` / `9dd853e42cd42a00ae3673dd6a095e697192c0ee7f4db3504dcd3afabfbef081` | `5f8b51670f1319575ee2da1176af2d39a0898ce1a51e6a052588a12422103305` / `437984e1f547b2e782f355cef0003f412391b6c6b0d63f5aae1e214c1f06e550` |
| 050 `20261009-215157` | `camera-pause-resource-050` / `d84fa396d29121bf5e4de6758696f14f13bbd09d3424a12856d813199d619abc` | `e9ed8e7dffbdbcd73daf15e3dd9cc205f5a0ae4852a21c91a056ac52f9af3283` / `88d42ca060521c00cb5b1b75d2910bd9cefdbf6ff0c13b4fedbbd02b27871c37` |

048、049、050 的独立单串口 Camera → Home 均选择普通 6,144 B ring，取得软件首帧、六个关闭
阶段和至少 60 秒 MQTT/Main/Voice 健康观察；error、`E:RX`、overflow、DQBUF 为 0，heap
median drop 为 0。048 证明 DVP/device/audio 在 Home `OnCreate` 前释放；049 证明旧 Camera
UI/timer/像素也在 Home 前释放。050 的阶段快照把剩余边界定位为：

| 阶段 | internal free / largest | DMA free / largest |
| --- | --- | --- |
| Camera UI 已释放 | 31,603 / 16,384 B | 26,063 / 16,384 B |
| DVP preview 已停止 | 38,983 / 16,384 B | 33,443 / 16,384 B |
| 音频焦点已释放 | 38,983 / 16,384 B | 33,443 / 16,384 B |
| Home UI ready | 19,451 / 6,144 B | 后续健康期最低 13,379 / 6,144 B |

因此 Camera/DVP 切换前释放已在该短窗口达到 16 KiB 连续块；连续块降至 6,144 B 发生在
Home 重建期间。050 仍为 **NO_GO**：Voice supervisor 最低剩余栈 2,388 B，内部历史最低
3,395 B；物理画质、任意 OOM、异常媒体/网络/音频并发和长期稳定性未证明。后续工作转为
Home 重建的 LVGL/内部堆分配与碎片化，不启动八小时资格长稳。证据位于
`.codex-temp/camera-lifecycle-048/`、`.codex-temp/camera-ui-release-049/` 和
`.codex-temp/camera-pause-resource-050/`；050 首启日志为
`build/logs/first-boot-20261009-215216.log`。

## 2026-10-09 Home 重建与返回连续内存 051–056

051 源码 `ba58fe4` 在 Home 重建的 containers、status-bar、tile-shells、page-N 和
footer-timer 阶段记录 internal/DMA 快照，确认 Camera 释放后的 16 KiB 连续块首次在 page-1
降到 6,144 B。052–055 继续缩减首屏磁贴分配：`fd975c6` 把每个磁贴的 7 个事件描述符合并为
一个 `LV_EVENT_ALL` 回调；`1d20a4b` 将 `TilePayload` 优先放入 PSRAM；`65de5bc` 合并普通
应用图标背景与图标标签；`9312e1e` 改为在按钮 draw 事件中直接绘制图标。冷启动 page-1 的
内部占用由约 14.8 KiB 降至约 6.2 KiB，055 冷启动 Home ready 最大连续块为 59,392 B。

Camera 运行仍会改变内部堆布局。055 的 Camera 释放后最大连续块只有 7,680 B，Home page-2
后降为 6,912 B，footer 后为 6,400 B，健康期最低为 6,144 B。056 源码 `e754869` 因此在
旧 Home 销毁后、DVP 启动前依次尝试保留 12,288、10,240、8,192 B 的 internal/DMA 连续块；
Camera 启动失败、preview timer 创建失败、暂停和销毁路径均幂等释放。Camera UI 生命周期
回归新增对运行期保留、暂停释放和回滚重建的直接检查，Debug 与 ASan/UBSan 均通过；普通
OFF ESP-IDF 6.0.2 构建、Camera teardown ELF 与 JPEG allocator 最终审计通过。

六份开发签名普通包均复用 046 的 immutable Recovery `20261009-202156`，先通过 COM3
VerifyOnly，再保留 NVS 只写 `otadata + ota_0`，未使用 `-Erase`：

| 候选 | 包 / task | 主镜像 SHA-256 |
| --- | --- | --- |
| 051 | `20261009-221637` / `home-resource-diagnostic-051` | `bcf230c6f58d29d8e03fd3ab87c442e4ccd62e6b5e9ea16b9d6b9ec5a37a4c05` |
| 052 | `20261009-222502` / `home-tile-events-052` | `4ab667813ec34f766902dabf002b29bdf84417248468c604db3bbad937fe2b62` |
| 053 | `20261009-223447` / `home-tile-payload-053` | `e772b435ec4c6bfec58564b74d70703f97d8db78cc69a5c8b67d949ff39c8bfb` |
| 054 | `20261009-224209` / `home-tile-objects-054` | `d14b703b57e056cdec745a6bcd8c92fad6ca040f9c3f63267f5331c3decba308` |
| 055 | `20261009-224924` / `home-tile-draw-055` | `bad3e486ad5725db5d1ee1d10d101f09dbc819e1d54cfc9481779fc8dd799e2f` |
| 056 | `20261009-230114` / `camera-home-reserve-056` | `8f424317df2fd168fb158583b3a90748d2dae798478dcdb17352ea6e6701cb88` |

056 manifest SHA-256 为
`732b3dac50cf10fdb4cfb9778e38491c4325f2816c9995162ffb2e78ae9445f4`，合并镜像 SHA-256
为 `db407dac5ff3812730b911a158b49d4d9b5ec0b9f36f4ceb9a86c065956b8f3f`，ZIP SHA-256 为
`f0aa60184a9ece1e7bb8027ce8ebb8bf319a3beb77c6983f47211b0c8c01fd5c`。Recovery → Main →
OTA confirmation → Home 通过，设备 MAC 为 `44:1b:f6:c3:b4:30`。本轮未做主镜像 readback，
也未重新取得服务器侧绑定快照，因此不把包上下文或既有 NVS 保留扩大为安装镜像/绑定复核。

056 的单串口 Camera → Home 窗口实际选择 8,192 B reserve 和普通 6,144 B DVP ring，在
67 ms 取得 Camera 首帧并提交软件预览，随后完整记录六个关闭阶段。关键阶段为：

| 阶段 | internal free / largest | DMA free / largest |
| --- | --- | --- |
| Camera UI 已释放 | 20,651 / 5,632 B | 14,139 / 3,712 B |
| DVP preview 已停止 | 28,035 / 7,680 B | 21,523 / 7,680 B |
| 8,192 B reserve 已释放 | 36,231 / 8,192 B | 29,719 / 8,192 B |
| Home page-1 | 27,383 / 8,192 B | 20,871 / 8,192 B |
| Home page-2 | 26,203 / 8,192 B | 20,095 / 8,192 B |
| Home footer / ready | 26,031 / 8,192 B | 19,519 / 8,192 B |

后续 65.016 秒窗口取得 3 个 MQTT、2 个 Main 和 1 个 Voice 新鲜健康样本，internal/DMA
largest 最低均为 8,192 B，heap median drop 为 0；`E:RX`、overflow、DQBUF、panic、abort
均为 0。当前短窗口连续块门禁通过，但资源与生产仍为 **NO_GO**：Voice supervisor 最低剩余
栈仍为 2,384 B，内部历史最低为 3,395 B，物理画质、任意 OOM、异常媒体/网络/音频并发和
长期稳定性未证明，因此不启动八小时资格长稳。证据位于
`.codex-temp/camera-home-reserve-056/`，首启日志为
`build/logs/first-boot-20261009-230150.log`。

## 2026-10-09 Voice supervisor 栈余量 057

057 源码 `7400ed5` 将 `voice_wake` 的 WithCaps PSRAM 栈从 4,096 B 提高到 6,144 B，并在
Voice health 中同时输出 `supervisor_stack_bytes` 与 `supervisor_stack_min_free`。宿主回收
模型新增实际创建栈大小观测，直接断言生产调用传入 6,144 B。VoiceWakeService 36 项和旧源
SIGABRT 负控在 Debug、ASan/UBSan 下通过；共享 task-retirement 14 项、Voice identity 集成
Debug/ASan、普通 OFF ESP-IDF 6.0.2 构建、Camera teardown ELF 与 JPEG allocator 审计通过。

后续工具提交 `49e09f0` 将独立门禁固化为 supervisor 配置容量至少 6,144 B、历史最低剩余栈
至少 4,096 B；这不提高 Main/MQTT 的共享 512 B 解析下限。release evidence 32 项和单串口
Camera smoke 13 项通过。056 原始日志在新规则下会因 2,384 B 余量失败；057 原始串口日志的
离线复核没有 stack failure。

开发签名普通包 `20261009-231940` / `voice-supervisor-stack-057` 复用 immutable Recovery
`20261009-202156`。主镜像 7,161,216 B，SHA-256
`28f760c01bea2dda77e29a04e47dfda49f51168e04250e4ac7ec9ffe0595241a`；manifest SHA-256
`6682417c60e580576b6cf1a1a754bb9bf99283dffd662ed3ae71f010c2f6b46e`，合并镜像 SHA-256
`ed56dd0eee1b2109d78da13bad1a1c426d0548537d3ec02bf97a50cbb2daa6c5`，ZIP SHA-256
`2c69b0d0e14c007031a96fdb1d89fd7ad0bd4c5d893c7d4be97d9f75af4f1fed`。COM3 VerifyOnly 后
保留 NVS 只写 `otadata + ota_0`，未使用 `-Erase`；Recovery → Main → OTA confirmation →
Home 通过。首次 Voice health 明确记录配置容量 6,144 B、剩余栈 4,736 B。

随后单串口 Camera → Home 窗口实际选择 8,192 B Home reserve 和普通 6,144 B DVP ring，
69 ms 取得 Camera 首帧并提交软件预览，六个关闭阶段完整。reserve 释放、Home page-1、
page-2、footer/ready 及 65.062 秒 MQTT/Main/Voice 健康窗口的 internal/DMA largest 最低均为
8,192 B；Voice supervisor 最低剩余栈为 4,432 B，配置容量仍为 6,144 B。`E:RX`、overflow、
DQBUF、panic、abort 为 0，heap median drop 为 0。因此当前有界连续块与 supervisor 栈门禁
均通过。

资源与生产仍为 **NO_GO**：软件首帧不证明物理画质，任意 OOM、异常媒体/SD、音频/TLS/MQTT、
cache-off/NVS/OTA 并发恢复、生产签名/readback/power-cut 和长期稳定性未完成，因此没有启动
新的八小时资格长稳。本轮未做主镜像 readback，也未重新取得服务器侧绑定快照。证据位于
`.codex-temp/voice-supervisor-stack-057/`；串口 SHA-256 为
`40d6e1a68f0da687d87d1d10e3d624c08d44e1d61260f2e8a55b06594321630b`，新门禁复核
`stack-gate-review.json` SHA-256 为
`349a39e38fc9804695825043f8df5559457433cde6c1cfa58bfc3b2678b3407e`，首启日志为
`build/logs/first-boot-20261009-232019.log`。

## 2026-10-10 Camera STREAMON 失败清理 058

057 在 Display 同传占用内部/DMA 连续内存时实际触发 `VIDIOC_STREAMON` 内部的 DVP ring
分配失败。驱动未进入流状态，但旧 `CameraService::CloseStream()` 仍调用 `VIDIOC_STREAMOFF`；
该调用返回 `EBUSY` 后，现有安全边界会保留 fd、mmap 与 Board Manager ownership，后续 Camera
因此持续报告 cleanup pending。源码 `9babfae` 新增独立的 `stream_started_` 状态，只在
STREAMON 成功后执行 STREAMOFF；启动前或 STREAMON 内失败直接释放 mappings、fd 和设备。
真实流启动后 STREAMOFF 失败仍保留全部 ownership 等待重试，没有削弱原有保护。

Camera host 目标直接编译完整生产 `camera_service.cc`。新增用例让 open/mmap/QBUF 成功、
STREAMON 以 `ENOMEM` 失败，同时预置 STREAMOFF 为 `EBUSY`，断言 STREAMOFF 调用数为 0、
映射归零，并在恢复后允许第二次 Start/Stop。Camera 可执行文件现为 **46/46**；Camera capture、
四个 teardown diagnostics 与两个 worker retirement 共 **7/7 CTest**，在 Debug 和
ASan/UBSan/leak 两种配置均通过。把相同用例链接到修复前完整生产 TU 时，因错误调用
STREAMOFF 命中负控。ESP-IDF 6.0.2 构建、Camera teardown linked-ELF 与 JPEG allocator
linked-ELF 审计通过。

开发签名普通包 `20261009-234829` / `camera-streamon-cleanup-058` 复用 immutable Recovery
`20261009-202156`。主镜像 7,161,264 B，SHA-256
`e107bc0901707b3ec5d33f2c035633b662e99b29d9e00d90abd0fd0708f1503f`；manifest SHA-256
`62c7c50320f4de4e7621738b48a5f293e94e171361a8dbae3c7129473a11ff2e`，合并镜像 SHA-256
`e8b1cddc0571ec7292a1445c7b94f737983d25890e6980242fd003444aa99a09`，ZIP SHA-256
`353d8d77d1c92faeea3c30abb5a45afbe9703e6ef8ddfee1c3ecd3e700815b7b`。COM3 VerifyOnly 后
只写 `otadata + ota_0`，未使用 `-Erase`；Recovery → Main → OTA confirmation → Home 通过，
WiFi、MQTT、Voice listening 和 8,192 B 启动健康连续块恢复。服务器最终快照仍为设备
`44:1b:f6:c3:b4:30`、`bound`、tokenVersion 4、MQTT connected、voice inactive。

普通 058 未再次触发 STREAMON OOM。Display 同传保持运行时，本地 Camera 连续三次启动，
DVP ring 实际依次选择 6,144、4,096、4,096 B，首帧分别为 128、162、133 ms；前两次显式
返回 Home 均完成 STREAMOFF、fd close、device release，并在 reserve 释放后恢复 8,192 B
最大连续块。第三次之后原 MQTT 信令连接发生自然 epoch 切换，桌面会话被撤销；设备随后已在
Home 建立新的 Display 会话并正常停止。两份串口中 `Failed to start camera stream`、DVP ring
`no mem`、`STREAMOFF failed`、panic/assert/Backtrace 计数均为 0。

Display 停止后，Remote Camera 在同一启动周期选择 6,144 B ring，74 ms 取得首帧，运行
25.604 秒 / 389 帧。WebRTC Camera connected 期间 internal/DMA largest 最低为 2,560 B；
停止完整经过 STREAMOFF、fd close、device release，恢复为 6,144 B，随后 51 秒 Main/MQTT
窗口保持 6,144 B，并取得新的 Voice health，supervisor 剩余栈仍为 4,432 B。远端收到的
320×240 JPEG SHA-256 为
`7b68a3de4667b8288ebd434177f0344b58e198e53c67bef7fc51dfb5851b5f46`，但所有像素均为同一
RGB 值 `(23, 28, 24)`，三个通道标准差均为 0。因此真实 sensor → JPEG → WebRTC 传输链路
通过，物理画质门禁不通过。

资源与生产仍为 **NO_GO**：本包没有在硬件上重现失败 STREAMON，不能用成功并发倒推失败
清理已获实机证明；任意 OOM、均匀暗帧根因、异常媒体/SD/音频/TLS/MQTT/cache-off/NVS/OTA
并发、生产签名/readback/power-cut 和长期资格测试仍未完成，没有启动新的八小时长稳。
Rodak 证据位于 `D:\workspace\rodak\.codex-temp\camera-physical-058\`；两份串口 SHA-256
分别为 `521a8340080f555dc1f0c59c3f554c725513d9e8cdc08a1716bf07f36329d766` 和
`0823322787681d09cafa16037f8d52ca1b6b3b300b53e9a54e5f5e6bacd34d4a`，首启日志为
`build/logs/first-boot-20261009-234849.log`。

## 2026-10-10 GC0308 test-pattern 定位 059 与普通恢复 060

源码 `5972684` 增加默认关闭的 `RODAKOS_CAMERA_TEST_PATTERN` 诊断开关。开启时仅在
Camera 打开后通过 `V4L2_CID_TEST_PATTERN` 请求 GC0308 内建彩条，并输出
`RODAKOS_RELEASE_FAULT_INJECTION_ACTIVE camera_test_pattern=1`；打包与刷写均要求显式
release-fault allow，普通 OFF 构建不含该 marker。Camera Debug 与 ASan/UBSan/leak 均为
**8/8 CTest**，ESP-IDF 6.0.2 构建及最终 Camera/JPEG linked-ELF 审计通过。

受控诊断包 `20261010-001850` / `camera-test-pattern-059` 的主镜像为 7,161,808 B，
SHA-256 `96e0a7ed9a011a5dba79afe0b59c9011583ea5f1eda7f998ea6f47496c27e734`。包先通过
fault-aware 验签与 COM3 VerifyOnly，再保留 NVS 只写 `otadata + ota_0`，未使用 `-Erase`；
Recovery → Main → OTA confirmation → Home 通过。实机 Remote Camera 串口明确出现诊断
marker，选择 6,144 B ring，125 ms 取得首帧，16.957 秒输出 258 帧并完整完成 STREAMOFF、
fd close 与 Board Manager release。Rodak 收到的 320×240 JPEG 为标准彩条，10,341 B，
SHA-256 `fccaff659ef625cfda7426896d8895fdee44a6aa32249979a69a9f7d173496e2`；RGB 通道标准差
分别约为 102.38、111.83、106.62，包含 10,254 个颜色值。WebRTC 期间 internal/DMA largest
最低 2,560 B，停止后恢复 6,144 B，串口无 DVP、panic、abort 或 watchdog 错误。

该结果证明 GC0308 的 SCCB 控制可切入 test pattern，且 sensor 数字输出、DVP/RGB565、JPEG、
WebRTC 与 Rodak 显示链路能够传递非均匀像素；它不证明普通成像模式的曝光、增益、时钟、
供电、寄存器表或光学输入正确。059 诊断结束后已将 CMake cache 恢复为
`RODAKOS_CAMERA_TEST_PATTERN=OFF`，普通二进制不含 fault marker，并生成开发签名 production
flavor 包 `20261010-003215` / `camera-test-pattern-off-060`。060 主镜像仍为 7,161,264 B，
SHA-256 `40d344680d4c6004ed4b07d0d47ef7a9b53cd93603809dc9e2a1f8b3c5284d41`；manifest、merged、
ZIP SHA-256 分别为 `dd33c22768dff337b310470655c944681258cfff3960d32dd28e2949da405b52`、
`d900913c09c8333be80646a443907e14bf61ac54d6df45974c06143cc866c82b`、
`d92f95d1917e036be269637a40d3d3d52baab802d582dc7ea372d41bb64ced81`。

060 通过验签、COM3 VerifyOnly 与保留 NVS 的 `otadata + ota_0` 刷写，未使用 `-Erase`；
Recovery → Main → OTA confirmation → Home、`RodakOS-Lab` 自动联网及 MQTT 恢复通过。服务器
快照仍为设备 `44:1b:f6:c3:b4:30`、原 device ID、`bound`、tokenVersion 4、MQTT connected。
普通模式对照在 91 ms 取得首帧并完整关闭，串口无 fault marker 与 Camera/DVP 错误；JPEG
重新得到与 058 完全相同的单色暗帧，SHA-256
`7b68a3de4667b8288ebd434177f0344b58e198e53c67bef7fc51dfb5851b5f46`。因此下一步应直接核对
普通模式 GC0308 初始化后的关键寄存器、曝光/增益与时钟/电源状态，不再优先怀疑 JPEG 或
WebRTC 传输。物理画质、失败 STREAMON 实机清理、任意 OOM、并发矩阵和资格长稳仍开放，
发布保持 **NO_GO**，没有启动新的八小时长稳。证据位于 Rodak
`.codex-temp/camera-physical-059/`、`.codex-temp/camera-physical-060/` 及
`.codex-temp/camera-physical-058/camera-stream-*-059-pattern*`、`camera-stream-*-060-off*`；
060 首启日志为 `build/logs/first-boot-20261010-003900.log`。

## 2026-10-10 GC0308 寄存器诊断 062 与普通恢复 063

提交 `c73c08f` 增加默认关闭的 `RODAKOS_CAMERA_SENSOR_DIAGNOSTICS`。诊断包
`20261010-011055` / `camera-register-settled-062` 通过 fault-aware 验签、COM3 VerifyOnly
及保留 NVS 的 `otadata + ota_0` 刷写，未使用 `-Erase`；Recovery → Main → OTA confirmation
→ Home、WiFi/MQTT 和绑定保留均通过。设备串口在 configured、streaming、first-frame 和第
60 帧输出 page 0/1 快照，全部 `failures=0`；首帧 94 ms、运行 328 帧，STREAMOFF、fd close
和 device release 完整。

第 60 帧时 page 0 的曝光候选由 `03=00 04=96` 变为 `03=01 04=e0`，page 1 的动态值由
first-frame 的 `62=69 63=1f 64=56 65=5f` 收敛为 `62=1c 63=1c 64=1c 65=1c`，证明
AEC/AGC 已经在改变传感器状态。Remote Camera 仍得到与 058/060/061 完全相同的单色暗帧，
JPEG SHA-256 为 `7b68a3de4667b8288ebd434177f0344b58e198e53c67bef7fc51dfb5851b5f46`。
因此当前软件证据排除了“曝光未运行”与 JPEG/WebRTC 传输链路，下一步应检查模拟前端、
镜头/遮挡、供电和普通 RGB 输出配置；物理画质仍未通过。

诊断后已恢复普通 063 包 `20261010-064533` / `camera-register-off-063`：
`RODAKOS_CAMERA_SENSOR_DIAGNOSTICS=OFF`、`RODAKOS_CAMERA_TEST_PATTERN=OFF`，主镜像
7,161,264 B，SHA-256 `846b585769b96b6c6e77cc996d5442fabd19435bb30ab93d2f278d0b58b52ec3`。
普通包通过验签、COM3 VerifyOnly、保留 NVS 刷写、Recovery → Main → OTA confirmation →
Home、自动联网和 MQTT 恢复；无 fault marker，未使用 `-Erase`。原始 062 串口为
`.codex-temp/camera-register-062/serial-062.log`，Rodak 侧帧证据位于
`D:\workspace\rodak\.codex-temp\camera-physical-058\camera-stream-*-062-settled.*`。
本条不关闭任意 OOM、异常并发、生产签名/readback/power-cut 或资格长稳，发布继续 **NO_GO**，
未启动新的八小时长稳。

## 2026-10-09 采集门禁补齐 Camera 首帧与 Voice 栈

Camera 导航 completion 早于延迟启动的预览，不能证明已取得首帧。采集器现在分别记录
`camera_requests` 和 `camera_preview_submissions`，关联本次请求、Camera 创建和
`Camera preview image updated`。允许合法的首帧早于 completion、启动日志早于 queue ACK；
退出、旧时间戳或重复首帧不能补足另一实例，已退出或采集结束仍无首帧时记录
`missing_camera_preview_submission`。该日志只证明 `lv_image_set_src` 后的软件预览提交，
不证明屏幕物理输出、成像质量或资源回收；当前协议没有逐请求 wire ID，关联范围限定于
单客户端有序串口观察。

Voice `supervisor_stack_min_free` 也已纳入全局 `stack_min_free` 的最小值，原字段仍保留在
`resource_minima.voice`，防止健康 MQTT 栈掩盖 Voice 低栈。完整 OTA Python 回归 **37/37**
通过（签名 5、采集器 32）；无 Camera 首帧以及 Voice 栈 128/511 B 的负控在旧解析器误通过，
新解析器均为 NO_GO；当时 512/2048 B 的共享任务边界保持通过。057 在该共享边界之外新增
Voice supervisor 专用的 6,144 B 配置容量与 4,096 B 剩余栈门槛，旧历史结果不追溯改写。

06:51:13 封存的中间输入位于 `.codex-temp/camera-soak-parser-20261009-065113/`。
`serial.log` 快照 637,971 字节，SHA-256
`28477a2b5d55cfbe3369040c1bc264b62df08a2be055b33a2672996bd93f90a4`。
离线重放保留 295 个健康样本和 29/29/29 应用事件，5 次 Camera 请求只有 3 次软件预览提交；
新增缺首帧失败，全局最小栈为 1796 B。新旧解析器均为 `no-go / complete=false`，没有把本轮
误写成曾经通过；该快照也不是完整八小时日志。比较清单、原/新摘要及栈负控分别保存在
`manifest.json`、`before.json`、`after-stack.json`、`stack-controls.json`。原采集进程未重启，
仍执行启动时加载的旧解析器；这些修复适用于后续采集及独立离线复核，不改变 043 固件内容。

## 2026-10-09 六轮合成语音尾段归属复核

只读检索原 `server-device*.ndjson`，补取历史 session
`5d18440a-a694-4341-bb70-4bfed30127b3` 的完整 trace；没有重新连接设备或重复六轮测试。
对应历史基线仍为 Rodak `44fbc43c`、RodakOS `b941705` 与普通 OFF 包
`20261009-014905`，不把新服务端修复当作这次旧窗口的证据。

六个 `vad-end` 均完成回复，流式下行分组的包数依次为 **200、340、340、337、442、305**，
与原串口六组 `Playback audio stats` 的 packets/decoded_frames 逐组一致，写入失败均为 0。
流式模式的最终 `audioPackets` 数组为空，因此不能仅用该字段推断是否有音频输出。

最终尾段在 `18:44:21.085Z` 记录 `reason=listen-stop`、9 packets / 41 payload bytes、
`hadLiveVadSpeech=false`、`captureStartReason=binary-auto-capture`。随后进入 VAD，并在
`18:44:21.127Z` 完成为 `discarded=true`、`discardReason=ambient-noise`、
`agentStreamedSentenceCount=0`；尾段开始后至会话断开没有音频包发送或播放完成日志。
因此这是六次合成回复加一个被丢弃的停止尾段，文件数 7 不表示七轮回复。

原串口与摘要 SHA-256 均复核未变；原 `final-summary.json` 的文件数判定仍保留，不覆盖历史。
本次 1,268 条设备窗口快照位于 Rodak `.codex-temp/six-turn-tail-audit/device-window.json`，
SHA-256 为 `c5c1af3ef759d3791acaaa250934bbc194cf59e6e356f646c52457f067d41422`；
逐段对账 `reconciliation.json` SHA-256 为
`792c5e62afb50743b0a0347d2e099390d37f3a4a87e3785ac2c56d9ca1250205`。
本条只收口该次合成六轮窗口的服务端尾段归属，真人六轮、声学、资源与长稳门禁继续开放。

## 2026-10-09 语音 cycles 逐轮门禁修复

`run_serial_voice_test.py --cycles` 旧门禁只统计整段日志；前一轮有两条播放统计、后一轮完全
没有播放时也可能通过。现以每轮 PCM 上传的 `audio_begin` 回执分窗，每窗必须有自身的
wake、session identity、匹配 focus token、正向播放统计、transport cleanup、stop 和 rearm。
允许异步任务的合法启动/播放日志交错、清理期间末尾播放统计，以及自然超时先于显式 stop
回执。barge-in 的 interruption、AFE confirmation 和 VAD end 也逐窗核对，不能跨轮借用。

`tests/voice_serial_tool` **21/21** 通过。保存的旧实现 AST 对两类反例仍错误通过：后一轮无
播放，以及第一轮两次打断而第二轮零次；新工具均拒绝。三份普通 OFF 封存日志离线复核仍
通过：late-follow-up、live-mic、barge-in 各自为 2/2、1/1、2/1 组播放开始/统计，被打断的首段
不强求正常结束统计；barge-in 的三个事件计数各为 1。未找到独立多 cycle 的实机封存日志，
多轮反例与正例仍属于 host 合成控制，不宣称新增硬件通过。

证据位于 `.codex-temp/voice-cycle-validator-20261009-071018/`，原日志哈希与首轮复核见
`review.json`，最终逐轮打断负控见 `barge-count-review.json`，后者 SHA-256 为
`6a5f2f4557858676c2d66adb5c90d244908c23932b381de14f5a4e1052e79525`。
本轮只修改主机工具与测试，没有打开 COM3、重启长稳或修改 043 固件。

## 2026-10-09 Camera 短窗口工具与 044/045 执行结果

新增 `tools/run_serial_camera_smoke.py`，调用前置和模板见
[单串口 Camera 冒烟](../tests/camera_serial_tool/README.md)。工具在原采集退出、设备 immutable
核验、保留 NVS 刷写与 DeviceCloud 身份基线完成后，才在同一个串口会话内重复执行
Camera → 软件首帧 → Home → 六个关闭阶段 → 至少 60 秒新鲜 MQTT/Main/Voice 健康观察。
阶段失败保留 NO_GO 并停止下一轮；收尾只在同一会话有界等待/请求 Home，不重发已发的
Home 请求、不复位、不抢占其他串口进程。

纯离线 Camera 工具 **13/13**、其依赖的 Voice **21/21**、OTA **37/37** 回归通过。
双轮正例及缺首帧、deferred release、缺 Voice、断线、低栈、过期关闭日志、延迟/缺失 Home
和中断清理等负例使用内存串口 fake。离线结果位于
`.codex-temp/camera-smoke-preparation-20261009-073129/review.json`。

044 已按该入口执行三次独立单轮实机观察；三次软件路径均闭合，但 raw `E:RX` 和不足资源
余量使结果保持 NO_GO。原始日志与摘要位于 `.codex-temp/camera-dvp-044/smoke*`。

045 又执行三次独立单轮观察；软件首帧、六阶段关闭和 65 秒健康观察均闭合，三份日志均无
`E:RX`。应用连续内部块仍为 4,096 B，资源门禁继续 NO_GO。原始日志与摘要位于
`.codex-temp/camera-stop-045/smoke-*`。

包、刷写记录与 boot log 仅作为外部提供的上下文；结果明确
`installed_image_verified=false`、`binding_verified=false`。`software-smoke-observed` 不是
物理成像或资源回收证明，`eight_hour_gate_passed` 始终为 false。

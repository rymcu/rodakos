# OTA Release Readiness

The existing Home, voice, media, MQTT, WebRTC display, and signed-appearance functional gates are
accepted as completed where their evidence is recorded in the repository. This document tracks only
the remaining signed-firmware release, interruption, and resource-failure work. Passing software
tests does not close physical power-loss or full heap-exhaustion gates.

Decision of 2026-10-07: release evidence is collected through local tests, firmware builds,
package verification and the physical gates below. GitHub Actions success or repair is not a
delivery prerequisite; do not rerun Actions or pursue billing, quota or required-check setup.
Preserve existing CI outcomes with their original candidates as history, including failures.
Missing or unavailable Actions do not make the release NO_GO; the unresolved software,
production-root, power-loss, resource and soak gates retain their existing acceptance criteria.

## Current evidence

The latest recorded device package is `20261008-055334`, built from
`34c9e6453344b8eb7896ab5476ef222504c12a94`, with the existing development root and preserved
NVS/binding/token version 4. Guarded boot, closed normal/matrix windows, seven correlated Stops
and the remote-pointer Home observation are recorded separately in
[030](#2026-10-08-video-task-retirement-and-navigation-030). Resource/production **NO_GO**,
Camera quality and three voice self-delete paths remain open. Historical
[029](#2026-10-08-aes-dma-allocation-cleanup-029),
[028](#2026-10-08-cooperative-dvp-worker-validation-028) and
[027](#2026-10-08-candidate-capacity-and-camera-failure-027) retain their own identities and limits. Exact Stop evidence remains in
[026](#2026-10-07-exact-stream-stop-026); earlier dated windows retain their own firmware identity.

Evidence review updated on 2026-10-08. The earlier source baseline `c64cf06` / `f7e8c91`
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
| Music scanning / playback | Production directory reader, asynchronous AudioService with managed Helix and real LVGL Music UI | 8 directory + 17 audio + 17 UI cases pass in Debug/ASan; physical SD/audio unverified |
| Media PNG / display allocation | PNG ownership/inflate headroom, Camera frame release, scoped screen JPEG PSRAM allocation and final ELF gate | 021 Display 30 / Home 43 / allocator 10 pass Debug/ASan; checker 26 and real ELF positive/bypass-negative checks pass. First-frame open DMA net loss is zero and screen-first Camera starts, but STREAMOFF stalls for 127.583 s before reset. Two post-reset A3 loads pass; final 8,192-byte internal largest does not close OOM/concurrency or soak |
| Other resource failures                     | Physical image/display coexistence, camera preview task, voice I/O task, MQTT bootstrap allocation hooks                               | Embedded validation pending                   |
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

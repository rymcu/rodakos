# OTA Release Readiness

The existing Home, voice, media, MQTT, WebRTC display, and signed-appearance functional gates are
accepted as completed where their evidence is recorded in the repository. This document tracks only
the remaining signed-firmware release, interruption, and resource-failure work. Passing software
tests does not close physical power-loss or full heap-exhaustion gates.

## Current evidence

Evidence review updated on 2026-10-07. The earlier source baseline `c64cf06` / `f7e8c91`
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
The latest media slice, including a bounded package-017 device run, is
[Camera exit and PNG allocation validation](#2026-10-07-camera-exit-and-png-allocation-validation).
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
The media allocation slice has 43 Home UI, 24 production `DisplayService`, 24 Photos/ImageLibrary,
21 Camera/FileService, and 11 valid plus 2 rejected-geometry LodePNG cases in Debug and ASan/UBSan
with leak detection. The complete 017 host runner passes 30 suites and 42 CTest cases plus Python
groups of 17, 15, 8 and 12 cases; key source hashes are unchanged before and after the run.
Its 7,108,864-byte firmware was packaged and flashed as development package 017. Two actual Camera
previews were followed by four successful A3 decodes. Screen-first Camera startup still failed DMA
allocation twice, and one successful device decode followed a desktop control timeout. The observed
131-byte historical minimum internal heap and final 7,680-byte largest block do not establish
sufficient headroom; the latter is below the 8 KiB soak threshold. Package 016's
clean-restart five PNG displays and 34/34 JPEG encodes remain accepted for their recorded window;
they do not replace these newer failure observations or the outstanding resource/soak gates.
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
| Command / stream / input lifecycle | Original-connection publication, bounded result cache, stream cleanup, real LVGL input grants and complete display ACK sender | 55 command, 15 input and 13 ACK host cases pass; 21 desktop cross-repository cases pass; hardware unverified |
| Voice identity / recovery | Single-record persistence, retained revision watermark, Unix/monotonic expiry, runtime recovery and proactive shadow reports | 277 app-model, 8 parser, 25 wake service, 6 frontend and 4 service integration cases pass; 4 desktop cross-repository cases pass; hardware unverified |
| Music scanning / playback | Production directory reader, asynchronous AudioService with managed Helix and real LVGL Music UI | 8 directory + 17 audio + 17 UI cases pass in Debug/ASan; physical SD/audio unverified |
| Media PNG / display allocation | Production PNG ownership, checked LodePNG inflate headroom, Camera final-frame release and stop publication, display capture/JPEG allocation | Software suites pass; 017 records four successful A3 decodes after two actual Camera previews. Screen-first DMA failures, a Retry control timeout, broader OOM/concurrency and soak remain open |
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

The current focused software gate, including the 017 extensions described below, passes in Debug
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

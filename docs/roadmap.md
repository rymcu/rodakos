# RodakOS Roadmap

Decision updated 2026-10-09: delivery proceeds through local tests, builds and recorded device
validation without depending on GitHub Actions. Do not run or repair Actions, including static
workflow changes, billing, quota or required-check setup. Existing runs remain
evidence for their original candidates; missing or unavailable Actions do not block delivery.
Actual software failures and the remaining physical and production-release gates still require
their own evidence.

Board-component migration on 2026-10-09 adopts `components/rodakos_hal_boards/`, retaining only
`boards/rymcu/rymcu_bigsmart/` and its local PCA9557 driver. The directory layout reserves
`boards/<vendor>/<board>/` for future ports; BigSmart remains the only supported build target.
Board Manager generation, runtime adapters, board/device identities and Recovery layout remain.
Local validation passed 4 board-generation tests, 55 existing generator tests and 11 Camera
overlay tests, followed by two-pass cold generation and an ESP-IDF 6.0.2 isolated build.
The 7,157,216-byte main image fits `ota_0`; board setup has one compilation owner and all three
factory symbols are linked. The final `sdkconfig` is byte-identical to the previous configuration.
No hardware was flashed for this migration; retain the existing long-soak candidate's evidence
identity and complete separate device acceptance. See [dependency maintenance](dependency-maintenance.md).

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

040 then completed a same-source TEST/OFF local build, static cost review and one USB-wake
priority snapshot. The four endpoints were `prepare_begin`, `open_acquired`, `cloud_returned`,
and `open_released`, with priority `4/4/4/4`, `flags=0`, and one task handle. WSS did not start
because `required_internal_stack=6144`; the endpoint record therefore does not prove priority
inheritance, resource headroom, or acoustic behavior. Ordinary OFF was restored with the original
binding/tokenVersion4 and MQTT connection. Root cause remains **INCONCLUSIVE** and resource/
production remain **NO_GO**. See [040 evidence](ota-release-readiness.md#2026-10-09-准备阶段优先级观察与-testoff-恢复-040).

After the 040 recovery, one ordinary-OFF USB diagnostic run completed six logical turns on the
same realtime voice session. Device evidence showed one session ID, six `input.start` markers,
follow-up counts 1—5, six non-empty playback-stat groups and zero playback write failures; the
session then stopped, cleaned up and re-armed wake monitoring. Local Electron events confirmed one
`session.open`, one `wake.detected`, six `input.start` events, disconnect and an empty runtime
session list. Server VAD split the same session into six `vad-end` files plus one short final
`listen-stop` file, so this is a bounded same-session lifecycle result rather than complete
server-segmentation, acoustic or production acceptance. See [six-turn evidence](ota-release-readiness.md#2026-10-09-六轮同-session-合成语音观察).

The subsequent 1,800.20-second ordinary-OFF release-soak observation produced 60 MQTT samples
and five application launches with 5/5 ACK and completion. The repaired collector now includes
Voice/Main/application resource minima and warning/error samples; offline replay found Voice
`internal_min=275 B`, an application `internal_largest=3584 B` and one `E:RX` line. The run was
below the 28,800-second release threshold, so resource and production remain **NO_GO**. Music
only reached app creation and a five-track scan; playback and coexistence are unverified. See
[soak evidence](ota-release-readiness.md#2026-10-09-资源采集器修复与-30-分钟有限观察).

The local follow-up host gate then passed Music UI 18/18, RecordingService 17/17 and the
voice-volume/focus suite 30/30 in Debug. The added Music case verifies that an asynchronous
library rescan publishes a new track list. These are software and host-fake results; SD removal,
DAC/ADC, touch, physical audio focus, Music/Recorder/Voice preemption and audible playback remain
hardware gates.

The formal 28,800-second capture on ordinary OFF `20261009-014905` was still running at the
2026-10-09 06:45:37 local snapshot: elapsed 8,490.5 seconds, `status=no-go`, `complete=false`.
It had already recorded warning/error lines and insufficient resource headroom; request/ACK/
completion counts of 28/28/28 do not establish successful app operation or resource release.
This is an interim failure observation, not a completed eight-hour result. Wait for the existing
capture to exit and COM3 to be released before finalizing evidence or opening a new device window.
See [interim evidence](ota-release-readiness.md#2026-10-09-正式八小时采集中间快照).

The formal-soak interim log exposed a DVP cleanup failure during a Camera → Home transition:
STREAMOFF returned, but SCCB/I2C removal and DVP deinit reported errors before the app returned
Home. RodakOS `29aaacd` retains the board handle and I2C reference across failed deinit, and
`4e08efa` propagates release errors so `CameraDevice::Acquire()` retries cleanup before a new
initialization, and `51927b0` makes `CloseStream()` report deferred cleanup instead of false
completion. The capture package predates these fixes; a fresh repeated Camera window is
required. `cc32989` now includes outer error propagation, a pinned DVP
teardown overlay and board-peripheral retry coverage. Debug/ASan host checks cover the
Camera wrapper 4/4 and DVP teardown 13/13; the capture source-contract suite remains 10/10.
A separate IDF 6.0.2 RCC overlay fixes the DVP deinit acquire/release mismatch and passes
six Debug and six ASan/UBSan CTests for single, repeated and shared-owner lifecycles. These
are software/host boundaries; hardware Camera validation remains open.
Earlier full-chain review found that the outer `dev_camera_deinit()` swallowed subtype errors
and that partial SDK teardown could leave a registered video device pointing at a freed sensor;
`cc32989` addresses those paths in source and host failure contracts. The result still does not
establish safe physical retries until the candidate is exercised on the device.

Candidate 043 has now been built locally with ESP-IDF 6.0.2 from `abecfb2`, paired with Rodak
`474b2e85`, and its development-signed package `20261009-063127` passes `ota_security.py
verify-package`. Its task is `network-camera-rcc-043`; main image SHA-256 begins `ddc7927c`.
The package includes GOT_IP refresh and the Camera/RCC corrections but has not been flashed.
Device `flash_and_test.ps1 -VerifyOnly` has not run because it resets the device; it must wait for
the current capture and COM3 ownership to end. Offline package verification does not establish an
installed immutable-Recovery match or hardware acceptance. See [043 package identity](ota-release-readiness.md#2026-10-09-网络与-camera-修复候选-043).

The same ordinary-OFF package also passed one bounded follow-up-silence run: after about 28 seconds
of silence, a replay entered a second reply, and the next 30-second follow-up window timed out before
stop/cleanup and wake re-arm. The device and IPC evidence stayed within one session and showed no
reset, panic, watchdog or transport failure. This closes only the synthetic follow-up silence gate;
music/Recorder coexistence, repeated-wake suppression, TTS tail, AEC/barge-in, physical acoustics,
resource headroom and long-duration stability remain open. See [follow-up evidence](ota-release-readiness.md#2026-10-09-follow-up-silence-与超时观察).

A separate one-cycle ordinary-OFF observation restored physical microphones during playback and
completed without VAD interruption, runtime/transport failure, or cleanup loss. This is bounded
microphone/full-duplex path evidence only; it does not close acoustic AEC/barge-in, music/Recorder
coexistence, or long-duration gates. See [microphone evidence](ota-release-readiness.md#2026-10-09-播放期间恢复实体麦克风的有界观察).

A one-cycle synthetic barge-in observation also produced `playback.abort(reason=vad_detected)` with
one AFE VAD confirmation and one VAD end, then completed cleanup without runtime or transport
failure. This is a protocol/device-path result only; real acoustic AEC and coexistence gates remain
open. See [barge-in evidence](ota-release-readiness.md#2026-10-09-合成-barge-in-与播放中断观察).

030 software now moves five video workers to bounded, generation-owned external WithCaps
retirement and replaces serial/Camera Home async admission with a precreated four-request
PSRAM queue and LVGL timer. Local production-source tests, pinned real-IDF exit controls,
real-LVGL navigation tests and ESP-IDF 6.0.2 build pass. Source `34c9e6453344b8eb7896ab5476ef222504c12a94`
was installed as development-signed package `20261008-055334` through the NVS-preserving
flow. Closed normal/matrix windows record seven correlated `stopped` receipts, all four
local-Camera/remote-Display order cells and one remote-pointer Camera Home action. Nine serial
requests each have admission and completion evidence; all Stops have more than 60 seconds of
raw serial coverage. Camera quality and physical GT911 touch are not accepted. The same-boot
internal minimum is 359 B; resource/production **NO_GO** and the unproven cause of 028's Home
rejection remain. Its three voice self-delete paths are addressed by the later 031 source;
032 carries that same implementation in separately identified test and ordinary packages. See [030 evidence](ota-release-readiness.md#2026-10-08-video-task-retirement-and-navigation-030)
and [task retirement and deferred navigation](task-retirement.md). The following 029/028 records
remain separate historical evidence.

029 source `83cab8021c265ed162d9bbf827b59299dbdfc546` fixes one confirmed AES DMA
allocation-failure cleanup defect through a pinned build overlay. If an input bounce buffer
has been allocated and the output bounce allocation fails, existing cleanup now releases the
input buffer while retaining output zeroization and the original error. Debug, Release and
ASan/UBSan checks pass with an explicit complete-upstream-source red control and unchanged
normal-path traces. The original-development-root package `20261008-040342` has passed
source/signature/ZIP/immutable-image/final-ELF verification; its main image is 7,139,584 B.
This does not reduce normal bounce-buffer peak demand or establish that 027 hit this branch.
The guarded NVS-preserving deployment passes Recovery/main/OTA/Home and a closed 70.039-second
cold baseline. Independent review seals one normal Camera and one normal Display smoke:
320×240 images, two correlated `stopped` results and more than 60 seconds of captured serial
after each Stop ACK. Camera is nearly dark; Display shows Home. Retain 44 warning/error lines;
there is no captured panic/reset/MQTT disconnect or raw `E:RX` in this smoke. Original
bound/token4 and MQTT online remain; the final UI has no image and control is disabled.
The new boot's internal minimum is 547 B; final Main internal free/largest is 16,515/7,680 B
and DMA 14,419/7,680 B. These bounded observations do not exercise the AES allocation-failure
branch, prove sufficient headroom or compare with 028's 555 B across boots. See
[029 evidence](ota-release-readiness.md#2026-10-08-aes-dma-allocation-cleanup-029).

The separately named 029 startup-order experiment still ran installed **028** firmware.
Three local-Camera/remote-Display order combinations pass their finite checks; the fourth,
Display-first/Home-first, returns `home queued:false` and leaves Camera running. A single
Home retry succeeds after Display stops; it does not replace the failure. Two desktop remote
stream exclusion checks pass without exercising simultaneous remote peers or firmware busy
ACKs. The same-boot internal minimum remains 555 B. See the
[startup-order record](https://github.com/rymcu/rodak/blob/master/docs/video-startup-order-verification.md).

The preceding 028 source `6666a3e52e50ddab989019517f237b2633e7c168` was installed as the original-root
package `20261008-021356`. It cooperatively parks the DVP worker before owner-side WithCaps
deletion and moves only its 3,072 B stack payload to PSRAM. Targeted host/negative controls,
IDF build, signature/source/final-ELF checks and the NVS-preserving Recovery/main/OTA/Home boot
pass. The 70-second cold-baseline capture is closed; fresh device state retains the original
ID, bound/tokenVersion=4 and connected MQTT.

Five normal Camera and five normal Display Start/image/Stop cycles are now independently
sealed on that boot: nine `stopped` and Display03 `already_stopped`, with at least 60 seconds
of observation after each confirmed Stop. The first Camera image is dark and only Display01
has a visual Home review; decoded/static images do not establish motion or camera quality.
Warnings and three raw Camera `E:RX` records remain. No reset/panic/MQTT disconnect is captured,
but the boot-cumulative internal minimum reaches **555 B**, first reported in Camera02's quiet
window after Camera01 had recorded 563 B. This does not identify the low point's time or cause.
Final sampled Main internal free/largest is 15,491/7,680 B, DMA 14,387/7,680 B. Resource and
release decisions remain **NO_GO**; these finite cycles do not prove concurrency, IRQ/cache-off
behavior, no leak or eight-hour soak. Static changes also shift the internal heap start by
256 B and grow the controller by 4 B, so moving a 3 KiB stack is not a measured net heap gain.
See [028 evidence](ota-release-readiness.md#2026-10-08-cooperative-dvp-worker-validation-028).

027 source `3ff55ab7ec3d46cd7a1e2c41fca5d700505c0a06` sets the public Camera/Display
candidate capacity to 32 and adds bounded heap samples. Its earlier package `20261008-004814` was installed
with the same immutable Recovery, development root, NVS, binding and token version 4. Normal
Display matches all 18 native candidate endpoints to SDK Add records, receives a real Home image
and confirms Stop; its selected path is candidate 4. Separate diagnostic SDP/trickle windows
then verify 18/32 admissions, the 33rd input and full-table duplicate limit, plus a working path
at overall candidate 14 after ten owned sinks. These are bounded Display inputs, not Camera or
normal five-cycle video acceptance; SDK over-limit still receives a software ACK.
An independent Camera window fails before any remote frame: DMA free/largest reaches 943/832 B,
then AES allocation fails and MQTT disconnects. Fault DRAM records prove ioctl returned 0 and
execution reached the following log boundary; the lock owner is not directly known. The debugger
then enters a separate panic path. Official USB reset recovers the same 027 main/MQTT after an
unsuccessful direct RTS attempt; all windows are preserved. See
[027 evidence](ota-release-readiness.md#2026-10-08-candidate-capacity-and-camera-failure-027) and
[Camera teardown diagnostics](camera-teardown-diagnostics.md). The later 028 finite cycles
do not rewrite the 027 failure or establish complete resource recovery.

026 source `cc776c1d007abe7e415ce9b386dea887eefe304e` adds exact-instance Stop confirmation
using the original Start command number. A successfully started instance is remembered only
after its native Stop returns; `stopped` and `already_stopped` are software results, while unknown
or stale instances still fail. Cached successes retain their original MQTT generation, epoch and
nonce. Original-root package `20261007-231703` has passed the NVS-preserving flash and limited
Display/Camera Stop windows with the existing binding/token version 4. A desktop automatic-terminal
display omission was found and fixed in a separate renderer window. Camera's short preview lowered
the boot-cumulative internal minimum to 331 B: resource, candidate-capacity and release gates remain
open. The six separate windows include the original automatic-terminal UI failure, a corrected
renderer rerun, and an outage where the old Stop remains unknown before a new session receives
`stopped`. Targeted MQTT and four shared-consumer Debug/ASan checks pass; they are not a new
38-suite full run. The final outage capture continues 483.903 seconds after Home completes,
without closing the resource or eight-hour soak gates. See
[026 evidence](ota-release-readiness.md#2026-10-07-exact-stream-stop-026) and the
[Stop contract](rodak-aiot-contract-v1.md#exact-stream-stop-026).

025 source `8d5cf99f95b59d45b0a1e66fc3a02d1a502618a6` implements complete SDK-client
replacement for automatic MQTT credential refresh within the existing authority boundary.
The worker revokes the old connection, confirms stop/destroy, reloads current credentials
and attaches a new generation through an atomic Cloud credential check. Late old-generation
rejections are coalesced; a new-generation rejection remains actionable. Changed authority
and unconfirmed SDK stop retain conservative restart isolation. See
[MQTT credential refresh](mqtt-credential-refresh.md). Package `20261007-200215` has passed
NVS-preserving flash and boot confirmation. The device then merges a second old-generation
rejection during HTTP, confirms generation 1 stop/destroy and connects generation 3 without
restarting; uptime, binding and tokenVersion=4 remain. Separate short/65-second outages and
screen/peer cleanup retain their own results, including the late Stop not-found receipt.
Fresh local checks pass 38 suites / 98 CTests, Python 107 and helper 21. See
[025 evidence](ota-release-readiness.md#2026-10-07-mqtt-credential-client-replacement-025).

The preceding 024 evidence remains independent. It distinguishes MQTT allocation/queue/count/byte failures and the production desktop uses
ACK-paced, nonpersistent signaling. Its final window passes 19/19 ACKs and a 320×240 frame,
then observes Home for 98.936 s. Eight SDK remote-candidate-limit messages remain; ACKs do not
prove all candidates were accepted. The two credential-recovery restart windows are preserved
as failures. See [024 evidence](ota-release-readiness.md#2026-10-07-mqtt-queue-diagnostics-and-pacing-comparison-024).

The tested static Photos first-frame regression was closed in
[023](ota-release-readiness.md#2026-10-07-static-screen-first-frame-validation-023).
The [021 STREAMOFF stall](ota-release-readiness.md#2026-10-07-scoped-screen-jpeg-psram-validation-021),
late control, resource recovery, actual SD/touch/audio, production-root, power-cut and eight-hour
soak gates remain **NO_GO**. [022 Camera diagnostics](camera-teardown-diagnostics.md) and the
repaired MI02 JTAG interface do not close the stall; the debugger-perturbed panic window is
not evidence of healthy recovery. Earlier packages retain their own identities and bounded results.

This is the active work list. Completed implementation details live in
[architecture](architecture.md) and the linked feature documents. The former Milestone 0–7
plan is retained in the [documentation archive](archive/README.md).

**Implemented** means the source path exists; **host-verified** means a recorded software test
exercised it; **hardware-verified** requires a recorded device or fixture run. A new host test or
firmware build does not change an existing hardware gate.

## Current baseline

- USB-installed server identity, pinned HTTPS/MQTTS/WSS, bounded DNS-SD candidate
  verification and atomic endpoint promotion are implemented and host-verified.
  No automatic re-pairing or plaintext fallback is permitted after pin installation.
  Package `20261007-010516` passed two trusted USB refreshes and both SRV port
  migrations on COM3 with the existing binding/token, wake listening and no crypto
  allocation errors. Port recovery still includes a controlled device restart.
  A separate same-name/different-key HTTPS listener was rejected before any HTTP
  request, followed by recovery to the genuine server. Additional 006 evidence
  covers a real 137/88 subnet round trip selected through USB WiFi configuration,
  retained binding/token, and two WSS silent connect/stop sessions on the 88 subnet;
  see the [Rodak verification record](https://github.com/rymcu/rodak/blob/master/docs/trusted-network-verification.md).
  That 006 evidence does not establish unknown-SSID roaming, automatic AP-loss recovery, speech
  or extended soak.
  Package 009 also passed a same-SRV-port two-address gate: an unreachable first
  candidate failed, the genuine second candidate authenticated, and numeric MQTTS
  and WSS routes remained usable after a controlled restart. This was triggered
  by normal port migration, not a new server IP or single-interface roam.
  Package 007 failed its first USB NVS
  write gate; the diagnostic 008
  passed the bounded USB/port/telemetry gate without reproducing that error.
  Compact authority v3 shares one trust object and retains v1/v2 read compatibility.
  Its 009 package passed two USB refreshes, bidirectional port recovery and a
  60-second stability window with the original binding/token and no allocation error.
  A separate 45-second Windows hotspot outage recovered WiFi/MQTTS and two fresh
  telemetry reports without USB provisioning or a device restart. Unknown SSIDs,
  AP isolation and extended resource/soak acceptance remain open.
  Two 009 synthetic-silence WSS sessions passed with MQTT coexistence and wake
  recovery, but internal heap reached a reported historical minimum of 863 bytes.
  This does not establish sufficient headroom for real audio/concurrent load.
  Version-1-only 006 and version-2-only 007/008 cannot read newer records; recovery
  must preserve NVS using a package supporting the stored version. Damaged-trust recovery and
  physical power-cut acceptance are still open;
  see [trusted server discovery](trusted-server-discovery.md).
  The latest strict USB/port/60-second network gate was package 010 (`20261007-033823`,
  source `554d05f`): both trusted USB rounds, bidirectional HTTPS/MQTTS port recovery,
  fresh shadow/telemetry and wake health passed with the original binding/token.
  `not_needed` describes the binding result, not whether configuration or trust records were written.
  No allocation failure was detected in that network window; a later display/UI run did fail to
  create a cloud-refresh task. Package 011 was subsequently flashed preserving NVS, but its UI/WSS
  checks do not repeat or replace 010's full bounded network gate.

- ESP32-S3 BigSmart: 16 MiB flash, 8 MiB PSRAM, ESP-IDF 6.0.2, LVGL 9.3,
  `esp_lvgl_port` 2.8, local Board Manager definitions and pinned component resolution.
- Static native app registry/host/navigation; Shell-owned Lock Screen and Control Center;
  exact-ID Home layouts, folders, one-save Arrange drafts, and active-plus-neighbors page residency.
- On-demand media hardware, SD/USB MSC, local MultiNet wake, canonical
  `rodak-realtime-voice/v1`, MQTT provisioning/credential refresh, and camera/display WebRTC peers.
- Assistant and Device Cloud now share fixed, typed cloud diagnostics and a two-step Settings
  recovery route. Bound devices can retry expired/rejected credentials or unavailable voice without
  unbinding. Package 011 passed controlled single-request HTTP 401 injection, actual device frames
  showing the corrected failure/disabled-wake text, a first-attempt Settings retry, wake off/on,
  and two WSS silence sessions after the display stream stopped. These results preserve the original
  binding and do not establish real credential expiry/revocation, physical touch or human speech.
  The UI suite has 15-case LVGL host coverage, including persistent task-start/delivery failures;
  011 did not reproduce 010's task-creation failure. Real DNS/HTTP cancellation latency,
  device typography/touch and resource pressure remain open
  under [#24](https://github.com/rymcu/rodakos/issues/24); see [cloud diagnosis](voice-assistant.md#device-cloud-diagnosis-and-recovery).
- Signed appearance packages have recorded COM3 revision 14 and next-boot trial evidence;
  see [appearance verification](appearance-verification.md). Fresh physical publisher/origin trust,
  power interruption, slow-card/fallback and peak-memory acceptance remain separate gates.
- Audio volume changes retain the previous service/UI cache if the codec API reports a failed write.
  A closed codec accepts configuration without opening hardware; the next open applies it and
  fails with cleanup if the initial API call fails. Voice MCP now exposes absolute/up/down tools
  with atomic shared configuration and bounded per-session duplicate suppression. Its versioned
  software receipts carry optional effect correlation; MQTT shadow values still do not correlate
  effects, and neither path proves physical speaker output. See [voice volume MCP](voice-volume-mcp.md).
  Single-dispatch MQTT volume effects now use their own correlated result topic, bounded
  same-authority ledger and transport-epoch cancellation; see [MQTT volume effects](mqtt-volume-effects.md).
  The pinned codec dependency now uses a source-verified build overlay to propagate lower-level
  volume driver errors and commit its own cache only on success; see
  [dependency maintenance](dependency-maintenance.md).
- Release-soak collection requires increasing device uptime and both queued and successful
  app-launch completion evidence. Its 17 Python regression tests pass; the eight-hour device
  gate remains open. See [OTA release readiness](ota-release-readiness.md).
- Native RGB light patches now share atomic local/MQTT application, failure-retained configuration,
  discovered identity and correlated software receipts. Recent-64 retention with an authority-wide
  version watermark supports continuous updates without repeating evicted effects. This does not
  add a backlight MCP capability; see [MQTT light effects](mqtt-light-effects.md).
- Rodak's MCP manager now shares the three native audio tools and software receipts with voice
  sessions. The host adds owner/nonce deduplication and a lease on the existing authenticated
  connection, without adding a firmware tool or restarting voice interaction. Five voice and
  eleven manual cross-repository scenarios exercise the production payload dispatcher with SDK
  fakes; full service/envelope coverage remains a separate host target. This host-only change
  does not rebuild firmware or add hardware evidence; see [manual MCP entry](voice-volume-mcp.md#desktop-manual-entry).
- Ordinary command ACKs have desktop failure classification and first-terminal-result freezing.
  Firmware ACKs and camera/display signal/state now retain the original client generation, epoch
  and topic, then use a bounded SDK-event queue and direct QoS 0 publish without an outbox entry.
  Epoch changes discard old results; the host SDK model separates enqueue from wire publication.
  Stream operations now use revocable instance leases and serialized cleanup; remote input checks
  its stream and enable grant at final LVGL execution, and delayed ACKs retain the peer instance.
  Package 018 adds original-peer FIFO ACK retries for temporary send/allocation pressure, bounded
  by one second from first send attempt or 50 attempts, plus explicit cancellation replies.
  Real-LVGL input 20 and complete ACK 21 cases pass Debug/sanitizers; device replies can still arrive
  after the desktop's unchanged three-second timeout. Cancelled pointer-release action semantics
  are corrected in 019 by a cancellation generation and explicit LVGL reset/release before new input;
  30 real-LVGL input cases pass Debug/ASan, with ACK 21 / Home 43 ASan checks. Device held-down
  cancellation produced no extra load, and a reenabled click worked; a later pressure click still
  timed out before the parsed-input entry. Accepted samples do not establish completed business clicks.
  A volatile latest-64 command cache replays final ACKs and rejects raw-payload conflicts. Eviction
  and device restart remain outside its deduplication guarantee. See the
  [command contract](rodak-aiot-contract-v1.md#command-results-and-replay-boundary),
  [host target](../tests/mqtt_volume_service/README.md#independent-command-fixture-and-tests)
  and [dated software evidence](ota-release-readiness.md#2026-10-06-stream-lifecycle-validation).
- Voice identity now retains a separate accepted revision watermark, a single versioned NVS record,
  Unix expiry with monotonic lifetime limits, and explicit runtime/storage recovery failure.
  Pending or unconfirmed identities cannot enter the desktop Base Prompt; expiry reports are
  emitted proactively. Disabled state reads preserve on-demand audio/model behavior. See
  [identity contract](voice-identity-wake-word.md) for migration and evidence limits.

- Music now separates library/storage failures from an empty library, provides worker-backed rescan,
  and preserves asynchronous playback errors. WAV/MP3 parsing rejects premature completion, and
  failed resume releases the paused worker for retry. See [music playback](music-playback.md) for
  software boundaries and the still-open physical SD/audio gates.
- Photos retains one successful LodePNG decode as an owned ARGB8888 image, avoiding a second
  draw-time decode. The checked LVGL 9.3.0 overlay now adopts the decompression allocation for
  eligible non-interlaced RGBA8 images, while keeping exact 11-file provenance and fail-closed
  component/target checks. `DisplayService` allocation, in-place RGB conversion, bounded JPEG
  output, callback and stop/recovery paths have 30 focused host cases; Photos/ImageLibrary has 24,
  Home UI has 43, and the overlay now has 11 valid variants plus 2 geometry rejections. Camera capture
  has 33 focused cases, including final-owner frame release, unexpected dequeue failure, concurrent
  snapshots, selected allocation failures and publishing stop only after the worker's final service access. Package 013's
  `A3.PNG` Retry abort was located in the concurrent DisplayService JPEG path on `std::bad_alloc`;
  package 014 returned error 83 for first open and two Retries without reboot. Package 015 displayed
  all three PNG attempts but the old JPEG peak then failed sustained screen encoding. Package 016
  removes the 153,600-byte worker copy, uses one 230,400-byte in-place RGB buffer and a 100 KiB
  output scratch. After a clean restart, all five PNG decodes and all 34 JPEG attempts succeeded
  without abort, panic or reboot. Package 017 releases the final CameraService frame and reserves
  the Huffman inflater's 260-byte tail headroom before decoding a known-size PNG. The same-size
  synthetic fixture's largest request drops from 1,196,213 to 797,615 bytes; the old code fails a
  1,081,344-byte allocation budget while the corrected code succeeds and releases all tracked allocations.
  On COM3, two actual Camera previews ran for 32.304 s / 467 frames and 21.960 s / 332 frames;
  after exiting them, four A3 decodes succeeded in 201/203/191/185 ms. Starting Camera while the
  screen stream was already active still failed DMA allocation twice. One Retry had a desktop
  control timeout despite later device decode success, so remote ACK acceptance remains open.
  Package 018 adds Camera allocation cleanup and JPEG heap-phase diagnostics, without changing
  the JPEG allocator. Its first Camera exit stalled without a captured panic and required reset.
  The separate post-reset Camera preview ran 18.080 s / 243 frames and A3 decoded three times in
  193/190/192 ms, but one release input was rejected and down/disable ACKs arrived 7.745/4.741 s late.
  First-frame open/close snapshots identify about 8,084 bytes of temporary internal JPEG allocation.
  Package 019 adds Camera exit phase logs (Camera 33 Debug) and fixes cancelled gestures at the LVGL
  boundary; it has no JPEG allocator migration and does not claim to fix the earlier Camera stall.
  Screen-first Camera still failed at a 6,656-byte largest DMA block; after stopping screen sharing,
  it ran 56.779 s / 792 frames and all recorded exit stages completed. Two A3 loads took 189/190 ms;
  cancellation trials did not produce extra loads, while a later pressure click still timed out.
  Package 021 adds PSRAM-only allocation within `EncodeJpeg`, retaining the original policy
  outside that task-local scope and keeping the codec single-task. Display 30, Home 43 and
  allocator 10 cases pass Debug/ASan/UBSan/leak; 26 checker tests and real final-ELF positive/
  bypass-negative checks pass. Native TLS stays at 32 aligned bytes per task (delta 0).
  On hardware, the first JPEG open has zero net DMA-free decrease and screen-first Camera
  starts, but its STREAMOFF stalls and requires reset. Two post-reset A3 loads take 188/187 ms;
  late rejected inputs and a failed post-reenable Retry keep resource and input gates open.
  See [media browsing](media-browsing.md) and
  [dependency maintenance](dependency-maintenance.md#lvgl-lodepng-decode-overlay).
- Recorder and Web upload hold path-scoped leases through writes and cleanup; Camera uses the
  FileService I/O lock. Photos and recordings use exclusive creation. Final WAV/header/flush/close
  failures remain errors; save completion, list refresh and UI teardown have separate states.
  Recorder retry/library-error UI, Camera result delivery and upload conflicts have host tests.
  See [media save](media-save.md) and [dated evidence](ota-release-readiness.md#2026-10-06-media-save-validation).

## Remaining acceptance and implementation work

| Area | Remaining work | Evidence / owner document |
| --- | --- | --- |
| Trusted server recovery | New-server-address/single-interface roaming, stale DNS caches, AP isolation, unknown SSIDs, non-scoped IPv6 and wider WSS Host compatibility; scoped IPv6 is unsupported. 009 passed bounded USB/port recovery, a 45-second known-hotspot outage and same-port unreachable→genuine address selection with numeric MQTTS/WSS after restart. Wrong-certificate/replay/expiry candidate variants, broader storage failures, damaged/missing-trust recovery and physical power cuts remain open. Preserve the stored authority version and Appearance publisher/origin confirmation | [Trusted server discovery](trusted-server-discovery.md), [RodakOS #33](https://github.com/rymcu/rodakos/issues/33) |
| Signed firmware release | Production trust root and Rodak signed manifest, wired immutable-Recovery migration, actual power cuts, eight-hour identified-build soak | [OTA release readiness](ota-release-readiness.md) |
| Resource recovery | 040 added a TEST-only 200 B prepare observer and captured four priority endpoints once; WSS failed the 6144-byte internal stack allocation, so PI, resource headroom, acoustic behavior and soak remain open. 039 same-boot normal/quiet/normal remains bounded evidence. The later 30-minute ordinary-OFF smoke reports Voice `internal_min=275 B` and application `internal_largest=3584 B`; its 5/5 app ACK/completions do not close headroom or the eight-hour gate. The formal-soak interim log also exposed Camera DVP/SCCB cleanup errors. Candidate 043 includes `abecfb2` and its prerequisite failure-retention, outer-propagation, phased-cleanup and RCC fixes; local build/package verification pass, device verification and flash remain pending. Root cause INCONCLUSIVE; resource/production NO_GO | [RodakOS #28](https://github.com/rymcu/rodakos/issues/28), [043 package](ota-release-readiness.md#2026-10-09-网络与-camera-修复候选-043), [040 evidence](ota-release-readiness.md#2026-10-09-准备阶段优先级观察与-testoff-恢复-040), [voice contract](voice-task-retirement.md) |
| Home and Shell | Physical bidirectional swipes, Arrange, page restoration, touch/readability, Shell settings/buttons, three-page turnover using the isolated 25-app flavor | [Home validation](home-layout-design.md#validation-boundary), [hardware flavor workflow](firmware-download.md#three-page-home-hardware-gate) |
| Voice | 040 provides one synthetic USB-wake endpoint snapshot. Ordinary OFF now has bounded synthetic evidence for six same-session turns, delayed follow-up/silence timeout, live-mic playback, and one VAD barge-in abort; server VAD segmentation remains an explicit boundary. Music resume, Recorder preemption, repeated wake suppression, TTS tail, real acoustic AEC/barge-in, false accept/reject, idle CPU, heap/PSRAM and long-duration measurements remain open | [Voice verification](voice-assistant.md#verification-gates), [six-turn evidence](ota-release-readiness.md#2026-10-09-六轮同-session-合成语音观察), [follow-up evidence](ota-release-readiness.md#2026-10-09-follow-up-silence-与超时观察), [barge-in evidence](ota-release-readiness.md#2026-10-09-合成-barge-in-与播放中断观察), [AEC integration](voice-aec-integration.md) |
| Voice transport | Remaining terminal-error, stale-audio, and stop/deinitialization cancellation fault injection after recorded bounded reconnect/retry exhaustion | [Voice assistant](voice-assistant.md) |
| Voice identity | Identified-firmware NVS power-cut/reboot acceptance, actual clock synchronization, model recovery under resource pressure, wake recognition across speakers/distances/noise and two-device identity isolation | [Identity validation](voice-identity-wake-word.md#validation-gates) |
| Audio | Codec startup/shutdown and other API failure recovery, hardware volume failure/retry and audible output checks; MQTT/MCP receipts prove only volatile software configuration, and remaining mutations need separate contracts | [Volume MCP](voice-volume-mcp.md), [MQTT volume effects](mqtt-volume-effects.md), [dependency correction](dependency-maintenance.md) |
| Media/storage | 023 passes two static Photos first-frame sessions with a 2.5 s offer delay and no extra source frame. Broader low-memory/network first-frame recovery remains open. Preserve 022's finite Camera exits and 021's A3 decode/cancellation evidence and later timed-out Retry; these are different gates. SD removal/slow cards, physical touch, arbitrary OOM, concurrency and soak remain open | [Media browsing](media-browsing.md), [023 evidence](ota-release-readiness.md#2026-10-07-static-screen-first-frame-validation-023), [RodakOS #28](https://github.com/rymcu/rodakos/issues/28), [Media save](media-save.md) |
| RGB light | Board driver failures, physical output and recovery on the identified firmware; host receipts remain volatile software evidence. Backlight, voice identity, media and OTA require their own mutation contracts | [MQTT light effects](mqtt-light-effects.md) |
| Ordinary commands | 021 retains the 019 cancelled-gesture correction: held down128 is accepted, up129 is rejected after cancellation, with no extra PNG load. Reenable131 does not recover the next Retry: down132/up133 still arrive after 3.197/3.193 s and are rejected. 020 had independently isolated 7.9-second delays before application callbacks. UDP/SCTP delivery, abnormal stream cleanup, reconnect/resource contention and GT911 physical touch remain open; keep timeout, grants and reliable ordering | [Command boundary](rodak-aiot-contract-v1.md#command-results-and-replay-boundary), [021 evidence](ota-release-readiness.md#2026-10-07-scoped-screen-jpeg-psram-validation-021), [020 evidence](ota-release-readiness.md#2026-10-07-peer-timing-diagnostics-020) |
| Board telemetry | Validate battery/charging readings on hardware, plus I2C/SD/memory-pressure diagnostics | [AIoT device properties](rodak-aiot-contract-v1.md#5-shadow-state-and-device-properties) |

Already recorded COM3 voice, provisioning, WebRTC, and appearance gates remain accepted within
their documented limits. Ordinary regression reruns must not be presented as new production-key,
power-loss, acoustic, or resource-exhaustion evidence.

## Deferred design decisions

- Keep free drag deferred until physical paging and touch are proven together.
- Extend host LVGL coverage into PhoneSystem policy when hardware dependencies can be isolated.
- Refine service-backed status, app capability visibility in Settings/System Info, and consistent
  back/home transitions. Decide the preferred capture location between `/photos` and `/DCIM`.
- Native capabilities remain descriptive metadata. Any future untrusted MiniApp runtime needs
  a separate capability broker, per-app storage, resource limits, authenticated transport and
  signed staged installation. See [OpenOS comparison](openos-comparison.md).
- Swipe unlock remains a privacy cover until PIN, encrypted storage, Secure Boot and Flash
  Encryption policies are implemented.
- Consider Board Manager IMU metadata only when a first-class device type exists.

## Maintenance rules

Use [firmware download](firmware-download.md) for the supported build/package/flash flow.
Keep the Recovery partition layout and generated Board Manager ownership intact. Record current
build size/hash with its source baseline in the evidence document; preserve older device-package
identities as dated evidence. Test-only populations and fault-injection flavors must be disabled
before normal device use.

Retain typed cloud diagnosis and the non-voice serial/Device Cloud provisioning gate as regression
checks. Complete their physical recovery/readability gates; prior provisioning acceptance does not
establish the new recovery UI or immediate cancellation of active DNS/HTTP calls.

Runtime binary plug-in loading, execution of application images directly from SD, and a parallel
hand-written board layer remain outside the current scope.

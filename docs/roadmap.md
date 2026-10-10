# RodakOS Roadmap

Decision updated 2026-10-10: delivery proceeds through local tests, builds and recorded device
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

Camera/DVP follow-up on 2026-10-09 remains **NO_GO**, but candidate 045 closes the 044 stop-time
partial-frame symptom. The completed eight-hour window `20261009-014905` remains historical
NO_GO with 7/16 Camera requests succeeding and nine DVP DMA allocation failures. Source `85538b0`
publishes the stop flag and FSM under the controller spinlock, stops the controller before the
sensor, and prevents queued/in-flight stop events from reporting data errors, invoking callbacks,
restarting capture or reopening VSYNC. Normal non-stop partial frames still report errors.
Camera capture 7/7, teardown 33/33, worker 17/17 plus seven source negatives, ESP-IDF 6.0.2 build,
and final ELF/JPEG audits passed, including ASan/UBSan coverage.

Development candidate 045 (`20261009-193618`, task `camera-stream-stop-045`) passed package
verification, COM3 VerifyOnly and an NVS-preserving `otadata + ota_0` refresh. The original ID,
`bound`, token version 4 and MQTT connectivity remain. Three independent Camera → Home windows each
produced a software first frame, all six close markers and 65.000–65.094 seconds of fresh
MQTT/Main/Voice health with Voice listening. All three logs contain zero `E:RX`. Qualification soak
did not start because each application window still fell to a 4,096-byte largest internal block and
the health windows reported a 7,680-byte DMA largest block. The 6,144/4,096 fallback, physical image,
arbitrary OOM, complete resource margin and long-duration stability remain unproved.

Candidate 046 changes the non-JPEG DVP ring policy from 8,192-first fallback to a
6,144-byte preferred ring with a 4,096-byte fallback, while keeping the IDF-valid 8,192-byte
configuration ceiling and retaining configured-first behavior for JPEG. For 320×240 RGB565 this
reduces the normal actual ring from 7,680 to 6,144 bytes and raises receive events per frame from
40 to 50; both normal sizes still use one descriptor per half. Generated-function allocation,
fallback and leak tests bring Camera teardown to 38/38; worker 17/17 plus seven source negatives,
Camera capture 7/7, device lifecycle 4/4, DVP deinit 13/13 and DVP RCC 6/6 pass, including Debug and
ASan/UBSan on the changed suites. ESP-IDF 6.0.2 build and final ELF/JPEG audits pass. The 7,158,464-
byte image SHA-256 is `fe29de1ef0410876bccdb34dfcc4584cf791f7a4facf3758bea407dcdfcccd9d`.
Development package `20261009-202156` (`camera-dma-headroom-046`) passed verification, COM3
VerifyOnly and the NVS-preserving refresh. One 65.093-second window plus five bounded cycles each
selected an actual 6,144-byte ring, produced a software first frame and all six close markers, with
zero `E:RX`, overflow, DQBUF or error logs. The five-cycle heap median drop is zero. Release remains
**NO_GO** because application largest blocks were 4,352/5,120 bytes and health DMA largest remained
6,144 bytes; 4,096 fallback, sufficient headroom, physical image and qualification soak remain open.

Candidate 047 (`4bf341f`) adds a default-OFF, package-guarded Camera-only fault switch and verifies
the 4,096-byte branch on COM3. Development fault package `20261009-205201`
(`camera-dma-fallback-047`) passed fault-aware signature verification, VerifyOnly and an
NVS-preserving refresh. One single-serial Camera → Home window emitted the fault marker, selected
`4096/2048/1`, produced a software first frame and all six close markers, then retained fresh
MQTT/Main/Voice health for 65.000 seconds with zero `E:RX`, overflow, DQBUF or ESP error logs.
Release remains **NO_GO**: application largest was 6,400 bytes, health DMA largest 6,656 bytes,
Voice supervisor stack minimum 2,388 bytes and internal historical minimum 2,123 bytes. Physical
image, sufficient margin, arbitrary OOM, complete resource recovery and qualification soak remain
open. The device was restored to ordinary 046 with the original ID, binding, token version 4 and
MQTT connectivity; the workspace switch is OFF and the ordinary image contains no fault marker.

Candidates 048–050 (`d988c95`, `686d9bb`, `d0ce53b`) move Camera pause ahead of candidate
`OnCreate`, release Camera UI/timers/pixels, stop DVP and release audio focus before Home creation,
and restore Camera on replacement rollback. Candidate 051 (`ba58fe4`) localized the first largest-
block drop to Home page 1. Candidates 052–055 (`fd975c6`, `1d20a4b`, `65de5bc`, `9312e1e`) merged
tile callbacks, moved tile payloads to PSRAM and removed redundant tile objects, reducing first-page
internal allocation from about 14.8 KiB to about 6.2 KiB. Camera runtime layout still reduced the
pre-Home largest block to 7,680 bytes.

Candidate 056 (`e754869`, package `20261009-230114`, task `camera-home-reserve-056`) reserves the
largest available 12,288/10,240/8,192-byte internal/DMA block before Camera starts and releases it
after preview shutdown. COM3 selected the 8,192-byte reserve and a normal 6,144-byte DVP ring,
produced a software first frame, completed all six close stages and retained an 8,192-byte largest
internal/DMA block through every Home reconstruction phase and the 65-second MQTT/Main/Voice health
window. `E:RX`, overflow, DQBUF, panic and abort counts were zero; heap median drop was zero. This
closes the current short-window continuous-block gate. Resource and production remain **NO_GO**
because Voice supervisor stack headroom is still 2,384 bytes and physical image, arbitrary OOM,
mixed media/network/audio concurrency and qualification soak remain open.

Candidate 057 (`7400ed5`, package `20261009-231940`, task `voice-supervisor-stack-057`) raises the
PSRAM-backed wake supervisor stack from 4,096 to 6,144 bytes and publishes its configured capacity
with each Voice health sample. COM3 recorded 4,736 bytes free at startup and 4,432 bytes after the
same Camera → Home path, while internal/DMA largest remained 8,192 bytes. Candidate tooling now
requires at least 6,144 bytes configured capacity and 4,096 bytes remaining supervisor stack;
32 release-evidence and 13 Camera-smoke tests pass. The bounded continuous-block and supervisor-
stack gates now pass. Resource and production remain **NO_GO** for physical image, arbitrary OOM,
mixed media/network/audio concurrency, production signing/readback/power-cut and qualification soak.
No new eight-hour run has started.

Candidate 058 (`9babfae`, package `20261009-234829`, task `camera-streamon-cleanup-058`) records
whether `VIDIOC_STREAMON` actually succeeded. A startup failure now releases mapped buffers, the fd
and Board Manager ownership without issuing `VIDIOC_STREAMOFF`; a stream that did start retains the
existing retry-on-STREAMOFF-failure boundary. The production Camera translation unit passes 46 cases
and all seven Camera CTests in Debug and ASan/UBSan/leak mode; the pre-fix full translation unit fails
the new negative control by issuing STREAMOFF after failed STREAMON. ESP-IDF 6.0.2 and final Camera/
JPEG ELF audits pass. COM3 did not reproduce startup OOM: three local Camera starts with Display
active selected 6,144/4,096/4,096-byte rings, and a remote Camera start after Display stopped selected
6,144 B and reached first frame in 74 ms. Its clean stop recovered a 6,144-byte largest internal/DMA
block after WebRTC had reduced it to 2,560 B. The captured 320×240 sensor JPEG contained one uniform
dark RGB value, so physical transport passed while image quality did not. Failed-STREAMON hardware
cleanup, arbitrary OOM, broader concurrency and qualification soak remain open; release stays
**NO_GO** and no new eight-hour run has started.

Candidate 059 (`5972684`, package `20261010-001850`, task `camera-test-pattern-059`) adds a
default-OFF, release-fault-guarded GC0308 test-pattern diagnostic. COM3 emitted the explicit marker,
selected a 6,144-byte ring and delivered a 320×240 color-bar JPEG with high per-channel variance,
then completed STREAMOFF and restored a 6,144-byte largest internal/DMA block. This proves the SCCB
control path and the sensor digital-output → DVP/RGB565 → JPEG → WebRTC → Rodak display chain can
carry non-uniform pixels. It narrows the ordinary uniform dark frame to normal sensor-mode
initialization, exposure/gain, clock/power or optical input. The device and workspace were restored
to ordinary OFF package 060 (`20261010-003215`, task `camera-test-pattern-off-060`), whose binary
contains no fault marker. Package verification, COM3 VerifyOnly, NVS-preserving refresh and
Recovery → Main → OTA confirmation → Home passed; the original ID, binding, token version 4 and
MQTT connectivity remain. Ordinary Camera again produced the identical one-color JPEG, while the
test pattern did not. Physical image quality, failed-STREAMON hardware cleanup, arbitrary OOM,
broader concurrency and qualification soak remain open; release stays **NO_GO** and no eight-hour
run has started.

Candidate 062 (`c73c08f`, package `20261010-011055`, task `camera-register-settled-062`) adds a
default-OFF GC0308 register diagnostic. Configured, streaming, first-frame and settled snapshots
read page 0/1 through SCCB with zero read failures. At the 60th frame exposure candidates changed
from `03=00 04=96` to `03=01 04=e0`, while page-1 dynamic values converged to `1c/1c/1c/1c`;
this proves AEC/AGC activity. The package passed fault-aware verification, COM3 VerifyOnly and an
NVS-preserving `otadata + ota_0` refresh. Hardware produced a 94 ms software first frame, 328 frames,
and complete STREAMOFF/fd/device release, but its Remote Camera JPEG retained the historical dark
frame hash `7b68a3de4667b8288ebd434177f0344b58e198e53c67bef7fc51dfb5851b5f46`. The remaining image
failure is now narrowed to the analog front end, lens/obstruction, power or ordinary RGB output
configuration; software exposure activity and the digital transport chain are evidenced, while
physical image quality remains unproved.

The device was restored to ordinary OFF candidate 063 (`20261010-064533`, task
`camera-register-off-063`, main SHA-256
`846b585769b96b6c6e77cc996d5442fabd19435bb30ab93d2f278d0b58b52ec3`). Diagnostics and test pattern
are both OFF; package verification, COM3 VerifyOnly, NVS-preserving refresh, Recovery → Main → OTA
confirmation → Home and WiFi/MQTT recovery passed without `-Erase`, and no fault marker remains.
Resource/production stays **NO_GO**; arbitrary OOM, broader concurrency, physical image, power-cut,
production signing/readback and qualification soak remain open, so no new eight-hour run started.

Input audit after 062 found no managed-source or board-contract drift: the GC0308 driver hash matches
the reviewed 2.3.0 provenance, and BigSmart still uses 20 MHz XCLK, low-active DVP_EN and the recorded
VSYNC/DE/PCLK/data pin mapping. The next image-quality window therefore requires physical evidence for
DVP_EN, XCLK, PCLK/VSYNC, camera supply and lens/obstruction before any further register-table change.

Evidence status updated on 2026-10-10. 039 completed one same-boot normal/quiet/normal
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

The formal 28,800-second capture on ordinary OFF `20261009-014905` completed with
`status=no-go`, `complete=true`. It recorded 960 health samples and 95/95/95 application
request/ACK/completion events, but only 7 of 16 Camera requests succeeded; nine failed to allocate
the DVP DMA ring. The final serial log is 2,099,134 bytes with SHA-256
`f1717ea5ef0f511a171285099186512abfe9c67bddc47e3e50d223720741534d`.
The final status SHA-256 is
`c7da28066ee15572489f413dac368a08183463fa86d59078398295cb674a45ce`.
See [final evidence](ota-release-readiness.md#2026-10-09-正式八小时采集最终结果).

The formal-soak final log exposed a DVP cleanup failure during a Camera → Home transition:
STREAMOFF returned, but SCCB/I2C removal and DVP deinit reported errors before the app returned
Home. RodakOS `29aaacd` retains the board handle and I2C reference across failed deinit, and
`4e08efa` propagates release errors so `CameraDevice::Acquire()` retries cleanup before a new
initialization, and `51927b0` makes `CloseStream()` report deferred cleanup instead of false
completion. The capture package predates these fixes. `cc32989` added outer error propagation, a pinned DVP
teardown overlay and board-peripheral retry coverage. Debug/ASan host checks cover the
Camera wrapper 4/4 and DVP teardown 13/13; the capture source-contract suite remains 10/10.
A separate IDF 6.0.2 RCC overlay fixes the DVP deinit acquire/release mismatch and passes
six Debug and six ASan/UBSan CTests for single, repeated and shared-owner lifecycles. These
were software/host boundaries. Candidate 045 has now exercised the revised stop path on hardware:
three repeated windows contain no raw `E:RX`, while low contiguous headroom keeps the result NO_GO.
Earlier full-chain review found that the outer `dev_camera_deinit()` swallowed subtype errors
and that partial SDK teardown could leave a registered video device pointing at a freed sensor;
`cc32989` addresses those paths in source and host failure contracts. Candidate 045 confirms
repeatable software first-frame and close completion without stop-time `E:RX`, but does not
establish full physical recovery or adequate resource margin.

Candidate 043 remains the historical network/RCC package. The current Camera/DVP candidate is 045,
built from `85538b0` and packaged at `20261009-193618`; its main SHA-256 is
`65a49fad94eb2903b9e6d32a0fb83336a927d5480125c21dd405b4b097f13481`.
Package authentication, immutable-device verification, the NVS-preserving refresh and first boot
passed. Its repeated Camera evidence has zero `E:RX`, but remains NO_GO because of low contiguous
application and DMA headroom. See [045 device evidence](ota-release-readiness.md#2026-10-09-camera-dvp-停流修复候选-045-实机窗口).

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
| Resource recovery | Historical soak `20261009-014905` is NO_GO. Candidates 045–047 close stop-time `E:RX` and cover real 6,144/4,096 B DVP paths; 048–056 release Camera resources, reduce Home allocation and retain an 8,192 B internal/DMA block through Camera → Home; 057 raises wake-supervisor stack headroom to 4,432 B and enforces a 4,096 B minimum. Continue with physical image, arbitrary OOM, mixed media/network/audio failure matrices, production power-cut and qualification soak. Resource/production NO_GO | [RodakOS #28](https://github.com/rymcu/rodakos/issues/28), [051–056 evidence](ota-release-readiness.md#2026-10-09-home-重建与返回连续内存-051056), [057 evidence](ota-release-readiness.md#2026-10-09-voice-supervisor-栈余量-057), [Camera diagnostics](camera-teardown-diagnostics.md), [voice contract](voice-task-retirement.md) |
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

### 2026-10-10 DVP signal diagnostics 065 / normal 066

The default-OFF `RODAKOS_CAMERA_SIGNAL_DIAGNOSTICS` build now counts BigSmart GPIO5 XCLK, GPIO7 PCLK and GPIO44 VSYNC through PCNT input bypass. High/low watch points are required for `accum_count`; without them, the 20 MHz XCLK counter wrapped and under-reported. Candidate `20261010-073131` (`camera-signal-diagnostics-065`) measured approximately 20.0 MHz XCLK in both first-frame and settled windows, non-zero PCLK, and periodic VSYNC, while STREAMOFF/fd close/device release all completed. The diagnostic serial evidence is kept in `.codex-temp/camera-signal-065/serial.log`.

After the window, the workspace and device were restored to ordinary OFF. Package `20261010-074226` (`camera-normal-066`) passed signed package verification, COM3 VerifyOnly, NVS-preserving `otadata + ota_0` refresh, and Recovery → Main → Home boot. These counters establish DVP signal activity only; physical image quality, resource/OOM, production signing, and the eight-hour qualification gate remain **NO_GO**.

### 2026-10-10 DVP_EN readback 067 / normal 068

Candidate `20261010-075734` (`camera-signal-diagnostics-067`) adds a read-only PCA9557 bit-2
readback through the Board Manager `gpio_expander` handle. It does not change expander direction or
output. The COM3 window reported `dvp_en_level=0 dvp_en_read_ok=1`; BigSmart defines DVP_EN as
active-low, so the enable line was asserted while Camera started. PCNT simultaneously measured an
approximately 20.0 MHz XCLK, active PCLK and periodic VSYNC, and all STREAMOFF/fd/device release
stages completed. This narrows the physical-image investigation beyond an unasserted DVP_EN line.

The device and workspace were restored to ordinary OFF package `20261010-080938`
(`camera-normal-068`), which passed package verification, COM3 VerifyOnly, the NVS-preserving
refresh and Recovery → Main → OTA confirmation → Home boot. Remaining work is physical sensor
power/analog-front-end, lens/obstruction and image-quality inspection, plus arbitrary OOM,
production signing/readback/power-cut and qualification soak. Status remains **NO_GO**.

### 2026-10-10 DVP_EN power-cycle diagnosis 069 / normal 070

The 069 development candidate briefly drove BigSmart PCA9557 DVP_EN to its inactive level for
100 ms, restored the active-low enable for 100 ms, then initialized Camera. Serial evidence read
`initial_level=0 disabled_level=1 enabled_level=0`; first frame arrived in 68 ms and teardown
completed without `E:RX`. Two remote 320×240 captures became non-uniform (1,108 and 1,464 RGB
unique values) instead of the historical single RGB `(23,28,24)` dark frame. This is the first
hardware evidence that the sensor needs a controlled DVP_EN power/reset transition before use.

The device was restored to ordinary OFF package `20261010-083900` (`camera-normal-070`), with
package verification, COM3 VerifyOnly, NVS-preserving refresh and Recovery → Main → OTA
confirmation → Home boot complete. The next gate is to decide whether this controlled power cycle
belongs in the ordinary Camera initialization path after power-rail, color/exposure and optical
checks; production signing/readback, arbitrary OOM, power-cut and qualification soak remain open.
Status remains **NO_GO**.

### 2026-10-10 ordinary BigSmart Camera power reset 071

Candidate `20261010-085245` (`camera-dvp-power-reset-071`) moves the proven DVP_EN transition
into the BigSmart board hook called before `esp_video_init`; the generic Board Manager hook is a
no-op for other boards. Ordinary COM3 evidence reported `initial=0 disabled=1 enabled=0`, a 79 ms
software first frame and complete teardown. The remote 320×240 frame had 1,517 RGB unique values,
so the ordinary path no longer produces the historical one-pixel dark frame. This closes the
diagnostic-to-production implementation gap, while physical color/exposure/optics, power rails,
arbitrary OOM, production trust, power-cut and qualification soak remain open. Status remains
**NO_GO**.

Two additional ordinary Camera → Home cycles produced 1,445 and 1,378 RGB unique values and
completed frame removal in roughly 0.34–0.39 seconds, supporting repeatability of the board-level
power reset. This is still bounded software/image evidence rather than physical color, exposure,
power-rail or long-duration acceptance.

Two real hard-reset cycles on the same ordinary 071 package preserved NVS/binding, reconnected
WiFi and MQTT generation 1, then completed Home → Camera → Home with 76 ms and 53 ms first frames.
Both cycles repeated the board power-reset marker and full teardown without `E:RX`, panic or abort.
This closes the bounded cold-restart observation for the candidate; power-cut, production and soak
gates remain open.

### 2026-10-10 Camera second-cycle DMA headroom 072

The ordinary 071 two-cycle smoke exposed a repeatable second-start failure: the largest contiguous
internal DMA block fell from 4,352 bytes to 3,968 bytes, below the 4,096-byte DVP fallback ring.
Candidate `20261010-093755` (`camera-dma-headroom-retry-072`) now checks DMA headroom after reserving
memory for the Home return path and releases that reserve before audio and Camera startup when the
largest block is below 4,096 bytes.

The package passed ESP-IDF 6.0.2 build, verification, COM3 VerifyOnly, the NVS-preserving refresh and
Recovery → Main → OTA confirmation → Home boot. In one serial session, cycle one started with a
7,680-byte block and delivered its first frame in 92 ms. Cycle two detected a 3,840-byte block,
released the Home reserve, recovered an 8,192-byte block and delivered its first frame in 117 ms.
Both cycles completed all six teardown markers and separate 65-second MQTT/Main/Voice health
windows without Camera OOM, `E:RX`, error, panic or abort. This closes the bounded second-cycle DVP
ring restart gate. Physical color/exposure/optics, power rails, arbitrary OOM and concurrency,
production signing/readback/power-cut and the eight-hour qualification soak remain open, so release
status remains **NO_GO**.

A follow-up five-cycle serial run on the same 072 package triggered the 3,840-byte headroom branch
on every cycle, recovered an 8,192-byte block each time and delivered driver first frames in
60/71/94/74/64 ms. Every cycle completed all teardown markers and a 65.02–65.09 second health
window. The five persistent I2C references remained constant rather than accumulating, the heap
median drop was zero, and no Camera OOM, `E:RX`, error, panic or abort was observed. This strengthens
the bounded restart evidence to five cycles without changing the remaining release gates.

A separate Rodak remote-stream series captured 320×240 frames at 3/10/20/35/60 seconds. All frames
were non-uniform with 1,646–1,779 unique RGB values. Channel means varied by at most
0.238/0.321/0.063 over the minute, and stopping removed the image from the UI in 41 ms. This supports
short-term exposure/color stability for the current static dark scene. The scene remains dark and
low-contrast and had no color chart, sharpness target or verified unobstructed lens, so color
accuracy, exposure range and optical clarity remain open.

### 2026-10-10 073d/074 资源门禁更新

073d 已实机覆盖“首次 Camera ring 分配失败 → 完整失败清理 → 下一次按帧对齐 recovery ring 重试”：3,840 B recovery ring 取得首帧、六阶段关闭完整，65 秒健康期 internal/DMA largest 恢复 8,192 B，测试包结束后已恢复普通 OFF。普通 074 首轮 Camera 首帧与关闭通过，但健康期最大连续块为 6,656 B，资源门禁仍为 NO_GO。继续保留物理画质、任意 OOM、混合并发、生产 power-cut/readback 和八小时长稳为未完成项。

### 2026-10-10 075/076 Camera 自动 OOM 重试

075 在同一次 Camera 请求中完成首次 ring OOM 后释放 reserve 和 3,840 B recovery ring 自动重试；076 恢复普通 4,096 B 阈值，两轮普通 Camera 健康期 internal/DMA largest 均保持 8,192 B。设备当前为普通 OFF。任意 OOM、物理画质、电源轨、混合并发、生产 power-cut/readback 和八小时长稳继续 NO_GO。

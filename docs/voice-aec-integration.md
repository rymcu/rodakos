# Voice AEC integration

RodakOS reuses the `xiaozhi` BigSmart AEC input arrangement and extends the Rodak playback-time
interruption contract with VAD start/end and playback epochs. Capture continues while TTS is
playing, and only AFE-processed audio is sent upstream.

## Device path

The BigSmart ES7210 exposes four TDM slots. The physical microphone board labels are crossed at
the ADC net names: physical `MIC1` is wired to `ADC_MIC2P/N`, while physical `MIC2` is wired to
`ADC_MIC1P/N`. ES8311 speaker output `OUTP/OUTN` is wired back to ES7210 `MIC3P/MIC3N`
through 0-ohm links, so `MIC3` is the AEC reference channel, not a second user microphone. The
AFE input must therefore be `MR`, where `M` is the selected MIC1/MIC2 signal and `R` is MIC3.

The processor follows `D:\workspace\xiaozhi\main\audio\processors\afe_audio_processor.cc` for
the hardware input arrangement, with a separate RodakOS conversation policy:

1. Read four-channel 16 kHz TDM from `AudioCodecInput`, then select M and retain R.
2. Feed it to ESP-SR AFE with `AEC_MODE_VOIP_HIGH_PERF` and WebRTC VAD (`VAD_MODE_0`,
   no neural VAD model). The configured AFE minimum speech/noise times are 128/200 ms.
3. Retain fetched PCM, including silence, as the continuous Opus uplink source. Upload frames
   carry sample-weighted VAD metadata and a sample-clock timestamp. Do not append AFE
   `vad_cache`, which would duplicate onset samples already uploaded through this continuous path.
4. Evaluate `VoiceBargeInPolicy` inside the existing assistant I/O task. After actual playback
   begins, require at least 180 ms of fresh silence followed by 180 ms of sustained speech.
   Short noise, invalid VAD, missing frames and stale queued PCM do not complete confirmation.
5. Interrupt once for that playback and preserve capture, AFE and the uplink Opus encoder.
   This path does not call `VoiceWakeService::NotifyWakeWordDetected` or restart the recorder.

`CONFIG_USE_DEVICE_AEC` and server-side timestamp AEC are mutually exclusive. RodakOS should use
device AEC first; the server-AEC path requires Binary Protocol 2 timestamps for every uplink frame
and the matching downlink playback timestamp.

## Rodak contract

AEC-enabled firmware advertises `features.device_vad_epoch: 1` in hello. Rodak uses this
explicit capability to leave immediate playback interruption to the device; server codec VAD
continues capture and semantic probing without a competing hard preemption. Older devices keep
the existing server-side interruption behavior. ASR remains a fallback for unconfirmed speech.

The device sends `listen:start` with `mode: "realtime"`, then continuous Opus frames. A confirmed
device VAD event is sent as `type: "vad"`, `state: "start"`, with `source: "esp-sr"`, `seq`,
`trigger_ms` and `playback_epoch`, followed by `type: "abort"`, `reason: "vad_detected"` and
the same playback epoch. `trigger_ms` records the detected speech onset rather than the later
confirmation time. Both send results are checked. Rodak uses the epoch to deduplicate the
device request against server-side interruption and preserves the existing capture buffer.

The device closes playback and resets only the downlink decoder. It keeps the WebSocket,
recorder and uplink encoder running and does not send another `listen:start` for tagged VAD
interruption, because restarting server capture can discard the beginning of the new utterance.
After 180 ms of fresh VAD silence it sends one `vad:end` with the original sequence and epoch.
An outstanding end prevents another local interruption from overwriting that identity; session
cleanup clears the pending state. This also supports Rodak's `requireVadEndToRearm` option.

TTS start/stop events carry `playback_epoch`; the transport associates binary audio with the
latest accepted start. `VoicePlaybackEpochPolicy` rejects interrupted or older start/audio/stop
events and accepts audio/stop only for the current epoch. A newer start admits the next reply.
The server must prevent an old producer from emitting audio after that newer start. Legacy
servers without epochs retain explicit interruption behavior, but automatic device VAD is
disabled for them because untagged packets cannot provide the same late-audio isolation.

The physical MIC1/MIC2/MIC3 mapping is confirmed against the schematics in `images/`, and
the AFE processor and VAD policy are integrated. Real acoustic echo attenuation, microphone
double-talk and noise rejection still need physical validation.

## Current lifecycle implementation

Standby loads MultiNet only. Conversation start creates AFE and its independent fetch worker before
publishing the conversation state. Capture submits interleaved MR blocks of twice the per-channel
feed chunk size; only fetched AFE PCM enters the conversation queue. Stop invalidates the generation,
waits for an in-flight feed while fetch drains, joins the fetch worker, then destroys AFE. A subsequent
conversation creates a new instance. Configuration objects are freed after AFE creation.

Capture and fetch stacks use PSRAM, while provisioning and wake notification retain internal stacks
for NVS writes. Provisioning retains its original 4096-byte stack. Device AEC now enables local
WebRTC VAD without an additional gate task; the gate runs in the existing assistant I/O task.

Firmware build and configuration-preserving COM3 refresh passed on 2026-09-16, including startup,
local OTA confirmation, serial readiness, wake monitoring and saved WiFi auto-connect. MQTT recovery
was subsequently verified with Rodak connection/shadow events and five consecutive telemetry reports;
see [MQTT worker resources](mqtt-ota-sd-recovery.md#mqtt-worker-resources). Repeated real-person
conversations and acoustic AEC attenuation still require evidence. The USB-injected VAD gate
results below do not replace those acoustic checks.

## Remote USB diagnostic sessions

`tools/run_serial_voice_test.py` can preload a mono 16 kHz PCM16 WAV and trigger a
software wake on the physical board. For example:

```powershell
python tools/run_serial_voice_test.py --port COM3 --wav <speech.wav> --cycles 2 --seconds 45 --log build/logs/voice-usb-test.log
```

The USB-only `RODAK_VOICE_TEST_V1` commands are `audio_begin <sample_count>`,
`audio_chunk <sample_offset> <PCM16LE_hex>`, `wake`, `stop`, and `audio_clear`.
The limit is ten seconds of audio and 256 samples per sequential upload chunk.
The `RODAK_VOICE_TEST_RESULT` response acknowledges acceptance; wake/stop execution
must be verified from subsequent runtime logs. No settings are changed.

Preloaded audio resides in PSRAM. Software wake uses the existing internal wake
notification task. During the test, synthetic PCM replaces only the AFE microphone
input; the electrical reference stays connected. After the fixture ends, microphone
input is zero until the session ends. Returning to wake monitoring frees the fixture.
Use this to test AFE/Opus/WebSocket/TTS memory and repeated session cleanup, and
cross-check Rodak STT/TTS events. It bypasses acoustic wake recognition and microphone
pickup, so it cannot establish echo attenuation, double-talk accuracy or VAD quality.

On 2026-09-16 the non-erasing COM3 diagnostic build completed two injected-audio
sessions (`build/logs/voice-usb-test.log`): AFE AEC/NS, Opus uplink, Rodak STT/TTS,
playback, and re-arming all occurred. Both transcriptions were incomplete, so this
is a transport/lifecycle result, not an ASR accuracy pass. A one-second silent
pre-roll is now the uploader default to avoid feeding speech during startup.

The third attempt (`build/logs/voice-usb-padded.log`) failed before WebSocket
connection: internal free space was 6,111 bytes and the largest block 5,632 bytes,
below the 6,144-byte WebSocket stack allocation. The observed internal low-water
mark across these tests was only 575 bytes. The failed session cleaned up and
normal wake monitoring resumed. These results supersede any inference that the
idle 18 KiB SRAM measurement establishes sufficient headroom for device VAD.

Subsequent [session memory optimization](voice-session-memory.md) passed six
consecutive sessions and raised the internal low-water mark to 10,911 bytes.
Device VAD was disabled in that six-session memory baseline. Subsequent Rodak commit `7ad4de4a` fixed premature
server segmentation and repeated interruption-probe routing. Three identical
fixture replays then retained the complete spoken tail and produced one effective
recognition/reply each. The proper-name recognition error also occurs with the
original WAV offline and remains an ASR accuracy issue, not proven device loss.

## Current VAD verification, 2026-09-16

The latest device baseline logs show the actual AFE chain `AEC -> NS -> VAD` using WebRTC VAD.
One injected conversation completed without a false local interruption and recorded a lowest
internal free heap of 14,215 bytes. This is one observed run, not a worst-case SRAM guarantee.

The playback replay diagnostic (`--barge-in --interruptions 2 --cycles 3`) subsequently passed
the device gate in three sessions with six confirmed interruptions. These runs exercise software
wake, the physical board's AFE/Opus/network path, playback gating and repeated interruption.
That intermediate run exposed one truncated server-side utterance. Follow-up runs also exposed
competing codec/device cancellation. Rodak now isolates worker generations, preserves ASR startup,
separates live-final endpoint protection from interruption routing, and honors the hello capability
above. The intermediate runs are not the final acceptance evidence.

Package `20260916-162147` (SHA-256
`6a7140f0845102ef6cd3352529e895b5880c041cad52af007d269a722392ae71`)
passed non-erasing COM3 refresh, immutable Recovery verification and local OTA confirmation.
The final joint run is `build/logs/vad-capability-joint.log` with its summary JSON:

- Three sessions, six local confirmations, six accepted server VAD starts/ends and exactly six
  ESP-SR reply cancellations. Each session progressed through playback epochs 2, 3 and 4.
- All nine transcriptions (initial speech plus two interruptions per session) retained the full
  spoken tail. All six replay recordings cover the complete 2.82-second source, with envelope
  correlation 0.945–0.948. The source proper-name ASR error remains reproducible independently.
- No competing codec hard-preemption, ASR second cancellation, allocation failure, reset or
  watchdog occurred. Minimum internal free heap was 12,823 B; each cleanup restored a 14,336 B
  largest block. MQTT remained connected.
- Explicit software wake during playback also passed in `build/logs/vad-manual-wake.log`:
  `wake_word_detected` abort and same-session `listen:start`, with no fabricated VAD event.
  This path bypasses automatic VAD-end gating and retains old-playback epoch isolation.
- ESP-IDF 6.0.2 build and host app-model tests passed. Rodak's final targeted suite passed
  89 tests; build, lint and type checking also passed.

Rodak's waveform evidence is `out/diagnostics/vad-capability-audio-continuity.json` in its
repository. These USB tests do not establish acoustic echo attenuation, distant speech,
noise rejection or double-talk performance. Stopping may finish one already-issued audio write;
cleanup joins the I/O task before closing output, and late playback accounting is rejected.

## AFE fetch timeout recovery

An AFE fetch timeout does not invalidate PCM returned by preceding successful fetches.
`VoicePcmAssembler::InvalidateContinuity()` therefore retains the partial upload frame;
only an explicit lifecycle `Reset()` discards it. Unsuccessful fetch results are still rejected,
even if the vendor result contains a non-null data pointer or a nonzero size.
The frame crossing the gap has invalid VAD metadata. The sample clock is re-anchored at the
next successful fetch, accounting for retained samples without advancing its end into the future.

Host regression tests exercise repeated timeouts between 512-sample fetches, exact preservation
of 1,920 input samples, VAD recovery, multiple output frames and explicit session reset.
They reproduce the timeout boundary deterministically; ordinary USB replays alone do not prove
that this rare scheduling condition occurred on hardware.

The timeout fix built with ESP-IDF 6.0.2 and was installed through non-erasing COM3 package
`20260916-164544` (SHA-256
`0b59379a2d24ca59f3ed5f6dc2156ba10154ddf812bc9916282df473bdfe393f`).
`build/logs/afe-timeout-regression.log` passed two same-session device interruptions and ends,
with no reset or allocation failure; internal low-water was 14,203 B. However, server session
`bcc8fe7e` split the second replay into 2.34-second and 1.14-second recordings and recognized
only the first portion. There was no AFE fetch rejection during that utterance (the only rejection
was during shutdown). This is a remaining segmentation issue, not evidence against or proof of
the deterministic timeout fix. Do not treat this latest run as full end-to-end continuity acceptance.

That remaining server issue was subsequently reproduced in its production energy VAD: after a
pause, raw speech had resumed but the smoothed dB level still counted it as silence. Rodak now
accepts raw or smoothed energy for continuation of already-confirmed speech, retaining its
original onset gate and 700ms endpoint configuration. On unchanged firmware `20260916-164544`,
`build/logs/tail-vad-fixed-joint.log` passed three sessions with nine unique interruptions and
twelve complete transcriptions/recordings, with no extra tail segment. All recordings cover the
2.82-second source. The previous failure above remains historical evidence, not the current
acceptance result. This still does not replace acoustic AEC or long-duration environmental tests.

## Real-person acoustic test failure, 2026-09-16 20:50 CST

Session `64690d9f-4888-4fe5-a5a1-100ae58d1b1a` used real wake and microphone input,
without USB audio injection. Wake and the question about Su Shi were recognized. The user
confirmed remaining silent when playback stopped after about 5.3 seconds. Device VAD seq 13
triggered local interruption; Rodak accepted it and canceled TTS before semantic confirmation.
Subsequent ASR was empty/no-speech, so the failure was not an ASR transcription mistaken for
an interruption. AEC was enabled, but raw microphone/reference/output signals were not captured;
the cause of the residual trigger cannot yet be assigned to reference wiring, alignment or AEC
attenuation. This is a failed acoustic gate despite the earlier USB protocol passes.

Evidence: `build/logs/live-human-20260916-2049.log` and its summary JSON. Next diagnostics must
measure microphone M, reference R and AFE output during silent TTS playback, and validate an
echo-aware interruption gate before repeating the real-person barge-in test.

## Acoustic diagnostics and reference gain correction

The USB diagnostic commands `aec_arm <milliseconds>`, `aec_stop`, `aec_status`,
`aec_read <channel> <sample-offset> <sample-count>` and `aec_clear` capture actual input.
The limit is 6 seconds / 960,000 bytes in PSRAM for four TDM channels plus independent AFE
output. Readback is allowed only after capture stops, at most 256 samples per request. Normal
session stop retains the data for export; explicit clear and frontend deinitialization free it.
Raw/AFE sample counts, software start-time estimates, discontinuities and microphone switches
are recorded separately. These are not hardware-synchronized timestamps or ERLE measurements.

```powershell
python tools/capture_aec_diagnostics.py --port COM3 --output build/logs/aec-human
python tools/analyze_aec_diagnostics.py build/logs/aec-human
```

The first command waits for real wake, a spoken question and actual TTS playback. The user must
then remain silent. For unattended diagnostics, `--prompt-wav <mono-16k-pcm16.wav>` injects only
the question, then `audio_live` disables injection and restores physical microphones before
capture. This mode is not a real-person wake or double-talk test. Large PCM exports occur after
recording, not on the capture/fetch tasks. The console uses `from_chars`; stream/locale parsing
was removed after it caused measurable internal heap overhead.

The 30 dB uniform-gain baseline in `build/logs/aec-auto-baseline/analysis.json` showed reference
TDM slot 1 at full scale, with 0.3583% clipped samples; the microphone channels were not clipped.
Conversation capture now selects `AudioCodecInput::InputGainProfile::kAecReference10Db`:
all microphones retain 30 dB, while physical ADC channel 2 (MIC3, TDM slot 1) requests 10 dB,
matching xiaozhi. ES7210 quantizes this request to 9 dB. Wake, Recorder and legacy owners use
the default uniform profile. Cache keys include both gain and profile, and owner/format changes
invalidate the cache. The SDK channel-gain wrapper does not propagate every register-write error,
so readback waveforms, not an API success alone, establish the observed effect.

After correction, `build/logs/aec-refgain-fixed/analysis.json` recorded reference peak 1,976 and
zero clipped samples. AEC output remained low but VAD still fired. A conservative playback-only
gate now requires DC-removed PCM RMS >=256 (-42.1 dBFS) for the existing 180 ms confirmation
window, in addition to valid fresh VAD. Raw PCM continues uploading and matching VAD end keeps
its original semantics. This is an energy safeguard, not a speaker identity or double-talk model;
quiet/distant speech may rely on server ASR confirmation instead of immediate device interruption.

An intermediate RMS128 gate also fired during a longer capture (RMS about153); it is not the
deployed threshold. Final package `20260916-213126`, SHA-256
`8c2ba4499214040808d3e4887c13fae3108df3c8ec5ee34521114fb02465e452`, passed non-erasing COM3
refresh, startup and host regressions. `build/logs/aec-gate-positive.log` passed two synthetic
prompt sessions with four injected interruptions and four matching ends, without failure markers.

The physical-microphone playback run `build/logs/aec-gate-quiet-three.log` had two interruptions
in one of three sessions, and its automated gate reports failure. Its ASR included speech
unrelated to the reply. The user subsequently confirmed being away and could not confirm the
ambient sound, so this is an uncontrolled observation, not a silent-room pass or proof that both
events were echo. The threshold was not raised again based on that observation. Repeat a
controlled real-person silent-playback and double-talk test before accepting acoustic behavior.

### September 16 late-follow-up investigation

Session `92e848c1` exposed a playback rearm race: stop epoch3 at23:12:43.565,
start epoch4 at43.834, then an obsolete follow-up listen at44.481. The accepted new
start now clears delayed follow-up rearm; rearm also requires the conversation policy
to be waiting. The server had sent the complete answer, while the old rearm could
switch the device to Listening and discard subsequent audio.

Session `7c2043f6` exposed a separate inactivity deadline: follow-up started23:32:11.450,
new speech began41.142, but device stop followed41.550. The final600ms recording was
a truncated new utterance, not evidence that the user spoke too briefly. Fresh AFE
speech while Listening now refreshes the30s inactivity deadline; idle/playing/finished
policy states cannot be armed by this update. Package `20260916-233912` passed build,
host policy tests and non-erasingCOM3 startup verification.

`tools/run_serial_voice_test.py --late-follow-up` replays an injected fixture28s after
follow-up starts, requires another actual playback, then verifies eventual idle timeout.
The first run (`build/logs/idle-boundary.log`, session `2d56950c`) failed end-to-end:
the device correctly extended its deadline, but the server's30s empty rolling capture
expired at23:42:07.117 and imposed2.5s cooldown just as138-byte uplink packets arrived.
Only780ms of the tail survived into the next capture. This is a server reception gap,
not an AFE timeout or acoustic AEC result. Preserve this failed run alongside later fixes.

After Rodak's continuous no-speech rollover fix, the same test passed in session
`0f7978b0` (`build/logs/idle-boundary-fixed.log`). Device follow-up started at624163ms,
the next reply actually played at656993ms (past the old654163ms deadline), and its
next follow-up at694903ms timed out normally at724923ms. Server rollover at23:50:54.909
preserved detection and the second STT arrived23:50:58.163. No failure markers were
observed. This verifies the injected protocol/timing path, not real-room echo rejection.

Final joint regression `build/logs/overnight-final-barge.log` passed two sessions with
four injected VAD interruptions, four matched ends, and six actual playback starts.
Both session gates passed without failure markers; diagnostic audio was cleared and
the device returned to physical wake monitoring. WiFi and service settings were retained.

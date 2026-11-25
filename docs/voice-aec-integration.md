# Voice AEC and barge-in integration

RodakOS keeps the microphone path active while assistant TTS is playing. The
BigSmart audio path uses the ES7210/ES8311 loopback reference, ESP-SR AFE, and
WebRTC VAD to decide whether a speaking-time interruption is real. The wire
messages and stale-result rules are defined in the
[realtime voice v1 contract](rodak-realtime-voice-contract-v1.md).

## Device path

The BigSmart ES7210 exposes four 16 kHz TDM slots. The board wiring maps the
physical microphones as follows:

- physical `MIC1` is `ADC_MIC2P/N`.
- physical `MIC2` is `ADC_MIC1P/N`.
- `MIC3` is the ES8311 speaker loopback and is the AEC reference.

The conversation frontend reads the four-channel stream from `AudioCodecInput`,
selects the active microphone and retains the loopback reference as `M/R`, then
passes it to ESP-SR AFE with `AEC_MODE_VOIP_HIGH_PERF` and WebRTC VAD
(`VAD_MODE_0`). Recorder uses its own input ownership and the default uniform
gain profile. Conversation capture uses `InputGainProfile::kAecReference10Db`,
keeping microphones at 30 dB while requesting 10 dB (9 dB after ES7210
quantization) for the reference channel at normal levels. During conversation, four
loopback samples at or beyond ±24,576 in one 20 ms read select 24 dB microphone gain
and 0 dB reference gain for the next read. The driver changes the two gain masks
on the open ADC. The lower gains stay in effect through playback and return to
normal after one second of inactive loopback. Wake monitoring keeps the normal
profile when its own echo canceller is enabled. It is a separate synchronous ESP-SR AEC
in front of MultiNet, described in [wake word evaluation](voice-wake-evaluation.md#wake-frontend-aec-and-agc).

Fetched PCM, including silence, is the continuous Opus uplink source. Frames
carry sample-weighted VAD metadata and a sample-clock timestamp. The service
does not append AFE `vad_cache` samples because those onset samples already
exist in the continuous stream.

The sample clock is anchored at the first fetch. When a later fetch shows that a
timestamp would be later than the fetch time, the anchor moves back. Before this
correction, a late first fetch put most frames 30–85 ms in the future for the
whole session. Barge-in rejects frames more than 30 ms ahead of the device
clock, so in most sessions no frame could reach it.

## Interruption policy

After actual playback begins, `VoiceBargeInPolicy` ignores the first 400 ms,
while AEC is still adapting to the new reply. It then requires at least 180 ms
of fresh silence, followed by at least 360 ms of continuous VAD speech. Inside
the last 360 ms of that speech, frames totalling at least 180 ms must pass both
energy gates:

- DC-removed AFE output RMS of at least 256 (about -42.1 dBFS).
- AFE output power at least 1/32 (−15 dB) of the loudest microphone input power
  over the last 200 ms, measured before AEC. For strong output (RMS at least 512),
  the existing ratio is 1/96. The fetch timestamp expires old raw reads even when
  capture pauses. A frame below the applicable ratio is treated
  as echo residual. A frame without input power (`input_power = 0`) skips this
  gate.

Short noise, invalid VAD, missing frames and stale queued PCM do not confirm an
interruption. A VAD silence frame ends the run and clears the energy evidence.
A speech frame that fails a gate does not end the run, because speech energy
dips between syllables.

Each `output.stop` logs
`Barge-in gate stats: vad_speech_ms=… quiet_rejected_ms=… echo_rejected_ms=…`,
and `AFE VAD confirmed:` adds `input_power` and the same counters.

### Hardware calibration (BigSmart, 2026-11-24)

These values come from serial tests on one BigSmart at volume 40, with the
device speaker playing real TTS from a local Rodak. The tests used
`tools/run_serial_voice_test.py`. Injected speech came from `--barge-in`;
echo-only sessions came from `--live-mic-playback` with a quiet room. No human
spoke during the tests.

| Signal after AEC+NS | Measured |
| ------------------- | -------- |
| Injected speech, input/output ratio of loud speech frames | 4–29 dB, median 12 dB |
| Injected speech, frames passing both gates in a VAD run | 3–4 of 6 frames per 360 ms |
| Echo only, longest VAD speech run | 480 ms |
| Echo only, frames passing both gates at −15 dB | at most 1 per run |

| Rule on hardware | Injected interruptions confirmed | Echo-only false interruptions |
| ---------------- | -------------------------------- | ----------------------------- |
| Every frame passes, −9 dB (first #57 version) | 0 of 5 | 0 of 7 |
| 180 ms in window, −12 dB | 6 of 9 | not run |
| 180 ms in window, −15 dB (current) | 9 of 10 | 0 of 6 |

In the one missed run, the VAD labelled no frame as speech during the replay, so
no energy gate was reached. In the echo-only runs at −15 dB, the VAD labelled
1.3–2.0 s of each reply as speech. The quiet gate rejected 87–100 % of that time
and the echo gate rejected the rest.

The calibration does not cover a human talker, other volumes, or other rooms.

While the ES8311 loopback carries playback (mean square above 1024, about
−60 dBFS) and for 1 s after it, `VoiceMicSelector` does not switch between MIC1
and MIC2. A switch would change the echo path under a converged AEC.

When the policy confirms speech, the service:

1. sends `vad` with `state: "start"`, source `esp-sr`, a positive sequence, and
   the current playback epoch when the negotiated strategy permits device VAD.
2. sends `playback.abort` with `reason: "vad_detected"` on the same session and
   epoch.
3. closes the local decoder and DAC output, while retaining the WebSocket,
   recorder, AFE, and Opus uplink.
4. rejects late TTS frames until the next `output.start`.
5. sends one matching `vad` `state: "end"` after 600 ms of fresh silence, so a
   pause between words does not end the utterance.

The tagged interruption does not call the local wake service and does not open a
second session or send a new `input.start`. Manual wake interruption uses
`reason: "wake-word"` and follows the normal same-session input restart. A
pending VAD end owns its original sequence and epoch until cleanup, so a later
local event cannot overwrite it.

`features.device_vad_epoch: 1` in `session.open` advertises that the device can
correlate VAD and playback epochs. If the server selects
`server-authoritative`, the device continues uploading audio but suppresses
device VAD control events. `hybrid-fallback` permits device boundaries when
server-side boundaries do not arrive.

Status (2026-10-24, COM3): the uplink no longer falls behind during playback. Two causes were
fixed. Reply playback moved to its own task, and the capture loop no longer sleeps one tick after
each full read. Before the second fix, the I2S RX ring overflowed and the uplink frame age reached
about 4.7 s after 70 s of playback. The frame age now stays at 0 ms. Server-side barge-in works.
On that October build, device-side barge-in rarely met the energy gate after AEC/NS.
The November calibration above records the later windowed policy and device results.

## Audio ownership and memory

The assistant owns the ADC with priority 30 through
`voice-conversation-frontend`. Wake monitoring uses priority 10 and Recorder
uses priority 20. `StopRecorderForPlayback()` is intentionally a no-op: keeping
capture alive is required for AEC/VAD barge-in and preserves pre-roll while a
reply is playing.

AFE capture/fetch work and the cleanup path use PSRAM where the ESP-SR and
WebSocket components permit it. Provisioning and wake notification keep their
internal stacks because they access NVS. The MQTT worker reserves a separate
8 KiB internal stack. Its resource notes are in
[MQTT worker resources](mqtt-ota-sd-recovery.md#mqtt-worker-resources).

The wake-to-cloud recorder retains the newest 80 frames, about 4.8 seconds at
60 ms per frame. If DNS/TLS/WebSocket setup exceeds that window, the recorder
discards the oldest frames.

## USB diagnostics

The local `RODAK_VOICE_TEST_V1` fixture exercises the AFE/Opus/WebSocket/TTS
lifecycle without changing settings:

```text
audio_begin <sample_count>
audio_chunk <sample_offset> <PCM16LE_hex>
wake
stop
audio_clear
```

The fixture stores input in PSRAM, accepts at most 256 samples per sequential
upload chunk, and is limited to ten seconds. It replaces only microphone input.
the electrical reference remains connected. It verifies transport, lifecycle,
playback gating, repeated cleanup, and Rodak event correlation. It cannot prove
real-person wake accuracy, echo attenuation, double-talk performance, or room
noise rejection.

For acoustic capture, use the diagnostic commands below. Send each command on
the serial console with the `RODAK_VOICE_TEST_V1 ` prefix. The device replies
with `RODAK_AEC_DATA {...}` lines and then one
`RODAK_VOICE_TEST_RESULT {"ok":...}` line:

```text
aec_arm <milliseconds>
aec_stop
aec_status
aec_read <channel> <sample-offset> <sample-count>
aec_clear
```

Capture is limited to six seconds of four-channel TDM plus AFE output in PSRAM.
Readback is available only after capture stops and is limited to 256 samples per
request. These samples are not hardware-synchronised timestamps and do not by
themselves establish ERLE.

`tools/capture_aec_diagnostics.py` runs this sequence. It waits for playback,
arms a capture, reads every channel back and writes one WAV per channel plus
`metadata.json`. `tools/analyze_aec_diagnostics.py <directory>` then reports
levels, clipping and lag correlation against the `tdm-slot-1` reference. Its
output is a diagnostic, not an acoustic acceptance score.

```powershell
python tools/capture_aec_diagnostics.py --port COM3 --output build/logs/aec-<stamp>
python tools/analyze_aec_diagnostics.py build/logs/aec-<stamp>
```

## Live microphone volume sweep on BigSmart, 2026-11-25

For [issue #63](https://github.com/rymcu/rodakos/issues/63), one BigSmart running
0.1.5 on COM4 made three ordinary Rodak voice requests at each volume. Every trial
started with the same injected recording requesting a one-minute story. At the first
playback marker the harness sent `audio_live` to restore the physical microphones.
It injected no barge-in speech. Rodak generated a new story for each trial. The
reported and desired volumes matched before and after every trial.

| Volume | Replies completed | Device AFE abort | Rodak ASR interruption |
| ---: | ---: | ---: | ---: |
| 60 | 3/3 | 0/3 | 0/3 |
| 80 | 1/3 | 0/3 | 2/3 at 15.10 s and 23.08 s |
| 100 | 1/3 | 1/3 at 3.72 s | 1/3 at 2.30 s |

Times are relative to the device playback-start marker. For Rodak ASR interruptions
the endpoint is the device's playback statistics line. In all three server-side
cases the server interrupted playback after its ASR recognized the transcript “嗯嗯”. The device returned to follow-up listening in these cases too.
Completed replies were checked against the number of TTS segments sent by Rodak
and the number of packets received by the device.

At volume 100, trial 3, the device confirmed local VAD at 3.65 s
(`frame_age_ms=52`, `pcm_power=206001`, `input_power=182884`). At 3.72 s it
logged `TTS interrupted`, `vad_sent=yes`, `abort_sent=yes`, `source=esp-sr`.
Rodak recorded receipt of `playback.abort` with reason `vad_detected` for the same
session. The harness sent its cleanup stop only after this event. Issue #63 used
IntelliConnect and reported `vad_sent=suppressed`; both captures show a device
abort. This sweep did not independently record room sound or isolate acoustic echo.
The board's reported and desired volumes returned to 30 after the last trial.

### High-level capture and delayed AFE input-power comparison

Package `20261125-193609` was built from `e7f924c` with the ADC headroom and input-power
history changes (0.1.5, app SHA-256
`ba8716079b910d993ed3525c37857f12c459c1e335fe0aaece8c23a652215e40`). A six-second
stock capture at volume 100 had 5.67% of MIC3 reference samples and 0.77% of the selected
microphone samples at or beyond ±32,760. A six-second capture on the final build measured
0.057% and 0.143%, respectively. Each capture contained 96,000 samples per channel,
no recorded discontinuities and no mic switch. They used different generated stories.

The stock capture's microphone and AFE RMS envelopes correlated most strongly at roughly
140 ms of delay (10 ms blocks). The old four-read, 80 ms peak could therefore compare an
echo with the quieter input *after* the corresponding speaker burst. In one 60 ms slice
the AFE power was 1,932,486, the recent 80 ms raw peak was 1,532,522, and the 200 ms peak
was 415,736,931. The wider history rejects this residual with the existing power ratio.
It resets at session stop and AFE buffer resynchronization.

| Volume | Complete replies on fixed build | Device AFE aborts | Server ASR interruptions | Injected speech interrupted and next reply finished |
| ---: | ---: | ---: | ---: | ---: |
| 60 | 3/3 | 0 | 0 | 1/1 |
| 80 | 5/5 | 0 | 0 | 1/1 |
| 100 | 6/6 | 0 | 0 | 1/1 |

The 14 physical-microphone trials used the same request as the stock sweep. Rodak's
completed segment packet totals matched device reception, and the codec reported zero
write failures. One volume-80 reply lasted only 2.76 s; four other volume-80 replies lasted
39–47 s. At volume 100 the six trials lasted 29–70 s. The separate interruption trials
injected speech during playback and checked local confirmation, `playback.abort`, VAD end,
the next playback epoch and natural return to follow-up. An initial fixed 12 s observation
ended before the next story finished; the table uses the later natural-completion checks.
Physical double-talk with a human and other rooms remains open.

The board finished on the development-signed production-flavor package with reported and
desired volume restored to 30. Recovery handoff, local OTA confirmation and Home startup
passed after flashing.

## Acceptance boundary

Host tests cover barge-in timing, VAD end ownership, playback epochs, stale
audio rejection, PCM continuity across AFE fetch timeouts, and cleanup. The
hardware gate must additionally demonstrate:

- no false interruption during controlled silent playback.
- correct microphone/reference gain and no reference clipping.
- complete speech capture after a barge-in, including the spoken tail.
- repeated same-session interruptions without a second WebSocket or reset.
- recovery to local wake monitoring after `session.end`, timeout, or error.
- a controlled real-person test with silence, speech overlap, fan/noise, and
  reverberation.

USB-injected protocol passes are lifecycle evidence only. They do not replace
the acoustic gate.

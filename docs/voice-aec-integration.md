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
quantization) for the reference channel. Wake monitoring uses the same profile
when its own echo canceller is enabled. It is a separate synchronous ESP-SR AEC
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
  over the last 200 ms, measured before AEC. A frame below this ratio is treated
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
Device-side barge-in still does not trigger: after AEC/NS, most speech frames during playback are
below the -42 dBFS energy gate, so the policy seldom gets three frames in sequence. This threshold
needs a separate adjustment.

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

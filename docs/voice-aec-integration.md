# Voice AEC and barge-in integration

RodakOS keeps the microphone path active while assistant TTS is playing. The
BigSmart audio path uses the ES7210/ES8311 loopback reference, ESP-SR AFE, and
WebRTC VAD to decide whether a speaking-time interruption is real. The wire
messages and stale-result rules are defined in the
[realtime voice v1 contract](rodak-realtime-voice-contract-v1.md).

## Device path

The BigSmart ES7210 exposes four 16 kHz TDM slots. The board wiring maps the
physical microphones as follows:

- physical `MIC1` is `ADC_MIC2P/N`;
- physical `MIC2` is `ADC_MIC1P/N`;
- `MIC3` is the ES8311 speaker loopback and is the AEC reference.

The conversation frontend reads the four-channel stream from `AudioCodecInput`,
selects the active microphone and retains the loopback reference as `M/R`, then
passes it to ESP-SR AFE with `AEC_MODE_VOIP_HIGH_PERF` and WebRTC VAD
(`VAD_MODE_0`). Wake and Recorder use their own input ownership and the default
uniform gain profile. Conversation capture uses
`InputGainProfile::kAecReference10Db`, keeping microphones at 30 dB while
requesting 10 dB (9 dB after ES7210 quantization) for the reference channel.

Fetched PCM, including silence, is the continuous Opus uplink source. Frames
carry sample-weighted VAD metadata and a sample-clock timestamp. The service
does not append AFE `vad_cache` samples because those onset samples already
exist in the continuous stream.

## Interruption policy

After actual playback begins, `VoiceBargeInPolicy` requires at least 180 ms of
fresh silence followed by 180 ms of sustained speech. Short noise, invalid VAD,
missing frames, stale queued PCM, and energy below the playback gate do not
confirm an interruption. The deployed playback-only energy safeguard requires
DC-removed PCM RMS of at least 256 (about -42.1 dBFS) during the confirmation
window.

When the policy confirms speech, the service:

1. sends `vad` with `state: "start"`, source `esp-sr`, a positive sequence, and
   the current playback epoch when the negotiated strategy permits device VAD;
2. sends `playback.abort` with `reason: "vad_detected"` on the same session and
   epoch;
3. closes the local decoder and DAC output, while retaining the WebSocket,
   recorder, AFE, and Opus uplink;
4. rejects late TTS frames until the next `output.start`; and
5. sends one matching `vad` `state: "end"` after 180 ms of fresh silence.

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

## Audio ownership and memory

The assistant owns the ADC with priority 30 through
`voice-conversation-frontend`. Wake monitoring uses priority 10 and Recorder
uses priority 20. `StopRecorderForPlayback()` is intentionally a no-op: keeping
capture alive is required for AEC/VAD barge-in and preserves pre-roll while a
reply is playing.

AFE capture/fetch work and the cleanup path use PSRAM where the ESP-SR and
WebSocket components permit it. Provisioning and wake notification keep their
internal stacks because they access NVS. The MQTT worker reserves a separate
6 KiB internal stack; its resource notes are in
[MQTT worker resources](mqtt-ota-sd-recovery.md#mqtt-worker-resources).

The wake-to-cloud recorder retains the newest 80 frames, about 4.8 seconds at
60 ms per frame. If DNS/TLS/WebSocket setup exceeds that window, the oldest
frames are discarded.

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
upload chunk, and is limited to ten seconds. It replaces only microphone input;
the electrical reference remains connected. It verifies transport, lifecycle,
playback gating, repeated cleanup, and Rodak event correlation. It cannot prove
real-person wake accuracy, echo attenuation, double-talk performance, or room
noise rejection.

For acoustic capture use the diagnostic commands:

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

## Acceptance boundary

Host tests cover barge-in timing, VAD end ownership, playback epochs, stale
audio rejection, PCM continuity across AFE fetch timeouts, and cleanup. The
hardware gate must additionally demonstrate:

- no false interruption during controlled silent playback;
- correct microphone/reference gain and no reference clipping;
- complete speech capture after a barge-in, including the spoken tail;
- repeated same-session interruptions without a second WebSocket or reset;
- recovery to local wake monitoring after `session.end`, timeout, or error; and
- a controlled real-person test with silence, speech overlap, fan/noise, and
  reverberation.

USB-injected protocol passes are lifecycle evidence only. They do not replace
the acoustic gate.

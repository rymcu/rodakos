# Voice AEC integration plan

RodakOS and Rodak use the same playback-time interruption contract as the `xiaozhi` BigSmart
implementation. The device must keep capturing while TTS is playing, but only AFE-processed audio
is sent upstream.

## Device path

The BigSmart ES7210 exposes four TDM slots. The physical microphone board labels are crossed at
the ADC net names: physical `MIC1` is wired to `ADC_MIC2P/N`, while physical `MIC2` is wired to
`ADC_MIC1P/N`. ES8311 speaker output `OUTP/OUTN` is wired back to ES7210 `MIC3P/MIC3N`
through 0-ohm links, so `MIC3` is the AEC reference channel, not a second user microphone. The
AFE input must therefore be `MR`, where `M` is the selected MIC1/MIC2 signal and `R` is MIC3.

The planned processor follows `D:\workspace\xiaozhi\main\audio\processors\afe_audio_processor.cc`:

1. Read four-channel 16 kHz TDM from `AudioCodecInput`, then select M and retain R.
2. Feed it to ESP-SR AFE with `AEC_MODE_VOIP_HIGH_PERF` and `VAD_MODE_0`.
3. Use the first AFE output channel as the Opus uplink source.
4. On a confirmed `VAD_SPEECH` transition, call `VoiceWakeService::NotifyWakeWordDetected`.
5. Keep a short pre-roll in the recorder queue so the server receives the beginning of speech.

`CONFIG_USE_DEVICE_AEC` and server-side timestamp AEC are mutually exclusive. RodakOS should use
device AEC first; the server-AEC path requires Binary Protocol 2 timestamps for every uplink frame
and the matching downlink playback timestamp.

## Rodak contract

The device sends `listen:start` with `mode: "realtime"`, then continuous Opus frames. A confirmed
device VAD event is sent as `type: "vad"`, `state: "start"`, with `source`, `seq`, and `trigger_ms`,
followed by `type: "abort"`. Rodak performs ASR and semantic confirmation, filters likely playback
echo, stops the current TTS turn, and keeps the WebSocket session alive.

The physical MIC1/MIC2/MIC3 mapping is confirmed against the schematics in `images/`, and
the AFE processor is integrated. Realtime mode still requires VAD wiring and acoustic
validation before it can be treated as echo-safe production behavior.

## Current lifecycle implementation

Standby loads MultiNet only. Conversation start creates AFE and its independent fetch worker before
publishing the conversation state. Capture submits interleaved MR blocks of twice the per-channel
feed chunk size; only fetched AFE PCM enters the conversation queue. Stop invalidates the generation,
waits for an in-flight feed while fetch drains, joins the fetch worker, then destroys AFE. A subsequent
conversation creates a new instance. Configuration objects are freed after AFE creation.

Capture and fetch stacks use PSRAM, while provisioning and wake notification retain internal stacks
for NVS writes. Provisioning retains its original 4096-byte stack. Device AEC currently disables local
AFE VAD; the VAD-to-interruption wiring described above remains planned, not verified behavior.

Firmware build and configuration-preserving COM3 refresh passed on 2026-09-16, including startup,
local OTA confirmation, serial readiness, wake monitoring and saved WiFi auto-connect. MQTT recovery
was subsequently verified with Rodak connection/shadow events and five consecutive telemetry reports;
see [MQTT worker resources](mqtt-ota-sd-recovery.md#mqtt-worker-resources). Repeated real-person
conversations, playback-time VAD interruption and AEC attenuation still require evidence.

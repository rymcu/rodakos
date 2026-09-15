# Voice AEC integration plan

RodakOS and Rodak use the same playback-time interruption contract as the `xiaozhi` BigSmart
implementation. The device must keep capturing while TTS is playing, but only AFE-processed audio
is sent upstream.

## Device path

The BigSmart ES7210 exposes four TDM slots. The board mapping is `MIC2` as the near-end microphone
and `MIC3` as the reference microphone. The AFE input format must therefore be `MR`; preserving
the physical slot order as `RM` causes the echo canceller to learn the wrong signal.

The planned processor follows `D:\workspace\xiaozhi\main\audio\processors\afe_audio_processor.cc`:

1. Read the two-channel 16 kHz PCM frame from `AudioCodecInput`.
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

Until the AFE processor is integrated and the physical MIC2/MIC3 mapping is verified, realtime mode
is an integration/diagnostic path and must not be treated as proof of echo-safe production behavior.

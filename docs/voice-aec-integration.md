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
Device VAD remains disabled; incomplete STT and repeated server interruption
probing observed during synthetic tests still require separate investigation.

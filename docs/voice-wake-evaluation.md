# Wake Word Evaluation

This document records how RodakOS measures local wake recognition and the results that chose the
wake phrase, MultiNet model and detection window. The identity contract and shadow fields are in
[Rodak identity and wake word](voice-identity-wake-word.md).

## Method

The USB serial console accepts `RODAK_VOICE_TEST_V1 wake_eval <threshold_permille>
<lead_ms>[ <pinyin command>]`, next to the existing `wake` and `audio_*` voice diagnostics. It is
accepted only while wake monitoring is idle. It replays the uploaded diagnostic audio into the idle wake
recognizer in place of the microphone, after `lead_ms` of low dither. Every match is logged with its
MultiNet probability. No voice session opens. The threshold and command are restored when the clip
and a 1.5 s tail have been fed, or when listening stops. A command that cannot be restored releases
the model, and the next re-arm rebuilds it from the active identity before it listens again.
Clips must be 16 kHz PCM16; the tool does not resample. A leading `+` sends the clip through the
wake frontend, and `~<db>~` mixes in the live microphone (see below).

`tools/run_wake_eval.py` uploads each `pos_*` / `neg_*` WAV clip and sweeps thresholds and lead
times. The lead time moves the phrase onset inside MultiNet's detection window. The evaluator
runs in real time: one trial takes the clip length plus lead plus tail.

```powershell
python tools\run_wake_eval.py --port COM3 --clips <dir> --log <file> --match '^pos_' `
  --thresholds 100,140,200 --leads 0,1000,2000 [--command "hai da ke"] [--gain-db -30]
```

The clip set is synthesized with the three Windows OneCore zh-CN voices (Huihui, Kangkang,
Yaoyao) at speaking rates 0.8, 1.0 and 1.3. Negatives are near-miss phrases: 你好, 你好大哥,
你好打卡, 嗨大哥, 海达科技, 还打开吗, 大客户, 孩子大了, 打开空调 and 今天天气怎么样.

**Limits.** The injected signal skips the room, microphone, ES7210 gain and microphone selection,
and TTS speech is cleaner than real speech. These results compare phrases, models and window
handling on equal terms; they do not measure far-field recall or false accepts per hour. The
acoustic gates in [Rodak identity and wake word](voice-identity-wake-word.md#validation-gates)
remain open.

## Results

All results below were measured on 2026-10-22/23 on one BigSmart board, with a development-signed
`RODAKOS_RELEASE_TESTS` build flashed to `ota_0` only.

### Phrase choice: 你好达克 vs 嗨，达克 (MultiNet5, before window realignment)

| Command | Recall, onset 0 / 1 / 2 s | Near-miss false accepts (threshold 0.14) |
| --- | --- | --- |
| `ni hao da ke` | 9/9, 9/9, 1/9 | 你好大哥 3/3 (0.53–0.84), 你好打卡 3/3 (0.22–0.50) |
| `hai da ke` | 9/9, 9/9, 1/9 | 嗨大哥 3/3 (0.41–0.61), **海达科技 3/3 (0.98–0.99)** |

Thresholds 0.10, 0.14 and 0.20 gave the same recall within one trial; matched probabilities on
correct phrases were mostly above 0.9.

The two phrases have the same recall on clean speech. `hai da ke` is worse on false accepts: a
common word that contains it (海达科技) scores as high as the wake phrase itself, so no threshold can
reject it. Pinyin commands carry no tones, so `hai` also matches 还, 海, 孩 and 害. The near misses of
`ni hao da ke` score lower than true wakes and can be separated by raising the threshold if field
tests show false wakes. **RodakOS keeps 你好达克.**

### Detection window

MultiNet times out `kDetectionDurationMs` (3 s) after `clean()` and discards its buffered audio.
With the previous fixed window, a phrase that began 2 s into the window matched 1/9 times, against
9/9 at 0 s and 1 s. A phrase that starts at a random time therefore failed about one time in three
on top of every acoustic loss. The window now restarts after 400 ms of silence once it is at least
1 s old, so a phrase onset is always within the first second. The silence floor follows the
quietest chunk at once and rises slowly, so pauses between syllables do not restart the window.
It is kept in fixed point so it can still rise to very quiet steady noise, and an evaluation
resets it so its test signal is not mistaken for the room.

With realignment, `ni hao da ke` matched 54/54 trials with onsets of 0, 0.5, 1, 1.5, 2 and 2.5 s,
and 36/36 (onsets 0, 1, 2 and 2.5 s) on the final build. The near-miss false accepts stayed at 6/30
(你好大哥 and 你好打卡 only), so realignment did not add false wakes on this set. The device then sat
idle in a quiet room for about 45 minutes without a wake; that is not a false-accept rate.

Attenuating the clips shows where recall drops off. Clip peaks start near −3 dBFS:

| Attenuation | Clip peak | Recall (MultiNet5, threshold 0.14) |
| --- | --- | --- |
| −20 dB | −26 to −23 dBFS | 18/18 |
| −30 dB | −36 to −33 dBFS | 18/18 |
| −36 dB | −42 to −39 dBFS | 15/18 |
| −42 dB | −48 to −45 dBFS | 8/18 |

Recall falls once speech peaks are below about −40 dBFS. The [wake frontend](#wake-frontend-aec-and-agc)
moves that edge down by a few dB.

### Model: MultiNet5 vs MultiNet7

| Model | Recall (`ni hao da ke`, onsets 0/1/2 s) | Matched probability | PSRAM free while listening |
| --- | --- | --- | --- |
| `mn5q8_cn` | 27/27 with realignment | mostly 0.95–0.99 | 2.64 MB |
| `mn7_cn` | 13/27 (threshold 0.14), 15/27 (0.10) | 0.10–0.66 | 2.10 MB |

Espressif publishes Chinese benchmarks for MultiNet5 and MultiNet6 only. MultiNet7 recognised the
correct text on this set but with low confidence, and missed whole clips at every threshold. It
also costs about 0.54 MB more PSRAM and 0.47 MB more image. **RodakOS stays on `mn5q8_cn`.** The
frontend accepts both models because their command-table ownership was verified (see
[frontend identity tests](../tests/voice_audio_frontend_identity/README.md)), so a later model change
is a configuration change plus this evaluation.

### Wake frontend: AEC and AGC

`VoiceWakeFrontend` runs ESP-SR echo cancellation and WebRTC gain control on the capture task, in
front of MultiNet. It is synchronous: no extra task, ring buffer or AFE output contract. The
conversation AFE is unchanged.

- **AEC** (`afe_aec_create("MR", 4, AFE_TYPE_SR, AFE_MODE_LOW_COST)`) uses the selected microphone
  and the ES8311 loopback in TDM slot 1. Wake capture now opens the ADC with the conversation's
  10 dB reference gain so the loopback is usable. AEC takes about 20% of one core, so it runs only
  while the loopback carries playback (mean square above 1024, about −60 dBFS) and for 1 s after.
  Otherwise the microphone passes straight through. A failed AEC frame passes the microphone
  through too, so the timeline is never broken.
- **AGC** (`esp_agc_open(AGC_MODE_2)`, 9 dB compression gain, −3 dBFS target, limiter on) runs on
  every 10 ms block and costs about 1% of one core.
- `CONFIG_RODAKOS_WAKE_AEC` and `CONFIG_RODAKOS_WAKE_AGC` switch each stage off; a stage that fails
  to create is skipped and logged, not fatal.

`wake_eval +...` sends a clip through this frontend. `wake_echo <seconds> <attenuation_db>` plays
speech-band noise through the speaker; it fails if the speaker is already owned, and a real wake
stops it before the assistant asks for audio focus. `~<db>~` (−30 to +24 dB) adds the live
microphone, which picks up that echo, to the injected clip.
`run_wake_eval.py --frontend --echo-db N --echo-gain-db G` drives both and restarts the echo for
every clip.

Each `wake_eval +` trial recreates AEC and AGC, so no gain or filter state carries over from the
previous clip. Runs made before that reset read higher (15/18 at −42 dB, 4/30 near misses); the
figures below are from the final build.

| Measurement | Raw path | Through the frontend |
| --- | --- | --- |
| CPU, idle (no playback) | — | 1.1–1.3% of one core |
| CPU, while playing | — | 19.9–20.7% of one core |
| Echo reduced, real speaker echo at −6 dBFS playback | — | 18.0–20.2 dB |
| Recall, full-level clip | 18/18 | 18/18 |
| Recall, clip at −36 dB (peaks −42 to −39 dBFS) | 15/18 | 18/18 |
| Recall, clip at −42 dB (peaks −48 to −45 dBFS) | 8/18 | 12/18 |
| Near-miss false accepts, full level | 6/30 | 6/30 |

With `wake_echo` playing (restarted for each clip) and the live microphone, which carries the real
speaker echo, mixed into each clip at 0 dB:

| Playback level | Raw path | Through the frontend |
| --- | --- | --- |
| −6 dBFS | 6/18 | 16/18 |
| −12 dBFS, onsets 0/1 s | 18/18 | 14/18 |
| −12 dBFS, onsets 0/0.5/1.5/2 s (earlier build) | 24/36 | 28/36 |

At loud playback the frontend restores most wakes. At moderate playback it is mixed: worse at
onsets 0/1 s, better across more onsets, equal overall (42/54 each). Most remaining misses in both paths start 2 s
into the window: continuous, modulated playback never looks like 400 ms of silence, so the window
cannot realign and the phrase can straddle MultiNet's timeout again. Playback that is music rather
than this synthetic noise has pauses and has not been measured. Running AEC during playback was not
checked for effects on music decoding or UI timing.

An earlier echo test added the loopback signal itself to the injected clip. AEC cannot remove that,
because it is not an acoustic echo, and its in/out ratio stayed near 0 dB. Those runs are not used
here. `AFE_MODE_HIGH_PERF` was also tried for the wake AEC and recalled no better (12/18 against
13/18), so the low-cost mode stays.

## Not done yet

- **A trained WakeNet model** for 你好达克 through Espressif's TTS training service (esp-sr issue
  #88). This is the long-term option with the best published far-field figures. It fixes the wake
  phrase in the model, so Rodak identity changes would still need MultiNet.
- **Acoustic acceptance**: speakers, distances, noise and false accepts per hour, as listed in the
  identity validation gates.

## Wake confirmation

When the wake word is accepted, the assistant plays a 120 ms two-tone chime after audio focus is
granted and before the WebSocket opens. Before this, nothing confirmed the wake until the cloud
session answered, 0.9–1.9 s later, so users repeated the phrase. `CONFIG_RODAKOS_WAKE_CHIME`
disables it. The recorder keeps running and the device AEC removes the chime from the uplink.

The chime opens the speaker at 16 kHz, waits for its frames to play and closes the speaker again,
so the first TTS reply still opens it at the negotiated downlink rate. On the device this took
141–146 ms, and `session.ready` followed about 0.85 s later, as before. Two consecutive simulated-wake sessions passed the
`tools/run_serial_voice_test.py --cycles 2` gate with full TTS replies and wake re-arm. The
`AFE input stalled` warning at conversation start also appears in most earlier session logs
(148 of 184 sessions in the six-hour soak), so it is not caused by the chime.

# Rodak Identity And Wake Word

Rodak uses one product identity across the desktop Agent Runtime and RodakOS. The initial identity
is `罗达克（Rodak）`; the default displayed wake phrase remains `你好达克` until a device receives
another identity configuration.

## ESP-SR boundary

RodakOS currently loads `espressif/esp-sr` 2.2.2 and the Chinese `mn5q8_cn` MultiNet model.
MultiNet is a speech command recognizer. Its command graph can be changed at runtime with
`esp_mn_commands_add()` followed by `esp_mn_commands_update()`, so adding `luo da ke` does not
require retraining or repacking the model. Chinese MultiNet5 commands use pinyin units; a display
phrase such as `罗达克` and the recognizer command `luo da ke` are separate fields.

This path is useful for persistent and temporary user configurations, but it does not guarantee a
wake match. `rodak` is a short English-like phrase and should not be sent as-is to the Chinese
model. A product-grade `Rodak` wake word needs either an English MultiNet model or a dedicated
WakeNet model trained for that pronunciation. A custom WakeNet model still needs distance, speaker,
noise, microphone-array and false-accept testing before it can be treated as a supported product
model.

The firmware keeps the threshold at a configurable product policy level. The current baseline moves
the MultiNet threshold from `0.20` to `0.14` to improve distant speech recall. This is a measured
starting point, not an accuracy guarantee: lowering the threshold can increase false wakes.

## Shadow contract

Rodak writes this patch to the device desired shadow:

```json
{
  "voice_identity": {
    "name": "罗达克",
    "wakeWord": "罗达克",
    "wakeCommand": "luo da ke",
    "mode": "persistent",
    "revision": 2
  }
}
```

For a temporary configuration, add `mode: "temporary"` and an absolute `expiresAtMs`. RodakOS
validates the fields and the monotonic revision, writes persistent settings to NVS, updates the
MultiNet command graph without rebooting, and restores the persistent configuration after expiry.
Rejected or expired updates leave the previous command active.

The device reports the active configuration under `voice_identity` in the reported shadow with
`status` (`applied`, `rejected`, or `expired`), `runtime`, `model`, and an optional `error`. Rodak
uses the reported identity when rendering the Base System Prompt, so the Agent name changes only
after the device has accepted the configuration.

## Validation gates

Each candidate wake phrase must be evaluated on the target BigSmart hardware with:

- at least three speakers and at least three microphone distances;
- quiet room, music playback, speech overlap, fan/noise and reverberant room samples;
- false accepts per hour while the device is idle;
- false rejects across repeated utterances at each distance;
- one reboot, one temporary expiry and one rejected command rollback;
- a physical or UI manual wake fallback when local wake confidence is insufficient.

The acceptance report must publish recall and false-accept measurements. Neither a MultiNet runtime
command update nor a custom WakeNet training run should be described as guaranteeing every wake.

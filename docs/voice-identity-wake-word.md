# Rodak Identity And Wake Word

The `voice_identity` desired/reported shadow fields belong to the
[Rodak AIoT v1 contract](rodak-aiot-contract-v1.md). This document covers the
local ESP-SR command graph, identity persistence, expiry and wake-word policy.

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

Temporary requests require `mode: "temporary"` and an absolute Unix-millisecond `expiresAtMs`.
Revisions are positive uint32 integers; expiry is a positive JavaScript-safe integer. Names and
display wake words are limited to 32 UTF-8 bytes each, and recognizer commands to 128 bytes.
Control characters are rejected. These limits apply before persistence or MultiNet application.

The service serializes identity admission, revision checks, state publication and recovery.
It retains the complete highest accepted request independently of the active identity. Expiry
may restore an older persistent identity without lowering that watermark. Repeating the same
accepted revision and content does not reactivate an expired temporary identity; the same
revision with different content is rejected.

The reported object adds `revisionWatermark` and `activeConfirmed` alongside `status`, `runtime`,
`model` and optional `error`. `applied` / `expired` require confirmed configuration and storage;
`rejected` can retain a successfully restored earlier identity. `pending`, `pending_clock` and
`recovery_failed` do not assert a confirmed active identity. Rodak must not inject an unconfirmed
candidate into its Base System Prompt. State changes, including expiry, are reported by the MQTT
worker rather than waiting for another desired patch or reconnect.

## Persistence and recovery

The NVS namespace `voice_wake` now stores one bounded schema-1 JSON value at `identity`, containing
`persistent`, `active` and `lastAccepted`. `enabled` remains independent. A save explicitly commits
and rereads through a fresh handle. Errors are classified as confirmed unchanged or indeterminate;
an error returned after writing a new item is never treated as proof that storage stayed unchanged.
An indeterminate result stops identity-driven wake listening and freezes ordinary identity updates
until an explicit service reconstruction and successful storage/runtime recovery.

Only an absent `identity` key allows migration from the old `p_*` / `a_*` fields. Migration requires
both complete, valid configurations, or both entirely absent for defaults. Read/type errors,
partial fields, malformed records and unknown schemas do not fall back to defaults. The new key
is authoritative; legacy keys are neither updated nor deleted. Downgrading to firmware that only
reads those legacy keys may restore stale settings and is outside this migration guarantee.

One complete record prevents mixed-field snapshots; it does not prove real power-cut recovery.
Runtime configuration, persistence and listener restart have separate failure points. A failed
change reports rejection only after the previous runtime, storage and listening state are verified;
failed recovery remains explicit and cannot be reported as applied or expired.

## Clock and disabled listener

Unix expiry uses one system-clock snapshot with the existing product validity floor of 2020;
this is a plausibility check, not independent proof of accurate network time. Uptime is used only for elapsed duration:
after a trusted clock establishes the remaining lifetime, a backward wall-clock adjustment cannot
extend it; a forward adjustment may expire it sooner. Expiry does not reset the accepted revision.
After reboot, an unsynchronized clock cannot safely resume a stored temporary identity. The service
uses the persistent fallback while reporting `pending_clock`, then reconciles when time is valid.

Reading state while wake listening is disabled must not load the speech model or open microphone
input. A stored identity can remain `pending` and unconfirmed until explicit application or enabling
the runtime. Expiry can still update the stored record while disabled; that alone is not runtime
application evidence. None of these statuses proves acoustic wake recognition.

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

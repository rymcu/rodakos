# Rodak realtime voice v1 machine-readable contract

Machine-readable form of [Rodak realtime voice v1](../../../docs/rodak-realtime-voice-contract-v1.md).
The prose document is normative. The Rodak server's behavior is the source of truth.

| Path | Content |
| --- | --- |
| `schemas/<event>.schema.json` | One JSON Schema 2020-12 per control event, for example `session-open`, `output-start`. |
| `schemas/control-message.schema.json` | Union of the thirteen events, including the draft `turn.accepted` and `turn.progress`. Rejects legacy XiaoZhi keys at any depth. |
| `schemas/rav1-frame.schema.json` | RAV1 binary frame. Fixtures store it as lowercase hex. |
| `examples/valid/`, `examples/invalid/` | Fixtures that each schema MUST accept or reject. |

The `realtimeVoice` descriptor that AIoT sends is in
[`../../rodak-aiot/v1/schemas/realtime-voice-descriptor.schema.json`](../../rodak-aiot/v1/schemas/realtime-voice-descriptor.schema.json).

## Profiles

| Direction | Events | Schema describes |
| --- | --- | --- |
| Device → server | `session.open`, `wake.detected`, `input.start`, `input.stop`, `playback.abort`, `vad` | What RodakOS emits. Rodak accepts more where noted (for example any non-empty `playback.abort` reason). |
| Server → device | `session.ready`, `output.start`, `output.stop`, `session.end`, `error` | What Rodak emits and RodakOS accepts. |
| Server → device (draft) | `turn.accepted`, `turn.progress` | Contract §6.1. RodakOS implements `turn.accepted`; `turn.progress` is not implemented yet. Sent only after `session.open` `features.turn_accepted: 1` and `features.turn_progress: 1`. |

Schemas cover one message at a time. Ordering rules stay in the prose: one `session.ready` per
generation, increasing `playbackEpoch` and RAV1 sequence, stale-generation drops (§2, §5, §6).

## Check

```sh
build/contract-venv/bin/python -I tools/check_aiot_contract.py
```

The checker validates both contracts. `tests/app_model/voice_contract_fixture_test.cc` runs the
server-event and RAV1 fixtures through the firmware parsers and compares the device's own
`session.open`, `wake.detected`, `input.start` and `playback.abort` with the fixtures.

## Change rule

When Rodak changes a voice wire shape, update in one commit: the prose contract, the schema, at
least one valid and one invalid fixture, and the contract changelog (§10).

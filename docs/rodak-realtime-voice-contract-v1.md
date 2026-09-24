# Rodak realtime voice v1 contract

> Schema: `rodak-realtime-voice/v1`  
> Transport: WebSocket  
> Firmware: `main/phone_os/realtime_voice_contract.*` and
> `realtime_voice_transport.*`

This is the canonical RodakOS voice wire contract. It is independent of the
legacy XiaoZhi protocol. The Rodak server may translate XiaoZhi traffic in a
server-side adapter, but RodakOS must not accept XiaoZhi fields, event names,
automatic greetings, or `/xiaozhi` endpoints in a canonical descriptor.

## 1. Descriptor from AIoT

The confirmed AIoT token response may contain `realtimeVoice` (or its
snake_case alias). The descriptor is optional for MQTT-only devices. When it is
present, all of the following fields are required unless marked optional:

```json
{
  "schema": "rodak-realtime-voice/v1",
  "protocol": "rodak-realtime-voice",
  "protocolVersion": 1,
  "transport": "websocket",
  "endpoint": "wss://voice.example/api/v1/aiot/devices/realtime-voice",
  "authMode": "device-token",
  "uplink": {
    "codec": "opus",
    "sampleRateHz": 16000,
    "channels": 1,
    "frameDurationMs": 60
  },
  "downlink": {
    "codec": "opus",
    "sampleRateHz": 24000,
    "channels": 1,
    "frameDurationMs": 60
  },
  "limits": {
    "maxAudioFrameBytes": 8192,
    "maxControlBytes": 65536
  },
  "features": [],
  "capabilities": ["session", "audio-input", "audio-output"],
  "events": [
    "session.open", "session.ready", "input.start", "input.stop",
    "wake.detected", "playback.abort", "vad", "output.start",
    "output.stop", "session.end", "mcp", "error"
  ],
  "vadStrategies": ["server-authoritative"],
  "preferredVadStrategy": "server-authoritative"
}
```

The parser requires the schema, protocol, version, WebSocket transport,
`device-token` auth mode, endpoint, audio objects, limits, and the three arrays.
The endpoint must be `ws://` or `wss://` with an authority and path; query,
fragment, userinfo, and legacy XiaoZhi/WebSocket paths are rejected.

Uplink is fixed at Opus, 16 kHz, mono, 60 ms. Downlink is Opus, mono, one of
8/12/16/24/48 kHz, with a 5/10/20/40/60 ms frame duration. The negotiated
`maxAudioFrameBytes` is at most 64 KiB and applies to the Opus payload;
`maxControlBytes` is at most 256 KiB. The current defaults are 8 KiB and 64 KiB.
The firmware's WebSocket frame accumulator currently caps inbound text at 64 KiB,
even when a descriptor advertises a larger control limit. An RAV1 binary message
therefore occupies 12 bytes of header plus at most the negotiated Opus payload
limit.

Allowed capability names are `session`, `audio-input`, `audio-output`,
`speech-boundaries`, `interrupt`, `assistant-events`, and `mcp`. Allowed event
names are the twelve names shown above. `features` is an array of server-defined
strings and must not contain legacy markers.

The VAD strategies are:

| Strategy | Meaning |
| --- | --- |
| `device-authoritative` | Device VAD boundaries are authoritative. |
| `server-authoritative` | Audio still flows, but device VAD control events are suppressed. |
| `hybrid-fallback` | Device boundaries may be sent when server-side boundaries do not arrive. |

If `vadStrategies` is absent, the safe default is only
`server-authoritative`. The preferred strategy must be present in the offered
list.

## 2. WebSocket connection and generations

RodakOS opens the endpoint only after local MultiNet detects the configured wake
word. Idle wake monitoring never keeps a cloud WebSocket open. The connection
uses the AIoT access token and sends these headers:

```text
Authorization: Bearer <AIoT access token>
Protocol-Version: 1
Device-Id: <WiFi STA MAC>
Client-Id: <persisted RodakOS client UUID>
```

If the configured token already contains an auth scheme, the transport uses it
as supplied. Generation `0` is reserved for “not established”; every connection
generation starts at a non-zero value and wraps `UINT32_MAX` to `1`.

The client sends one `session.open` per connection generation. The server must
answer one matching `session.ready`; a duplicate or delayed ready event is an
error and cannot reset the session's playback or audio counters.

`session.open` has this shape (the `features` object reflects device support):

```json
{
  "event": "session.open",
  "protocol": "rodak-realtime-voice",
  "protocolVersion": 1,
  "generation": 7,
  "vadStrategies": ["server-authoritative"],
  "preferredVadStrategy": "server-authoritative",
  "features": {"mcp": false, "device_vad_epoch": 0},
  "uplink": {"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":60},
  "downlink": {"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60}
}
```

`session.ready` must include `event`, `protocol`, `protocolVersion`, matching
`generation`, `transport: "websocket"`, a non-empty `sessionId`, and a
negotiated Opus mono `downlink` object. `sampleRateHz` and `frameDurationMs`
must use the supported values above. `vadStrategy` is optional and defaults to
`server-authoritative`, but a supplied value must have been offered by the
descriptor.

## 3. Control events

All JSON control messages are UTF-8 objects with an `event` field. Events after
the handshake carry the negotiated `sessionId`.

| Direction | Event | Required or meaningful fields |
| --- | --- | --- |
| device → server | `wake.detected` | `sessionId`, `text` (configured wake phrase) |
| device → server | `input.start` | `sessionId`, optional `mode`: `auto-stop`, `manual-stop`, or `realtime` |
| device → server | `input.stop` | `sessionId` |
| device → server | `playback.abort` | `sessionId`, `reason`: `user`, `wake-word`, or `vad_detected`; optional `playbackEpoch` |
| device → server | `vad` | `sessionId`, `state` (`start`/`end`), `source`, positive `sequence`, `triggerMs`, optional epoch |
| device → server | `mcp` | `sessionId`, object `payload` |
| server → device | `output.start` | `sessionId`, optional `text` and `playbackEpoch` |
| server → device | `output.stop` | `sessionId`, optional `reason` and `playbackEpoch` |
| server → device | `session.end` | `sessionId`, optional `reason` |
| server → device | `mcp` | `sessionId`, object `payload` |
| server → device | `error` | optional `sessionId`, non-empty `code`, non-empty `message`, boolean `retryable` |

The initial wake turn is `wake.detected` followed by
`input.start` with `mode: "realtime"`. Later turns reuse the same session and
send another `input.start`; they do not repeat `wake.detected`.

After `output.start`, the device decodes binary Opus frames. `output.stop`
means no more audio for that utterance; the device drains the local TTS tail
before sending the next `input.start`. A user saying “再见” is an application
action that causes the server to send `session.end`; `goodbye` is not a wire
event in this contract.

An MCP `payload` must be a JSON object. Scalar and array payloads are rejected.
An `error` without all three error fields is rejected. Control messages that
exceed the effective transport or negotiated control limit are discarded and
are not delivered to the voice state machine.

## 4. RAV1 audio envelope

Every binary WebSocket audio message is one RAV1 frame:

```text
offset  size  field
0       4     ASCII magic: RAV1
4       4     unsigned sequence, big-endian, non-zero
8       4     unsigned payload length, big-endian
12      N     one Opus packet
```

The encoded payload length must exactly equal the remaining message length and
must not exceed `maxAudioFrameBytes`. Sequence values increase strictly within
the active generation/session. The header is used for downlink and is not
wrapped in a JSON envelope.

## 5. Stale-result and playback isolation

The transport gates every result by four pieces of state:

- connection `generation` must match the current WebSocket;
- `sessionId` must match the current negotiated session;
- `playbackEpoch` must increase for each explicit `output.start` and match its
  corresponding stop;
- RAV1 audio sequence must be strictly greater than the previous sequence.

Before an epoch is negotiated, zero may be omitted for compatibility. Once an
explicit epoch is observed, an epoch-less delayed `output.start` is rejected.
Late output, audio, errors, and lifecycle callbacks from an older generation
must be dropped; they must never reopen playback, replace the session, or start
a second WebSocket.

## 6. Device voice policy

The canonical product flow is:

```text
local wake → connect → session.open/ready → wake.detected → input.start
→ output.start + RAV1 audio → output.stop and drain
→ input.start (same session) or session.end
```

The follow-up window is 30 seconds and is refreshed when capture restarts or
fresh speech begins. A transport error, connection/listening watchdog, explicit
`session.end`, or follow-up timeout closes the session and re-arms local wake
monitoring. A watchdog must not cut off audio that is already draining.

During AEC/VAD-enabled playback, the capture path may remain open. A confirmed
barge-in sends `vad`/`playback.abort` on the current session and playback epoch,
closes only the decoder, and rejects late audio until the next `output.start`.
Manual wake interruption uses `reason: "wake-word"` and follows with a new
`input.start` on the same session.

## 7. Security and compatibility

AIoT provisioning is the authority for the endpoint and access token. The
serial provisioning protocol cannot submit a voice token or endpoint. Tokens,
secrets, and raw authorization headers must not be logged. Canonical parsing
rejects fields or markers associated with XiaoZhi, including `hello`, `listen`,
`tts`, `goodbye`, legacy `type`/`session_id`, and legacy WebSocket descriptors.

The canonical runtime must keep its own protocol-neutral session controller and
gate. The historical `xiaozhiSessionMachine` is for the legacy adapter only and
must not be reused by the Rodak realtime path.

## 8. Verification anchors

Host coverage is in `tests/app_model/realtime_voice_contract_test.cc`, including
descriptor validation, VAD negotiation, RAV1 parsing, MCP/error validation,
generation isolation, playback epochs, monotonic sequences, and bounded reconnect
policy. Product behavior and hardware gates are documented in
[Voice assistant integration](voice-assistant.md) and
[Voice AEC integration](voice-aec-integration.md).

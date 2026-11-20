# Rodak realtime voice v1 contract

> Schema: `rodak-realtime-voice/v1`  
> Transport: WebSocket  
> Firmware: `main/phone_os/realtime_voice_contract.*` and
> `realtime_voice_transport.*`  
> Canonical server: Rodak  
> Machine-readable form: [`contracts/rodak-realtime-voice/v1/`](../contracts/rodak-realtime-voice/v1/README.md)  
> Source of truth: the Rodak server's behavior. When this document and the server disagree, this
> document is fixed.

This is the canonical RodakOS voice wire contract. It is independent of the
legacy XiaoZhi protocol. Rodak currently has no XiaoZhi adapter. RodakOS must not
accept XiaoZhi fields, event names, automatic greetings, or `/xiaozhi` endpoints in a
canonical descriptor.

The keywords MUST, MUST NOT, SHOULD and MAY are used as in RFC 2119. Text marked *Informative*
describes the current RodakOS implementation.

| You are | Read |
| --- | --- |
| Implementing the server side | §1–§5, §7, §8 |
| Building a device or simulator | All sections, then [voice assistant](voice-assistant.md) for firmware timing |
| Checking a message | `contracts/rodak-realtime-voice/v1/schemas/` |

## 1. Descriptor from AIoT

Rodak sends `realtimeVoice` in the bootstrap and in every AIoT token response. *Informative:*
RodakOS also accepts the `realtime_voice` alias and treats the descriptor as optional for MQTT-only
servers. When it is present, all of the following fields are required unless marked optional.
The values below are what Rodak's AIoT routes emit (schema
[`realtime-voice-descriptor`](../contracts/rodak-aiot/v1/schemas/realtime-voice-descriptor.schema.json)):

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
  "capabilities": ["session", "audio-input", "audio-output", "speech-boundaries",
                   "interrupt", "assistant-events", "mcp"],
  "events": [
    "session.open", "session.ready", "input.start", "input.stop",
    "wake.detected", "playback.abort", "vad", "output.start",
    "output.stop", "session.end", "mcp", "error"
  ],
  "vadStrategies": ["device-authoritative", "server-authoritative", "hybrid-fallback"],
  "preferredVadStrategy": "hybrid-fallback"
}
```

The parser requires the schema, protocol, version, WebSocket transport,
`device-token` auth mode, endpoint, audio objects, limits, and the three arrays.
An installed [server trust pin](trusted-server-discovery.md) further requires WSS on
the current pinned HTTPS origin and the fixed expected TLS name before transmitting
the device Bearer token. This runtime restriction is stronger than the legacy parser's
scheme compatibility described below.
The endpoint must be `ws://` or `wss://` with an authority and path. The device rejects query,
fragment, userinfo, and legacy XiaoZhi/WebSocket paths.

Uplink is fixed at Opus, 16 kHz, mono, 60 ms. Downlink is Opus, mono, one of
8/12/16/24/48 kHz, with a 5/10/20/40/60 ms frame duration. The negotiated
`maxAudioFrameBytes` is at most 64 KiB and applies to the Opus payload.
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
word, or after it accepts an MQTT `voice.announce` command for a playback-only session (§8).
Idle wake monitoring never keeps a cloud WebSocket open. The connection
uses the AIoT access token and sends these headers:

```text
Authorization: Bearer <AIoT access token>
Protocol-Version: 1
Device-Id: <WiFi STA MAC>
Client-Id: <persisted RodakOS client UUID>
```

If the configured token already contains an auth scheme, the transport uses it
as supplied. Generation `0` is reserved for “not established”. Every connection
generation starts at a non-zero value and wraps `UINT32_MAX` to `1`.

The device also owns an interaction generation and a reconnect-policy generation. The interaction
generation identifies one local wake lifecycle. The policy generation is the currently committed
transport generation that failed. A successful retry receives a different transport generation. A
callback or failure for any other generation is stale and must be dropped. During command restoration,
the new generation may be temporarily bound only to queue its callbacks. It is not active until the
full restore sequence commits.

The client sends one `session.open` per connection generation. The server must
answer one matching `session.ready`. A duplicate or delayed ready event is an
error. It cannot reset the session's playback or audio counters.

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

`features` in `session.open` is a flag object whose values are only booleans or numbers. It is
distinct from the descriptor's string-array `features`. A server-initiated announce session
also carries `"announce": true` and `"announceSeq": <N>` (see §8). Ordinary wake sessions omit
both keys.

`session.ready` must include `event`, `protocol`, `protocolVersion`, matching
`generation`, `transport: "websocket"`, a non-empty `sessionId`, and a
negotiated Opus mono `downlink` object. `sampleRateHz` and `frameDurationMs`
must use the supported values above. `vadStrategy` is optional and defaults to
`server-authoritative`, but a supplied value must have been offered by the
descriptor.

`features.mcp` is always `false`: the firmware registers no MCP tools. Device control for
Rodak's models uses MQTT shadow effects ([volume](mqtt-volume-effects.md),
[light](mqtt-light-effects.md)). The session-scoped volume tools removed on 2026-10-21 are kept
as a [historical contract](archive/2026-10-21-voice-volume-mcp.md).

## 3. Control events

All JSON control messages are UTF-8 objects with an `event` field. Events after
the handshake carry the negotiated `sessionId`, which MUST be nonblank after trim. Each event has
a schema in [`contracts/rodak-realtime-voice/v1/schemas/`](../contracts/rodak-realtime-voice/v1/README.md).

| Direction | Event | Required or meaningful fields |
| --- | --- | --- |
| device → server | `wake.detected` | `sessionId`, `text` (configured wake phrase) |
| device → server | `input.start` | `sessionId`, optional `mode`: `auto-stop`, `manual-stop`, or `realtime` |
| device → server | `input.stop` | `sessionId` |
| device → server | `playback.abort` | `sessionId`, `reason`: `user`, `wake-word`, or `vad_detected`, and an optional `playbackEpoch` |
| device → server | `vad` | `sessionId`, `state` (`start`/`end`), `source`, positive `sequence`, `triggerMs`, optional epoch |
| device → server | `mcp` | `sessionId`, object `payload` |
| server → device | `output.start` | `sessionId`, optional `text` and `playbackEpoch` |
| server → device | `output.stop` | `sessionId`, optional `reason` and `playbackEpoch` |
| server → device | `session.end` | `sessionId`, optional `reason` |
| server → device | `mcp` | `sessionId`, object `payload` |
| server → device | `error` | optional `sessionId`, non-empty `code`, non-empty `message`, boolean `retryable` |

The initial wake turn is `wake.detected` followed by
`input.start` with `mode: "realtime"`. Later turns reuse the same session and
send another `input.start`. They do not repeat `wake.detected`.

After `output.start`, the device decodes binary Opus frames. `output.stop`
means no more audio for that utterance. The device drains the local TTS tail
before sending the next `input.start`. A user saying “再见” is an application
action that causes the server to send `session.end`. `goodbye` is not a wire
event in this contract.

`session.end.reason` is informational. The device handles every reason as an ordinary session
end and re-arms local wake. Rodak currently sends these reasons: `goodbye-intent` (the user said
goodbye), `wake-arbitration-lost` (§6), `announce-finished` and `announce-expired` (§8). A new
reason MUST NOT require device changes.

An MCP `payload` must be a JSON object. Scalar and array payloads are rejected.
Outer control JSON is bounded to twelve nesting levels before parsing. The transport still
validates the `mcp` envelope (current session/generation, object payload, no duplicate outer
fields) so a malformed frame fails the same way as other control events, but the service
discards valid `mcp` events: there is no dispatcher, response or side effect.
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

- connection `generation` must match the current WebSocket.
- `sessionId` must match the current negotiated session.
- `playbackEpoch` must increase for each explicit `output.start` and match its
  corresponding stop.
- RAV1 audio sequence must be strictly greater than the previous sequence.

Before an epoch is negotiated, zero may be omitted for compatibility. Once an
explicit epoch is observed, an epoch-less delayed `output.start` is rejected.
Late output, audio, errors, and lifecycle callbacks from an older generation
must be dropped. They must never reopen playback, replace the session, or start
a second WebSocket.

## 6. Session lifecycle

The canonical product flow is:

```text
local wake → connect → session.open/ready → wake.detected → input.start
→ output.start + RAV1 audio → output.stop and drain
→ input.start (same session) or session.end
```

Rules both sides rely on:

- An interaction is established only after `session.ready`, `wake.detected` and its first
  `input.start` succeed. A failure before that point ends the wake interaction.
- The device MAY reconnect an established interaction only after a retryable failure: network
  loss, connect or `session.ready` timeout, send failure, WebSocket `1011`/`1012`/`1013`, or a
  server `error` with `retryable: true`. It MUST NOT retry configuration or authentication errors,
  protocol errors, WebSocket `1002`/`4001`, `session.end`, user stop or deinitialization.
- Each reconnect performs `session.open/session.ready → wake.detected → input.start` on a new
  generation. Only a completed sequence commits it. Results of the old generation MUST be dropped.
- The server MUST keep the frames of one reply less than 20 s apart, or send `output.stop`. The
  device ends a session whose reply stalls for 20 s.
- A confirmed barge-in sends `vad` and `playback.abort` on the current session and playback epoch,
  and rejects late audio until the next `output.start`.

*Informative:* RodakOS timing (30 s follow-up window, three reconnect attempts with 250 ms–8 s
backoff, recorder pre-roll, AEC/VAD gating) is in
[voice assistant: wire behavior](voice-assistant.md#wire-behavior) and
[reconnect policy](voice-assistant.md#reconnect-policy).

### Multi-device wake arbitration (server side)

When several devices on one Rodak server report `wake.detected` within 1.5 s, the server treats
them as one wake. The server answers only the first device. Each losing device receives `session.end`
with `reason: "wake-arbitration-lost"`, and then the server closes that connection. RodakOS needs
no special handling: this is an ordinary session end that re-arms local wake.

The server also records each device's in-flight reply text. When a recognition result strongly
overlaps another device's playback during the capture window, the server discards it as echo.
This uses the server's `echoSuppression` switch and threshold. Barge-in probes compare against
this device's playback and other devices' playback. Full-turn recognition compares only against
other devices. This is server-side behavior, and the device wire format does not change.

## 7. Security and compatibility

AIoT provisioning is the authority for the endpoint and access token. The
serial provisioning protocol cannot submit a voice token or endpoint. Tokens,
secrets, and raw authorization headers must not be logged. Canonical parsing
rejects fields or markers associated with XiaoZhi, including `hello`, `listen`,
`tts`, `goodbye`, legacy `type`/`session_id`, and legacy WebSocket descriptors.

The canonical runtime must keep its own protocol-neutral session controller and
gate. It must not reuse the historical `xiaozhiSessionMachine`.

## 8. Server-initiated announce

The voice WebSocket is closed while the device is idle, so the server cannot speak on it
directly. For proactive announcements such as household alerts or due reminders, Rodak uses
this sequence (Rodak `docs/rodak-realtime-voice-protocol-v1.md` §8.2):

```text
MQTT voice.announce {announceSeq, priority} → device ACK ok | busy
→ connect → session.open {features.announce: true, announceSeq}
→ session.ready → output.start + RAV1 audio → output.stop
→ session.end (announce-finished)
```

1. Rodak sends the MQTT command `voice.announce` with `announceSeq` and `priority`. The command
   does not carry the spoken text. The text travels only in the authenticated voice session.
   Command validation, the busy rules, and ACK semantics are in the
   [AIoT contract](rodak-aiot-contract-v1.md#9-shadow-and-telemetry).
2. The device opens a normal connection with the regular headers. Its single `session.open`
   carries `features.announce: true` and the same `announceSeq`. The sequence is bound to that one
   `session.open`. The device clears the sequence immediately after that message. Later
   sessions never carry it.
3. An announce session is playback-only. The device does not open the microphone, and it does
   not send `wake.detected`, `input.start`, `vad`, or uplink audio. After `session.ready`, the
   device goes straight to the speaking phase and waits for `output.start`.
4. Playback follows the normal `output.start` / RAV1 / `output.stop` rules, including the
   generation, session, epoch, and sequence gates in §5. After the reply drains, the device does
   not open a follow-up turn.
5. `session.end` ends the session. Rodak sends `announce-finished` after the audio. If the
   `announceSeq` is unknown or older than the 30-second registration window, Rodak sends
   `announce-expired` immediately and no audio.

Device-side termination and failure rules:

- A local wake word or manual interrupt during an announce ends the session immediately. The
  device does not send `playback.abort` and does not turn the announce into a conversation. The
  device closes the WebSocket without sending `input.stop`. Rodak's protocol also allows
  `playback.abort` followed by a normal interaction, but RodakOS does not use that option.
- The 20 s reply-stall watchdog (§6) starts when the session commits, before `output.start`
  arrives. If the server stays silent for 20 s, the device ends the announce. If `output.stop`
  arrives but `session.end` does not, the 30-second follow-up timeout closes the session.
- An announce session never reconnects. Any transport failure ends it, because a retry would
  open an ordinary session without the announce flag. Rodak decides whether to re-queue the
  notice.
- Every announce takes exclusive audio focus. If an urgent announce starts while local music
  plays, the focus pauses the music. The music resumes when the announce releases focus.

Rodak creates no user turn for an announce session and writes no conversation history. On Rodak,
an `announceSeq` is a short-lived claim. Rodak never replays it after a server restart or a
device reconnect. A retried notice gets a new sequence. Rodak's `played` status means only that a
non-empty audio downlink completed in software on the original authenticated connection. It is
not evidence that the speaker produced sound.

## 9. Machine-readable contract and verification

[`contracts/rodak-realtime-voice/v1/`](../contracts/rodak-realtime-voice/v1/README.md) holds one JSON
Schema per control event, a `control-message` union that rejects legacy XiaoZhi keys, and a RAV1
frame schema (fixtures store the binary frame as hex). `python3 tools/check_aiot_contract.py` checks
it together with the AIoT contract. `tests/app_model/voice_contract_fixture_test.cc` feeds the
server-event and RAV1 fixtures to the firmware parsers and compares the device's own `session.open`,
`wake.detected`, `input.start` and `playback.abort` with the fixtures byte for byte (as JSON).

Other anchors: `tests/app_model/realtime_voice_contract_test.cc` (descriptor validation, VAD
negotiation, RAV1 parsing, generation isolation, playback epochs, reconnect policy, announce flags)
and `voice_assistant_reconnect_coordinator_test.cc`. Neither instantiates `VoiceAssistantService` or
the ESP/FreeRTOS transport. The announce state machine, Stop/Deinit cancellation and decoder reset
need device validation; see [voice assistant](voice-assistant.md) and
[voice AEC integration](voice-aec-integration.md). Rodak's server tests validate real downlink
events and RAV1 frames against `contracts/rodak-realtime-voice/v1/`.

## 10. Changelog

| Date | Change |
| --- | --- |
| 2026-11-20 | Added the reading guide and `contracts/rodak-realtime-voice/v1/`. Shortened §6 to the rules both sides rely on; firmware timing now lives in [voice assistant](voice-assistant.md). Corrected the descriptor example to Rodak's emitted value and the `session.end` reasons (`goodbye-intent`, not `shutdown`). |
| 2026-11-05 | Server-initiated announce (§8). |
| 2026-10-21 | Session-scoped voice MCP volume tools removed ([archive](archive/2026-10-21-voice-volume-mcp.md)). |

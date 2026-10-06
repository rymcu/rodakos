# Voice volume MCP and software receipts

Implemented on 2026-10-06 over the canonical
[realtime voice contract](rodak-realtime-voice-contract-v1.md). The production path is
`RodakRealtimeVoiceTransport` → `VoiceAssistantService` inbound queue / I/O task →
`VoiceVolumeMcp` → `AudioOutputService::ApplyVolume` → `AudioCodecOutput`.

The service advertises `session.open.features.mcp: true` only after installing its MCP
handler. These tools belong to the active voice connection. They are not MQTT commands and do
not keep a connection alive during idle wake monitoring.

## Tools and handshake

Send JSON-RPC 2.0 `initialize` with `protocolVersion: "2024-11-05"` inside the `mcp` event's
object `payload`, then `tools/list` / `tools/call`. `notifications/initialized` has no response
or side effect. Calls before initialization are rejected. The actual registry contains:

| Tool | Arguments | Operation |
| --- | --- | --- |
| `self.audio_speaker.set_volume` | Required integer `volume`, 0–100 | `volume.set` |
| `self.audio_speaker.volume_up` | Optional integer `step`, 1–100; device default 10 | `volume.adjust`, `direction: "up"` |
| `self.audio_speaker.volume_down` | Optional integer `step`, 1–100; device default 10 | `volume.adjust`, `direction: "down"` |

Unknown argument fields, non-integers and out-of-range values are rejected before mutation.
Relative operations read the current configuration, clamp the new result to 0–100, apply it,
and produce their revision under the same output mutex. An omitted `step` remains omitted
in the request and receipt's `requested` object; `effectiveStep` reports the device default.
The desktop must not convert a relative instruction into a guessed absolute value.

The inbound dispatcher accepts at most 4 KiB and eight JSON nesting levels, rejects duplicate
keys and embedded NUL values, and limits responses to 3 KiB. The transport also bounds the
outer control object before JSON parsing. Early MCP messages near `session.ready` enter the
existing bounded queue (64 events / 128 KiB total); they execute only after the voice listen
handshake commits. Queue pressure may discard droppable MCP/audio events.

The transport validates the outer `event: "mcp"`, active `sessionId` and connection generation,
then `ParseRealtimeVoiceMcpInbound` serializes only the object `payload` for the dispatcher.
Duplicate outer fields and non-object payloads are rejected. The service receives inner JSON-RPC,
never the complete `{event, sessionId, payload}` envelope. Replies use the existing
`BuildRealtimeVoiceMcpMessage` envelope builder and generation/session-checked send path.

## Correlation and receipt

Rodak's main process may add this metadata to `tools/call.params`; it does not belong in
model-controlled `arguments`:

```json
{
  "_meta": {
    "rodak/deviceEffect": {
      "effectId": "effect:volume:1",
      "parametersHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    }
  }
}
```

`effectId` accepts 1–128 ASCII letters, digits, `:`, `_` or `-`; `parametersHash` is 64 lowercase
hex characters. The device echoes these opaque correlation values; Rodak checks them against
its persisted effect. Unknown `_meta` keys are ignored, while malformed known correlation
fields are rejected. Compatibility calls without correlation still return a receipt, omitting
`effectId` and `parametersHash`.

`result.content` contains a short, honest text result. `result.structuredContent` carries:

```json
{
  "schema": "rodakos.volume-receipt.v1",
  "effectId": "effect:volume:1",
  "parametersHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "operation": "volume.adjust",
  "requested": {"direction": "up"},
  "effectiveStep": 10,
  "previousVolume": 60,
  "volume": 70,
  "configurationRevision": 1,
  "status": "configured",
  "application": "deferred",
  "persistence": "volatile",
  "physicalVerified": false
}
```

Absolute receipts use `operation: "volume.set"`, `requested: {"volume": 30}` and no
`effectiveStep`. An explicit relative step appears in both `requested.step` and `effectiveStep`.
The outer JSON-RPC `id` correlates the RPC; there is no inner request ID.

| Result | Receipt / state |
| --- | --- |
| Codec closed | `configured` / `deferred`; accepts RAM configuration without opening hardware |
| Open codec API succeeds | `configured` / `codec-applied`; shared RAM configuration commits |
| Codec API fails | `result.isError: true`, `rejected` / `unverified`, `errorCode: "codec-volume-rejected"`; prior configuration and revision retained |

Every accepted operation, including a same-value write, advances the output configuration
revision. Failed writes do not advance it; revision exhaustion rejects further commits instead
of wrapping. The revision is output-service-local, volatile, and unrelated to MQTT shadow versions.
`AudioService` reads this shared configuration for UI/playback; opening a track no longer writes
an independent cached volume back over it.

All receipts say `persistence: "volatile"` and `physicalVerified: false`. `codec-applied` means
the codec API accepted the write, not I2C register readback or measured speaker loudness. Failure
does not prove that hardware retained or returned to its previous physical state. The
[checked codec overlay](dependency-maintenance.md) propagates driver errors but does not add
physical measurement.

## Retry and lifecycle boundary

Within the current transport generation, a 64-entry ledger retains committed success and
failure responses without eviction:

- The same RPC ID and normalized request returns the cached response; changed arguments or
  correlation conflict and are rejected. Numeric and string IDs remain distinct.
- The same `effectId` with a new RPC ID and the same request returns the cached outcome with the
  new outer ID. Each alias consumes one ledger slot. Changed parameters or hash conflict.
- A full ledger rejects new entries, preserving old deduplication evidence. Repeated `initialize`
  does not clear it.
- Stop and a new transport generation clear the scope and require initialization again. Old
  generation events are discarded. A queued request canceled by Stop has no volume effect;
  Stop and the guard-to-volume-commit path share the service mutex.

This is session-scoped duplicate suppression. It does not promise exactly-once execution across
reconnects/reboots or recovery after a response is lost at the connection boundary.

Plain MQTT desired/reported volume retains its configuration-only evidence boundary. A separate
[MQTT effect contract](mqtt-volume-effects.md) now correlates single-dispatch volume writes on
`effects/receipt`; it does not upgrade ordinary shadow reports into execution evidence.

## Desktop manual entry

Rodak's MCP manager now routes manual calls to these same three native tools through its
persistent device-effect path. This host-side entry reuses the firmware implementation at
RodakOS baseline `15ffa5cc017512d6c6b451a59da6b7204045f032`; it adds no firmware endpoint,
transport or MCP tool. It requires the existing active canonical voice connection and does not
open a separate idle-control channel or keep the voice interaction alive.

The host issues correlation from its trusted execution context, scopes each user-request nonce
to its renderer owner, and binds dispatch to the authenticated device/token/session/connection.
Manual and voice calls share the session RPC allocator and device ledger. Ordinary manual
volume remains a write with no additional confirmation dialog; existing permission and risk
checks still apply. A same-name generic MCP tool retains its normal manual behavior.

Arguments remain unchanged, including omitted relative `step`. Omitted step and explicit
`step: 10` produce different deduplication request identities even though the current device
default is 10. A changed request cannot reuse the same effect ID. Manual aliases also consume
the existing 64-entry session ledger; opening a new connection to retry an unknown operation is
not safe. Repeated host nonces return the persisted outcome before resolving a replacement
session, and lost/late responses do not automatically repeat the tool.

Rodak's `scripts/run-rodakos-mcp-conformance.mjs` now runs five voice and eleven manual cases.
The manual path uses real pairing, canonical WebSocket and the original MCP session, with
JSON-RPC payloads delegated to the existing production `rodakos_mcp_fixture`. It covers the three
tools, duplicate concurrent nonces, a manual call during an existing voice turn, generic tools,
identity/connection changes, real codec-fake rejection and a dropped real response with the
normal 30-second timeout. The tested host ELF remains unchanged for this host-only feature
(SHA-256 `db82cece506b859970ea28e2c5b5e7425aa3ba3ef2a901929939ff8946616288`);
no firmware rebuild, flash or hardware acceptance is implied.

This CLI binds device generation 1 and handles inner MCP payloads. Its bridge is not a complete
`VoiceAssistantService`/ESP32 transport execution. The separate actual-service tests below
provide lifecycle and canonical-envelope evidence, not a single combined end-to-end hardware run.

## Verification

`tests/app_model/` covers the real atomic output/playback/adapter stack and MCP dispatcher.
`tests/voice_volume_service/` additionally compiles the real `VoiceAssistantService`, I/O loop,
queue and reconnect coordinator. Its ten cases drive public startup and installed inbound
callbacks through the production canonical-envelope parser and response builder. They retain
separate delayed-callback tests for the service's own stale-generation guard. Task scheduling,
WebSocket I/O, recorder, focus, Opus and codec facilities are faked.
See its [host workflow](../tests/voice_volume_service/README.md) and dated
[release evidence](ota-release-readiness.md#2026-10-06-voice-volume-mcp-validation).

These host checks do not establish hardware transport timing, NVS persistence, audible output,
physical rollback or release readiness.

# MQTT Command Semantics

> Part of [Rodak AIoT v1](rodak-aiot-contract-v1.md). Read §8 of the contract first.
> This document is normative for the replay ledger, exact stream Stop and stream result delivery.
> Text marked *Informative* describes the current RodakOS implementation.

The contract lists commands, payloads and error codes. This document explains when a command
runs, when it is replayed, and which results the server can still receive after reconnects.

## Terms

| Term | Meaning |
| --- | --- |
| Authority | The server identity that issued the current credentials. Replacing it clears the ledger. |
| Generation | Counter of MQTT client instances. A new client gets a new generation. |
| Connection epoch | Counter of connections within one generation. Each reconnect gets a new epoch. |
| Instance nonce | Never-reused number for one stream Start. It separates two sessions with the same `sessionId`. |
| Stream lease | Binding of generation, epoch, instance nonce and session ID. Signals and Stop need the current lease. |
| Completed-cleanup proof | Record that native Stop of a successful Start returned. Used for `already_stopped`. |
| Tombstone | Ledger entry kept after its reply body was dropped. Replay returns `command_result_unavailable`. |

## Command results and replay boundary

The ordinary failure envelope is `{"status":"error","errorCode":"unsupported_command"}`.
Camera/display failures use `camera_stream_unavailable` / `display_stream_unavailable`,
`camera_stream_busy` / `display_stream_busy`, `camera_stream_start_failed` /
`display_stream_start_failed`, `camera_stream_not_found` / `display_stream_not_found`, and
`camera_signal_rejected` / `display_signal_rejected`. Shared validation codes are
`invalid_payload`, `missing_session_id`, `invalid_signal` and `invalid_signal_encoding`.
Service availability is checked before session validation. Successful stream commands return
`result.sessionId`. Start also returns `result.transport: webrtc-datachannel` and
`result.startCommandNo`, the original Start command number. These are ordinary
command replies, without effect ID/hash, configuration revision, persistence or physical evidence.

### Exact stream Stop (026)

New clients identify the instance using the original Start command number:

```json
{"command":"display.stream.stop","sessionId":"preview-session","startCommandNo":"start-command-1"}
```

`camera.stream.stop` uses the same fields. A successful response contains
`result: {sessionId, startCommandNo, stopOutcome}`. `stopOutcome: stopped` means this request
revoked the matching active instance and its native Stop returned. `already_stopped` means the
latest successfully started instance had already completed that cleanup in the same MQTT
generation, connection epoch and authority. Neither outcome proves physical/DMA/idle-task
resource return. Native joins keep their existing blocking limits. No hard timeout is promised.

Each stream kind retains at most one volatile completed-cleanup proof. It is created only after
native Stop returns, for the captured owner of a successful Start. Revocation or a terminal callback
alone is insufficient. Failed/synchronously terminated starts cannot create a successful proof.
A replacement Start invalidates the previous proof for that stream kind, while epoch/authority
changes clear both. Late cleanup cannot repopulate a newer scope. Unknown sessions, wrong
`startCommandNo`, the other stream kind and stale proofs return the existing `_stream_not_found`
error without stopping a replacement. A present `startCommandNo` must be a nonempty string of
at most 128 bytes. Invalid values return `invalid_start_command_no`. Ambiguous duplicate JSON
keys or embedded JSON NULs in stream requests return `invalid_payload`.

An exact Stop's cached success is replayed only while its saved generation/epoch/instance nonce
still matches the latest completed-cleanup proof and current connection. Otherwise this delivery
returns `_stream_not_found`. The original final ledger record is not overwritten and no native
operation is repeated. Cached failures remain conservative failures, and changed raw payloads
still produce `command_conflict`. This is a response-scope restriction for exact Stop, not a
renewal of an old authorization.

For compatibility, a Stop with no `startCommandNo` can stop the current matching `sessionId`,
returning the actual start identity and `stopOutcome: stopped`. It does not receive the new
`already_stopped` behavior. Its existing cached-response behavior is unchanged. Such a legacy
request cannot distinguish an old logical session from a same-named replacement. New clients
must use a fresh Start command number for each instance and preserve it with the session.
The existing 64-command replay window and reboot/eviction limits still apply. Intentionally
reusing an evicted Start command number is not an unbounded exactly-once identity guarantee.

### Server-side result recording

Rodak now records native `status: error` replies as `failed`, retaining the first ACK body,
timestamp and failure detail. Its first terminal command result is frozen: duplicate or conflicting
replies and a late publish completion/failure cannot overwrite it. HTTP replies use the same
classification, and both transports associate command numbers with the authenticated device.
Desktop terminal freezing is independent of the firmware result-publication boundary below.

### Stream callbacks on the ACK topic

Camera/display callbacks reuse the start command's `/ack` topic. Their nested
`result.cameraStream` / `result.displayStream` messages carry `event: signal/state`, session ID,
peer state or SDP/candidate data. Rodak treats these as sideband messages: forwarding valid video
signaling does not settle the command, and later callbacks do not replace the first command result.
For `closed`, `disconnected` and `failed`, Rodak uses the topic's command number to find a persisted
start request, then matches its session ID, the live session's device and stream kind. Closed stops
that preview. Disconnected/failed fail it. New previews use new session IDs, so old-session events
cannot terminate them. The desktop session does not separately bind a unique starting command number.

## Replay ledger

The firmware keeps a volatile window of the latest 64 admitted command numbers within the same
authority. Identical raw payload bytes (SHA-256) replay the original final ACK without re-entering
the handler. Changed bytes return `command_conflict`, including JSON whitespace changes. An
in-flight duplicate neither executes nor emits an additional result. The original request completes.
Completed records are immutable. Reconnects,
ordinary token refresh and Stop/Start on the same service object retain the window. Authority
replacement clears it, and old completion tickets cannot enter the new authority.
Exact Stop successes additionally obey the completed-instance response-scope check described above.

### Limits and eviction

Command numbers are limited to 128 bytes and input payloads to 256 KiB. Cached reply bodies share
a 64 KiB budget. If a body cannot be retained, its recent completed ID stays as a tombstone and
replay returns `command_result_unavailable` without execution. This error does not prove that the
original command was never applied. New admissions evict the oldest completed record by first
admission order at capacity. Cache hits do not refresh that order, and in-flight records are not
evicted. Evicted commands and device reboots have no deduplication
guarantee. A cached start success is historical evidence and does not reopen a stopped stream.
Other ledger rejection codes are `command_limits_exceeded`, `command_hash_failed` and
`command_capacity_exceeded`.

## Result delivery across reconnects

The MQTT worker rejects stale generation/epoch messages before handler entry. ACKs and asynchronous
camera/display signal/state callbacks capture that original generation, epoch and ACK topic.
A bounded result queue rejects stale callbacks and is cleared on epoch changes. The SDK user-event
callback rechecks the scope and sends through direct QoS 0 publish with retain disabled, outside the
service lock so synchronous disconnect callbacks can re-enter safely. This path does not store an
SDK outbox item or fall back to ordinary Publish. A failed send is dropped without replay.

The queue allows 8 entries, at most 64 KiB per payload, 128 KiB total payload and 512 bytes per
topic. Overflow or unavailable SDK events drop results with a warning. An admitted send may still
complete on its original SDK connection. Success is not a Broker acknowledgement. These limits
do not prove that an unacknowledged command was never executed.

## Stream leases

Stream leases bind generation, epoch, a non-reused instance nonce and session ID. Start/Stop/remote
signaling share an operation mutex. Epoch changes revoke immediately and schedule worker cleanup
outside the MQTT mutex. SDK and peer callbacks never wait for that operation mutex. Service Stop
revokes admission before waiting for active operations. A Start that finishes after revocation
stops its own instance. Old callbacks cannot clear a replacement with the same session string.
`command_scope_expired` reports failed admission. Exhausted nonces reject with
`stream_instance_capacity_exceeded`. An action admitted before revocation may finish and is then
cleaned up. Neither this boundary nor the cache physically rolls back an already admitted action.

Signal publication requires the current active lease. Same-epoch terminal events may survive
cleanup only while their instance nonce is still the latest for that resource. Replacement drops
old terminal events. Data-channel close/disconnect map to the existing `closed` / `disconnected`
wire states.

### Remote input grants

*Informative.* Remote input retains explicit enable/disable, sequence checks and rate limits. Text, shortcuts,
pointers and deferred navigation carry a stream lease and a separate enable grant, checked at
the final LVGL action. Disable/re-enable cannot revive an old grant. Stale cleanup cannot release
a new owner's pointer. Controller destruction invalidates deferred navigation. Delayed control
ACKs also retain an opaque peer-instance token, checked at the actual sender under the peer API
lock, so sequence reuse alone cannot authorize a reply on the replacement peer.

### Other publications

Ordinary telemetry/shadow Publish
and effect receipt publishing retain their own SDK outbox behavior. Server-side effect receipt
matching to the original authenticated connection remains necessary.

## Verification

Host fixtures and their scope are described in [AIoT implementation notes](aiot-implementation-notes.md#command-host-fixture).

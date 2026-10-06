# Rodak AIoT v1 Cross-Repository Contract

> Contract: `rodak-aiot/v1`
> Canonical server implementation: Rodak `src/main/server/`
> Firmware implementation: RodakOS `main/phone_os/`

This document is the normative contract for device identity, owner binding,
credentials, unified MQTT, shadow state, and OTA coordination. The realtime
voice wire contract is separate: see [Rodak realtime voice v1](rodak-realtime-voice-contract-v1.md).
XiaoZhi compatibility is a Rodak server adapter and is outside this contract.

## 1. Identity

| Field | Value |
| --- | --- |
| `protocol` | `rodak-aiot` |
| protocol version | `1` |
| product key | `rymcu-bigsmart` |
| board discriminator | `rymcu_bigsmart` (Board Manager only) |
| device key | Stable device identity, derived from the WiFi MAC by default |
| binding status | `unbound`, `pending`, or `bound` |

The cloud product key is deliberately different from the Board Manager board
name. RodakOS sends the canonical AIoT identity and rejects a bootstrap that
identifies another product, protocol, or module. It does not read legacy
XiaoZhi bootstrap fields, WebSocket credentials, or legacy NVS namespaces.

## 2. Bootstrap and owner binding

Paths are relative to the configured server origin:

```text
GET  /api/v1/aiot/devices/bootstrap
POST /api/v1/aiot/devices/binding/request
GET  /api/v1/aiot/devices/binding/requests/{requestId}/status
POST /api/v1/aiot/devices/auth/token
POST /api/v1/aiot/devices/binding/unbind
```

`GET /bootstrap` is a capability and routing response. It does not authorize
cloud access by itself. A first-time device follows this sequence:

1. Generate a 32-byte random device secret (persisted as 64 hexadecimal
   characters) before sending any binding request.
2. Send `protocol`, `protocolVersion`, `productKey`, `deviceKey`, the device
   secret, firmware/client identity, and the `rodakos`/`rymcu_bigsmart`
   application and board markers to `binding/request`.
3. Persist the returned `requestId`, request token, pairing code, expiry, and
   status. The request token is used only for status polling.
4. Poll `binding/requests/{requestId}/status`. `pending` keeps MQTT and voice
   disabled. Only `confirmed` or `approved` may enable Device Cloud.
5. On confirmation, consume the access token and `unifiedMqtt` object from the
   status response and commit them as one pending/complete configuration.

The binding request carries the device identity and provenance used by the
server to distinguish RodakOS from legacy hardware:

```json
{
  "protocol": "rodak-aiot",
  "protocolVersion": 1,
  "productKey": "rymcu-bigsmart",
  "deviceKey": "<WiFi MAC>",
  "deviceSecret": "<64 hex characters>",
  "credentialSecret": "<same secret>",
  "clientId": "<persistent client UUID>",
  "application": {"name": "rodakos", "version": "<firmware>"},
  "board": {"type": "rymcu_bigsmart", "productKey": "rymcu-bigsmart"}
}
```

The create response must provide `requestId`, `requestToken`, `pairingCode`,
and an expiry. Status responses may use camelCase or snake_case aliases and
must provide a non-empty status; confirmed responses additionally provide the
access token and MQTT configuration.

Status values are normalized case-insensitively. `expired`, `rejected`, and
`revoked` clear the saved request and require an explicit new pairing. Unknown
values fail closed. HTTP 401/404/410 for a status request also invalidate the
saved request. A response that lacks required request fields or confirmed
credentials is rejected without partially updating the live configuration.

An already bound device with no pending request may refresh an expired access
token through `POST /auth/token` using `protocol`, `productKey`, `deviceKey`,
and its persisted credential secret. A rejected refresh clears the cached
token and MQTT configuration and returns to the owner-binding path. Routine
token refresh does not silently create a new binding.

Some Rodak server deployments retain `/devices/register` and
`/devices/activate` as compatibility or migration endpoints. They are not the
RodakOS v1 onboarding sequence and must not be used as the device's primary
flow.

## 3. Transaction and persistence rules

AIoT identity, access token, realtime voice descriptor, and unified MQTT values
are committed transactionally. Before a candidate token/configuration is
written, RodakOS persists a pending marker. A reset while that marker is set
must not expose the candidate as usable; the next boot retries or clears it
according to the binding status. A failed commit restores the previous complete
snapshot. Passwords, access tokens, request tokens, and device secrets never
appear in ordinary logs, serial replies, or shadow payloads.

Changing the serial provisioning URL clears cached AIoT, MQTT, and realtime
voice state before the next bootstrap. Serial provisioning supplies WiFi and
the bootstrap URL only; it never supplies MQTT or voice credentials. See
[Serial provisioning](serial-provisioning.md).

Unbind is server-confirmed. After `binding/unbind` succeeds, RodakOS clears the
access token, pairing request, MQTT values, and realtime voice descriptor. WiFi
credentials and the provisioning URL remain so the device can be paired again.
If the request or local cleanup fails, the state remains pending and is retried;
the device must not claim a completed unbind prematurely.

## 4. Unified MQTT v2 payload

The token/bootstrap response carries an object named `unifiedMqtt`. Firmware
also accepts `unified_mqtt`, `mqttConnectInfo`, and the documented camelCase or
snake_case aliases during the v1 compatibility window. The MQTT payload version
is independent from the `rodak-aiot` protocol version:

```json
{
  "protocol_version": 2,
  "broker_address": "mqtt.example",
  "broker_port": 1883,
  "username": "device",
  "password": "<access token>",
  "keepalive": 240,
  "device_key": "<stable device key>",
  "http_base_url": "https://api.example",
  "http_bearer_token": "<access token>",
  "home_enabled": false,
  "topics": {
    "telemetry": "devices/<deviceKey>/telemetry",
    "shadow_report": "devices/<deviceKey>/shadow/report",
    "shadow_desired": "devices/<deviceKey>/shadow/desired",
    "ota_notify": "devices/<deviceKey>/ota/notify",
    "ota_progress": "devices/<deviceKey>/ota/progress",
    "commands": "devices/<deviceKey>/commands/+",
    "pc_status": "devices/<deviceKey>/pc_status",
    "home_prefix": "devices/<deviceKey>/home/"
  }
}
```

The server should provide complete topic strings. RodakOS derives the
`devices/{deviceKey}/...` form only when a topic is absent. The access token is
reused as the controlled HTTP bearer credential for OTA. The firmware connects
after WiFi has an address, publishes telemetry and reported shadow state, and
subscribes to desired shadow, OTA notification, command, and PC status topics.

## 5. Shadow state and device properties

Device-to-server topics:

```text
devices/{deviceKey}/telemetry
devices/{deviceKey}/shadow/report
devices/{deviceKey}/ota/progress
devices/{deviceKey}/effects/receipt
```

Server-to-device topics:

```text
devices/{deviceKey}/shadow/desired
devices/{deviceKey}/ota/notify
devices/{deviceKey}/commands/{commandNo}
```

Command acknowledgement is published to the command topic with `/ack`
appended. Telemetry reports firmware, WiFi RSSI, volume, heap/uptime/slot
health, and valid `battery`/`charging` readings. Reported shadow includes
firmware, volume, light, battery/charging, and `voice_identity`. A failed
battery ADC read omits the field; it does not publish a guessed value.

The command surface accepts `ping` as a raw string, JSON string, or
`{"command":"ping"}`/`{"type":"ping"}` object. The acknowledgement is
`{"status":"ok","result":{"pong":true,"firmware":"..."}}` on
`devices/{deviceKey}/commands/{commandNo}/ack`. It also supports
`camera.stream.start/stop/signal` and `display.stream.start/stop/signal`, with a
required `sessionId`, mutually exclusive camera/display sessions and SDP/ICE
signaling; see [WebRTC peer integration](esp-peer-integration.md). Unsupported
commands return an `unsupported_command` error status. There is currently no
`volume.set` command handler on this acknowledgement channel.

### Command results and replay boundary

The ordinary failure envelope is `{"status":"error","errorCode":"unsupported_command"}`.
Camera/display failures use `camera_stream_unavailable` / `display_stream_unavailable`,
`camera_stream_busy` / `display_stream_busy`, `camera_stream_start_failed` /
`display_stream_start_failed`, `camera_stream_not_found` / `display_stream_not_found`, and
`camera_signal_rejected` / `display_signal_rejected`. Shared validation codes are
`invalid_payload`, `missing_session_id`, `invalid_signal` and `invalid_signal_encoding`.
Service availability is checked before session validation. Successful stream commands return
`result.sessionId`; start also returns `result.transport: webrtc-datachannel`. These are ordinary
command replies, without effect ID/hash, configuration revision, persistence or physical evidence.

Rodak now records native `status: error` replies as `failed`, retaining the first ACK body,
timestamp and failure detail. Its first terminal command result is frozen: duplicate or conflicting
replies and a late publish completion/failure cannot overwrite it. HTTP replies use the same
classification, and both transports associate command numbers with the authenticated device.
Desktop terminal freezing is independent of the firmware result-publication boundary below.

Camera/display callbacks reuse the start command's `/ack` topic. Their nested
`result.cameraStream` / `result.displayStream` messages carry `event: signal/state`, session ID,
peer state or SDP/candidate data. Rodak treats these as sideband messages: forwarding valid video
signaling does not settle the command, and later callbacks do not replace the first command result.
For `closed`, `disconnected` and `failed`, Rodak uses the topic's command number to find a persisted
start request, then matches its session ID, the live session's device and stream kind. Closed stops
that preview; disconnected/failed fail it. New previews use new session IDs, so old-session events
cannot terminate them. The desktop session does not separately bind a unique starting command number.

The firmware keeps a volatile window of the latest 64 admitted command numbers within the same
authority. Identical raw payload bytes (SHA-256) replay the original final ACK without re-entering
the handler; changed bytes return `command_conflict`, including JSON whitespace changes. An
in-flight duplicate neither executes nor emits an additional result; the original request completes.
Completed records are immutable. Reconnects,
ordinary token refresh and Stop/Start on the same service object retain the window; authority
replacement clears it, and old completion tickets cannot enter the new authority.

Command numbers are limited to 128 bytes and input payloads to 256 KiB. Cached reply bodies share
a 64 KiB budget; if a body cannot be retained, its recent completed ID stays as a tombstone and
replay returns `command_result_unavailable` without execution. This error does not prove that the
original command was never applied. New admissions evict the oldest completed record by first
admission order at capacity; cache hits do not refresh that order, and in-flight records are not
evicted. Evicted commands and device reboots have no deduplication
guarantee. A cached start success is historical evidence and does not reopen a stopped stream.
Other ledger rejection codes are `command_limits_exceeded`, `command_hash_failed` and
`command_capacity_exceeded`.

The MQTT worker rejects stale generation/epoch messages before handler entry. ACKs and asynchronous
camera/display signal/state callbacks capture that original generation, epoch and ACK topic.
A bounded result queue rejects stale callbacks and is cleared on epoch changes. The SDK user-event
callback rechecks the scope and sends through direct QoS 0 publish with retain disabled, outside the
service lock so synchronous disconnect callbacks can re-enter safely. This path does not store an
SDK outbox item or fall back to ordinary Publish; a failed send is dropped without replay.

The queue allows 8 entries, at most 64 KiB per payload, 128 KiB total payload and 512 bytes per
topic. Overflow or unavailable SDK events drop results with a warning. An admitted send may still
complete on its original SDK connection; success is not a Broker acknowledgement. These limits
do not prove that an unacknowledged command was never executed.

Stream leases bind generation, epoch, a non-reused instance nonce and session ID. Start/Stop/remote
signaling share an operation mutex. Epoch changes revoke immediately and schedule worker cleanup
outside the MQTT mutex; SDK and peer callbacks never wait for that operation mutex. Service Stop
revokes admission before waiting for active operations. A Start that finishes after revocation
stops its own instance; old callbacks cannot clear a replacement with the same session string.
`command_scope_expired` reports failed admission; exhausted nonces reject with
`stream_instance_capacity_exceeded`. An action admitted before revocation may finish and is then
cleaned up; neither this boundary nor the cache physically rolls back an already admitted action.

Signal publication requires the current active lease. Same-epoch terminal events may survive
cleanup only while their instance nonce is still the latest for that resource; replacement drops
old terminal events. Data-channel close/disconnect map to the existing `closed` / `disconnected`
wire states.

Remote input retains explicit enable/disable, sequence checks and rate limits. Text, shortcuts,
pointers and deferred navigation carry a stream lease and a separate enable grant, checked at
the final LVGL action. Disable/re-enable cannot revive an old grant; stale cleanup cannot release
a new owner's pointer. Controller destruction invalidates deferred navigation. Delayed control
ACKs also retain an opaque peer-instance token, checked at the actual sender under the peer API
lock, so sequence reuse alone cannot authorize a reply on the replacement peer.

Ordinary telemetry/shadow Publish
and effect receipt publishing retain their own SDK outbox behavior; server-side effect receipt
matching to the original authenticated connection remains necessary.

The independent [command host fixture](../tests/mqtt_volume_service/README.md#independent-command-fixture-and-tests)
uses the production registered MQTT callback, fragments, queue, worker, handler and captured
publications. Its host cases and Rodak's 21 cross-repository cases cover real ping shapes,
unsupported/malformed commands, unavailable camera/display services, terminal replay behavior
and device identity isolation. Host cases also model queued-versus-wire outbox stages, held SDK
events, reconnects, late stream callbacks and synchronous disconnect during direct publication.
The network SDK remains a fake; direct QoS 0 outbox semantics are separately checked against the
resolved ESP-MQTT source, whose hashes are recorded by the runner. The separate Rodak runner,
`scripts/run-rodakos-command-conformance.mjs`, records frozen source/binary evidence; these counts
do not extend the volume/light or MCP gate, and do not prove real WebRTC or hardware acceptance.

### Volume configuration and evidence

Desired shadow accepts `volume` inside either root `desired` or `state.desired`
and clamps it to 0–100. `AudioOutputService` reports its accepted runtime
configuration. `AudioService` and playback/UI read this shared output configuration. If an
already open codec returns an API error when setting volume, the previous shared configuration
is retained. Ordinary MQTT shadow reports show that retained configuration.
With the codec closed, setting volume accepts configuration without opening it;
the next playback open applies that configuration. If the initial volume API call
fails, the open attempt is closed and its format state is cleared for retry.

This is an API error boundary, not full hardware verification. RodakOS builds a
source-verified overlay for esp_codec_dev 1.5.7 so `esp_codec_dev_set_out_vol`
propagates the selected codec/software-volume driver's error and commits its cache
only on success. The original managed source remains unchanged; see
[dependency maintenance](dependency-maintenance.md). Software success or a reported
volume still does not establish I2C register application or audible speaker output.

Versioned volume desired messages now use root `version`/`shadowVersion` as an ordering
watermark; this is not the output configuration revision. Plain shadow reports still carry
no volume `effectId`, desired applied revision or per-effect result.
Connection establishment and unrelated state updates also publish ordinary shadow
reports. A matching reported value, a newer server shadow version, or a successful
MQTT publish cannot prove that a particular Agent Runtime effect executed on the
device. Rodak's local desired-state compensation likewise does not prove physical
restoration. A single desired publication may carry top-level
`_meta['rodak/deviceEffect']` with `rodak.mqtt-volume-effect.v1` correlation. Its result is
published separately on `devices/{deviceKey}/effects/receipt`, never inside reported shadow.
See [MQTT volume effects](mqtt-volume-effects.md) for strict validation, scope and replay rules.
The separate voice MCP path returns
[`rodakos.volume-receipt.v1`](voice-volume-mcp.md), with optional effect correlation and an
output-local configuration revision; that contract does not upgrade MQTT shadow evidence.
`voice_identity` has its own revision/status semantics below and must not be generalized
into a volume receipt.

### Light configuration and evidence

Desired `light` patches target discovered Board Manager RGB lights. Legacy messages without ID
select the first light; correlated `light.patch` requires a reported, real light ID and strict
nonempty enabled/brightness/color fields. `LightService` merges and applies under one lock and
commits accepted configuration/revision only on driver success. Failure retains prior values and
reports `available=false`, numeric `last_error`, and `application: unverified`; it cannot guarantee
physical rollback after a partial LED driver write.

`rodak.mqtt-light-effect.v1` publication metadata and `rodakos.mqtt-light-result.v1` results use the
same desired and independent effects/receipt topics as volume. The metadata schema selects only
one domain, so old fields in the desired shadow cannot cause unrelated mutations. Light receipts
include complete previous/current configuration and actual ID. Ordinary reports remain snapshots
and never complete an effect by value comparison. The 64-result cache permits continued writes;
version-bound IDs and an authority-wide monotonic watermark prevent re-execution after eviction.
See [MQTT light effects](mqtt-light-effects.md) for the full wire and cancellation contract.

This is the native RGB light capability, not LCD backlight or a brightness MCP tool. Voice identity,
local music playback, streaming and OTA retain their distinct execution and verification contracts.

### Voice identity

The shared `voice_identity` desired/reported object is:

```json
{
  "voice_identity": {
    "name": "罗达克",
    "wakeWord": "你好达克",
    "wakeCommand": "ni hao da ke",
    "mode": "persistent",
    "revision": 1,
    "expiresAtMs": 0
  }
}
```

Persistent identities have no effective expiry. Temporary identities require a
positive absolute Unix-millisecond `expiresAtMs`. RodakOS checks identity fields and reports
`status`, `runtime`, `model`, `revisionWatermark`, `activeConfirmed` and an optional `error` in the
shadow. The highest accepted request is retained independently of the active identity, so temporary
expiry does not lower the watermark or allow an old request to reactivate. Same-revision content
conflicts are rejected. A bounded schema-1 NVS record holds the complete persistent/active/accepted
snapshot, and uncertain writes or failed runtime recovery stop identity-driven wake listening.
`pending`, `pending_clock` and `recovery_failed` do not claim an active confirmed identity. State
changes are reported proactively; acoustic recognition and physical NVS power-loss acceptance remain
separate. See [identity persistence and recovery](voice-identity-wake-word.md).

## 6. Realtime voice handoff

The confirmed token response may include a `realtimeVoice` descriptor. It is
optional for MQTT-only rollouts, but when present it must satisfy the separate
[realtime voice v1 contract](rodak-realtime-voice-contract-v1.md). RodakOS
stores the descriptor with the AIoT token and uses the same access token for the
WebSocket `Authorization: Bearer` header. It never accepts a legacy XiaoZhi
WebSocket object as a substitute.

## 7. OTA ownership

Rodak publishes OTA task metadata on `ota/notify`. RodakOS obtains the manifest
and download ticket over the configured HTTP origin, stages the image on SD,
verifies its `manifestVersion: 2` metadata, size, SHA-256, and required `rsa2048-sha256` signature, and asks the immutable Recovery application to
write `ota_0`. Progress and the terminal result use `ota/progress` or the
matching HTTP result endpoint. Recovery, pending verification, confirmation,
rollback, and journal schema remain firmware facts documented in
[MQTT and SD Recovery OTA](mqtt-ota-sd-recovery.md).

## 8. Compatibility boundary and ownership

- Device identity and credential persistence: `DeviceCloudConfigService`.
- MQTT connection, topics, shadow, commands, and telemetry: `UnifiedMqttService`.
- OTA download, journal, Recovery handoff, and result reporting: `OtaUpdateService`
  and the factory Recovery image.
- Voice identity policy: `VoiceWakeService` through the shadow contract.
- Realtime voice transport: `RodakRealtimeVoiceTransport` under the separate
  `rodak-realtime-voice/v1` contract.
- XiaoZhi compatibility: Rodak server adapter only; it is not a RodakOS wire or
  credential path.

## 9. Verification anchors

- RodakOS host tests: `device_pairing_protocol_test.cc`,
  `device_pairing_policy_test.cc`, `mqtt_credential_refresh_policy_test.cc`,
  `voice_identity_test.cc`, and `realtime_voice_contract_test.cc`.
- Device workflow: `serial-provisioning.md` and the Recovery-safe
  `flash_and_test.ps1` path.
- Rodak server gates: device pairing, token refresh, MQTT/OTA, shadow, and
  hardware gate suites in the Rodak repository.
- Ordinary commands: independent `tests/mqtt_volume_service/mqtt_command_service_test.cc`
  and the command CLI, plus Rodak `tests/main-integration/command-ack-gateway.test.ts`.
  The service host target now includes 55 command cases plus 31 effect cases; its Debug and
  ASan/UBSan/leak runs pass. Input-controller validation uses production code and real host LVGL
  in `tests/remote_input`. Use the separate frozen-input runner and current
  [build evidence](ota-release-readiness.md) for exact source identity and scope. No serial, flash,
  device NVS or physical hardware run was performed for this slice.

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

The current command surface accepts `ping` as a raw string, JSON string, or
`{"command":"ping"}`/`{"type":"ping"}` object. The acknowledgement is
`{"status":"ok","result":{"pong":true,"firmware":"..."}}` on
`devices/{deviceKey}/commands/{commandNo}/ack`; unsupported commands return an
`unsupported_command` error status.

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
positive absolute `expiresAtMs`. RodakOS validates lengths, control
characters, revisions, and expiry boundaries, then reports `status`, `runtime`,
`model`, and an optional `error` in the shadow. An invalid desired update leaves
the previous identity active.

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
verifies its size and SHA-256, and asks the immutable Recovery application to
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

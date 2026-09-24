# Rodak AIoT v1 Cross-Repository Contract

> Contract: `rodak-aiot/v1`
> Canonical server implementation: the Rodak repository `src/main/server/`
> Firmware implementation: this repository `main/phone_os/`
> This contract describes the Rodak + RodakOS mainline. It is not a XiaoZhi voice protocol.

## 1. Canonical identity

| Field             | Value/source                                                                            |
| ----------------- | --------------------------------------------------------------------------------------- |
| protocol          | `rodak-aiot`                                                                            |
| protocol version  | `1`                                                                                     |
| product key       | `rymcu-bigsmart`                                                                        |
| device key        | Stable device identity, derived from the WiFi MAC by default                            |
| binding status    | `unbound`, `pending`, or `bound`                                                        |
| credential source | `aiot-register`, `aiot-pairing`, or `device-register` |

RodakOS accepts only the canonical `rodak-aiot` identity. It does not read, migrate, or recognize XiaoZhi bootstrap data, legacy WebSocket credentials, or legacy NVS fields. Compatibility for XiaoZhi firmware is a separate adapter in the Rodak server, not a RodakOS credential source.

## 2. Onboarding and credentials

Paths are relative to the configured server origin:

- `GET /api/v1/aiot/devices/bootstrap`
- `POST /api/v1/aiot/devices/binding/request`
- `GET /api/v1/aiot/devices/binding/requests/{requestId}/status`
- `POST /api/v1/aiot/devices/auth/token`
- `POST /api/v1/aiot/devices/binding/unbind`

First enrollment creates a binding request and waits for owner confirmation. Only after confirmation does the device receive an AIoT token and unified MQTT configuration. A bound device may refresh the token with its persisted credential secret. AIoT identity and MQTT settings are persisted as one pending/commit unit; a failed commit restores the previous complete snapshot.

Requests may use camelCase or snake_case aliases. The current Rodak server emits the established snake_case wire form for the `unifiedMqtt` payload; RodakOS must continue accepting both forms during the v1 compatibility window.

## 3. Unified MQTT payload

The configuration object is `unifiedMqtt` (firmware also accepts `unified_mqtt` and `mqttConnectInfo`). Its current wire payload uses `protocol_version: 2`; this is the MQTT payload version and is independent from the `rodak-aiot` protocol version above:

- broker: `broker_address`, `broker_port`, `username`, `password`, `keepalive`
- identity: `device_key`, `protocol_version`
- HTTP: `http_base_url`, `http_bearer_token` (the controlled HTTP bearer credential is reused for OTA)
- feature: `home_enabled`
- topics: `telemetry`, `shadow_report`, `shadow_desired`, `ota_notify`, `ota_progress`, `commands`, `pc_status`, `home_prefix`

The firmware also accepts the corresponding camelCase aliases (for example `brokerAddress`, `deviceKey`, and `protocolVersion`).

The firmware uses complete topics supplied by the server. It derives `devices/{deviceKey}/...` only when a device topic is absent. Passwords, tokens, and secrets must not be emitted in ordinary logs.

## 4. MQTT and shadow

Device namespace:

- `devices/{deviceKey}/telemetry`: device -> Rodak
- `devices/{deviceKey}/shadow/report`: device -> Rodak
- `devices/{deviceKey}/shadow/desired`: Rodak -> device
- `devices/{deviceKey}/ota/notify`: Rodak -> device
- `devices/{deviceKey}/ota/progress`: device -> Rodak
- `devices/{deviceKey}/commands/{commandNo}`: Rodak -> device; acknowledgement uses `/ack`

The shared voice identity fields are:

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

Reported state adds `status`, `runtime`, `model`, and an optional `error`. A temporary identity requires a positive `expiresAtMs`; a persistent identity has no effective expiry. Firmware validation covers length, control characters, revision, and expiry boundaries.

## 5. OTA contract

Rodak publishes task metadata through `ota/notify`. RodakOS obtains the manifest/download ticket over HTTP, stages the image on SD, verifies SHA-256, and lets Recovery write `ota_0`. Progress is reported through `ota/progress` or the matching HTTP result endpoint. Recovery layout, pending verification, running-image confirmation, and rollback remain firmware facts.

## 6. Ownership and compatibility

- Device identity/credentials: Rodak `DeviceStateService` + `DeviceCloudConfigService`
- MQTT connection/topic routing: Rodak `MqttBrokerService` + `UnifiedMqttService`
- Shadow desired/reported: both sides follow this contract; Agent Runtime does not access device DAOs directly
- OTA lifecycle: Rodak `OtaService` + `OtaUpdateService`/Recovery
- Agent capability, policy, and audit: Rodak Agent Runtime contracts
- `rodak-realtime-voice/v1`: canonical realtime voice is implemented by RodakOS and the Rodak server; XiaoZhi firmware compatibility exists only in a Rodak server adapter and is outside the RodakOS/canonical wire contract

## 7. Verification anchors

- Rodak: `tests/main-integration/server-runtime.test.ts`, `device-state-service.test.ts`, `server-security-regressions.test.ts`, `run-mqtt-ota-v2-gate.test.ts`, `device-cloud-hardware-gate.test.ts`
- RodakOS: `tests/app_model/device_pairing_protocol_test.cc`, `mqtt_credential_refresh_policy_test.cc`, `voice_identity_test.cc`, `tests/home_ui/`, `flash_and_test.ps1`
- Hardware verification must use the Recovery-safe `flash_and_test.ps1`; do not use default `idf.py flash` against the Recovery layout.

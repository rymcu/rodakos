# MQTT light patches and software receipts

Source baseline: `4a72e8c` plus this integration. This contract controls discovered Board Manager
RGB lights, including BigSmart `board_rgb`; it does not control LCD backlight. No brightness MCP
tool or generic light command is introduced.

## Native application

`LightService::ApplyLightPatch` resolves the actual light ID and, under one service lock, reads
the previous configuration, merges the supplied fields, calls production `ApplyBoardLight`, and
commits configuration/revision only after driver success. Local Smart UI setters, Toggle and
Apply use the same path. SetColor still enables the light; changing brightness alone does not.
Legacy partial RGB channels merge under that lock; correlated patches require all three channels.

The adapter scales RGB by brightness and calls `led_strip_set_pixel` followed by
`led_strip_refresh`. A failed call retains the last accepted software configuration and revision,
sets `available=false`, keeps the actual driver `last_error`, and marks application `unverified`.
The driver buffer or physical LEDs may already have changed. There is no claim of hardware
rollback or readback. A later successful application can restore availability.

## Desired frame

Publish once to `devices/{deviceKey}/shadow/desired`. Metadata belongs to this publication only;
it must not become persistent desired state. `shadowVersion` and optional `version` must agree.

```json
{
  "deviceKey": "device-1",
  "shadowVersion": 12,
  "desired": {"light": {"id": "board_rgb", "brightness": 30}},
  "_meta": {
    "rodak/deviceEffect": {
      "schema": "rodak.mqtt-light-effect.v1",
      "effectId": "light:12:12345678-1234-1234-1234-123456789abc",
      "parametersHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "dispatchId": "dispatch-12",
      "shadowVersion": 12,
      "operation": "light.patch",
      "requested": {"lightId": "board_rgb", "patch": {"brightness": 30}}
    }
  }
}
```

- `patch` is nonempty and accepts only `enabled` (boolean), `brightness` (integer 0–100), and
  `color` (exactly integer `r/g/b`, each 0–255). Unknown or duplicate requested keys are rejected.
- `lightId` and `dispatchId` are 1–128 ASCII characters from `[A-Za-z0-9:_-]`; the light ID must
  match a real discovered `LightState.id`. A UI default is not discovery or authorization.
- `effectId` is `light:<shadowVersion>:<canonical UUID>`, with no leading zero in the decimal
  version and UUID shaped `8-4-4-4-12` hexadecimal digits. Versions are positive safe integers.
- `parametersHash` is 64 lowercase hexadecimal digits. The device echoes this opaque host
  correlation value; it does not independently recompute the host's canonical hash.
- Root `desired` and/or `state.desired` must each contain the matching light ID and every patch
  field with equal values. Unrequested old desired fields may remain, but only the patch executes.
- The complete frame's metadata schema selects one effect domain. A light frame cannot apply an
  old desired volume; a volume frame cannot apply an old desired light. Invalid present effect
  metadata never falls through to legacy mutation.

Without effect metadata, legacy desired light patches remain supported. Missing ID selects the
first discovered light; an explicit ID must exist. Numeric brightness/channels keep legacy
clamping. Versioned legacy writes share the light watermark; unversioned legacy/local operations
remain outside correlated deduplication and produce no effect receipt.

## Result and report

Results use `devices/{deviceKey}/effects/receipt`, the same transport as volume results. The outer
schema is `rodakos.mqtt-light-result.v1` with the original `effectId`, `parametersHash`, `dispatchId`
and `shadowVersion`. A native attempt includes:

```json
{
  "schema": "rodakos.light-receipt.v1",
  "effectId": "light:12:12345678-1234-1234-1234-123456789abc",
  "parametersHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "operation": "light.patch",
  "requested": {"lightId": "board_rgb", "patch": {"brightness": 30}},
  "previous": {"id": "board_rgb", "enabled": false, "brightness": 60, "color": {"r": 32, "g": 160, "b": 255}},
  "current": {"id": "board_rgb", "enabled": false, "brightness": 30, "color": {"r": 32, "g": 160, "b": 255}},
  "configurationRevision": 1,
  "status": "configured",
  "application": "driver-applied",
  "persistence": "volatile",
  "physicalVerified": false
}
```

A failed native attempt returns `status: rejected`, `application: unverified`,
`errorCode: light-driver-rejected`, unchanged accepted configuration and unchanged revision.
Pre-execution errors return outer `errorCode` (`invalid-request`, `effect-conflict`, `stale-shadow`,
or `outcome-unknown`) without a receipt. Unusable correlation identifiers cannot receive a safely
correlated result and are dropped. Nothing here proves physical light output or persistence.

Ordinary `shadow/report.light` includes actual `id`, `enabled`, `brightness`, `color`, `available`,
numeric `last_error`, `configurationRevision`, and `application`. It remains a state snapshot with
no effect ID; equal values alone cannot complete an effect.

## Ordering, cancellation and retention

The light ledger stores the last 64 unique native-attempt outcomes, including rejection. Exact
semantic retries return their original result before watermark checks; a changed retained request
with the same effect ID is a conflict. Cache hits do not extend retention. New writes can continue
past 64 operations by evicting the oldest result.

A monotonically increasing authority-wide light version guards all discovered lights, alongside
per-light watermarks. A missing cache entry at or below that watermark returns `stale-shadow`
without touching the driver. Embedding the version in the effect ID prevents changing its version
after eviction; the authority-wide watermark also prevents retargeting an evicted ID to another
light with a lower watermark. Because device shadow versions are global, this conservatively
rejects an out-of-order lower version even when it targets a different light.

The ledger and watermarks are volatile and scoped to the current boot/authenticated binding.
Ordinary reconnect, Stop/Start and token refresh preserve them; binding replacement or unbind
clears them. They do not promise exactly-once execution across a reboot or authority change.

Production `UnifiedMqttService` holds its MQTT lock across epoch validation and native application,
including legacy patches. Stop, disconnect and credential changes share this boundary. Fragment
assembly and queued work carry both client generation and connection epoch. Receipts use the
existing SDK custom-event path; queued old-epoch receipts are discarded. Once an SDK outbox has
accepted a publication it cannot be recalled; the host must still validate the original binding.

## Validation boundary

`tests/light_service` compiles real `LightService` and `board_device_adapter`; only Board Manager,
LED and button SDK APIs are faked. Its 13 cases exercise real RGB scaling, logical ranges, local
setter semantics, invalid patches, first/mid-pixel failure, refresh failure/recovery, clear failure,
missing handles, atomic result snapshots and concurrent toggles.

`tests/mqtt_volume_service` now also compiles the real light service and adapter. Seventeen added
light tests cover fragmented routing, old desired values, malformed metadata, reports, 70 writes,
eviction/retargeting, failure replay, Stop, connection epochs, token refresh and binding replacement.
The existing fourteen volume tests remain in the same target. Other device services remain fakes.

The `rodakos_mqtt_light_fixture` CLI exposes actual production result publications and raw shadow
report bodies for cross-repository validation. See its [workflow](../tests/mqtt_volume_service/README.md)
and the [native driver test workflow](../tests/light_service/README.md). No hardware, serial or NVS
operation is performed by these host tests; release and physical acceptance remain separate.

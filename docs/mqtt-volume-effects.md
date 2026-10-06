# MQTT volume effects and software receipts

The MQTT `volume.set` path supports a versioned, correlated software result in addition to
ordinary desired/reported shadow state. It uses the same atomic `AudioOutputService::ApplyVolume`
as [voice MCP](voice-volume-mcp.md), with MQTT-specific ordering, identity and replay boundaries.
This protocol does not add a `volume.set` command on `commands/+` or relative volume in shadow.

## Single-dispatch request

Rodak adds metadata to one non-retained desired publication. Metadata stays outside persisted
`desired`; ordinary subscription/reconnect synchronization must not replay it:

```json
{
  "deviceKey": "device-1",
  "shadowVersion": 42,
  "version": 42,
  "desired": {"volume": 30},
  "state": {"desired": {"volume": 30}},
  "_meta": {
    "rodak/deviceEffect": {
      "schema": "rodak.mqtt-volume-effect.v1",
      "effectId": "effect-1",
      "parametersHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "dispatchId": "dispatch-1",
      "shadowVersion": 42,
      "operation": "volume.set",
      "requested": {"volume": 30}
    }
  }
}
```

The actual topic must match the configured desired topic. Correlation requires the outer
`deviceKey` to match the active MQTT identity, a safe positive integer shadow version, and an
integer volume from 0 to 100. Either `shadowVersion` or `version` is sufficient; both must agree
when present. The same rule applies to `desired.volume` and `state.desired.volume`. Metadata's
version and requested volume must match those outer values. Unknown fields inside the known
effect/requested object, duplicate relevant keys, malformed IDs and hashes are rejected.
`effectId` and `dispatchId` accept 1–128 ASCII letters, digits, `:`, `_`, `-`; hashes are 64 lowercase
hex digits. No token, password or device secret belongs in this payload.

Known metadata that is present but invalid never falls back to the legacy volume setter. When
identity fields are insufficient to correlate an error safely, the request produces no receipt.
Without effect metadata, legacy volume still accepts numeric `valueint`/0–100 clamp semantics.
A valid versioned ordinary volume update also advances the volume ordering watermark; equal
or older versions are ignored. Unversioned legacy volume retains its existing behavior.

## Separate result topic

The device publishes a result only on `devices/{deviceKey}/effects/receipt`, at QoS 0 and without
retain. Ordinary shadow reports remain ordinary state and never contain or imply effect receipts:

```json
{
  "schema": "rodakos.mqtt-volume-result.v1",
  "effectId": "effect-1",
  "parametersHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "dispatchId": "dispatch-1",
  "shadowVersion": 42,
  "receipt": {
    "schema": "rodakos.volume-receipt.v1",
    "effectId": "effect-1",
    "parametersHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "operation": "volume.set",
    "requested": {"volume": 30},
    "previousVolume": 60,
    "volume": 30,
    "configurationRevision": 1,
    "status": "configured",
    "application": "deferred",
    "persistence": "volatile",
    "physicalVerified": false
  }
}
```

`receipt` is the original atomic result: closed-codec configuration is `deferred`, an accepted
open-codec API call is `codec-applied`, and a failed codec call is `rejected` / `unverified` with
inner `errorCode: "codec-volume-rejected"`. Failure retains the prior configured volume and
revision; it does not claim that physical hardware was restored.

Protocol/lifecycle rejection uses exactly one outer `errorCode` instead of `receipt`:
`stale-shadow`, `effect-conflict`, `effect-capacity`, `invalid-request` or `outcome-unknown`.
An envelope must never contain both. Rodak verifies the trusted device/socket authorization,
effect/hash/dispatch/version and receipt fields before retaining a whitelisted software result.
`dispatchId` is correlation, not a credential. Firmware adds no separate session-nonce handshake;
Rodak owns the one-authenticated-socket dispatch boundary and rejects old-connection receipts.

## Ordering and lifecycle

The firmware retains at most 64 effect records in RAM without eviction. Exact duplicates return
the original serialized result before testing the current watermark, including after another
valid volume update. Reusing an effect ID with different hash, dispatch ID, volume or version
returns `effect-conflict`; a full ledger refuses new effects. New effects at or below the volume
watermark return `stale-shadow`. Result storage is reserved before mutation so output allocation
failure cannot cause a duplicate to repeat the codec operation.

The ledger and watermark survive same-boot Stop/start, same-authority network reconnects and
ordinary token refreshes. A device/binding/authority/routing change or explicit unbind isolates
them. Firmware restart loses both; this is not cross-boot exactly-once execution.

Transport cancellation is separate. Every MQTT CONNECT/DISCONNECT, Stop and credential replacement
advances a connection epoch and clears partial frames and pending results. The received client
generation and connection epoch travel through the real worker queue into the execution lock.
That lock spans the current-scope check, ledger decision and atomic volume call. Stop cannot
cancel between a successful scope check and the hardware/API commit.

Receipts retain their original expected scope in a bounded eight-item pending queue. The worker
posts an ESP-MQTT custom event while protecting the client pointer from destruction. The SDK task
rechecks scope and enqueues the result in `MQTT_USER_EVENT`; it never substitutes a new client or
epoch. This ordering matters because ESP-MQTT invokes callbacks while holding its recursive API
lock. Taking the service lock and then calling `enqueue` from another task would invert that
lock order. Lost/failed publication leaves the device outcome cached and the server outcome
unverified; no automatic command re-execution is implied.
Epoch cancellation removes service-owned pending results. A result already accepted into the SDK
outbox cannot be recalled and may reach the network after reconnect; Rodak's original authenticated
connection binding must reject that late result. Cancellation does not promise to retract bytes.

## Verification boundary

`tests/app_model/mqtt_volume_effect_test.cc` exercises production parsing, ordering, bounded
deduplication, atomic results and failure envelopes. `tests/mqtt_volume_service` compiles the
real `UnifiedMqttService` and drives its registered MQTT callback, fragmented input, worker queue,
desired handling and SDK-task output. Its fake SDK holds the same recursive API lock around
callbacks, including Stop/destroy interleavings. See the
[host and cross-repository fixture workflow](../tests/mqtt_volume_service/README.md).

The cross-repository test uses a real Rodak broker and forwards its complete desired envelope to
that C++ service fixture, then sends the unmodified production result back to the broker. SDK
network transport, hardware and board APIs remain host fakes. These checks do not prove physical
speaker volume, I2C readback, persistence, recovery after power loss or signed-release readiness.

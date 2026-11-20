# AIoT Implementation Notes

> *Informative.* This document describes how RodakOS implements
> [Rodak AIoT v1](rodak-aiot-contract-v1.md). It does not add wire requirements. A server
> implementer can skip it. Firmware maintainers keep it in sync with the code.

## Volume configuration and evidence

Desired shadow accepts `volume` inside either root `desired` or `state.desired`
and clamps it to 0–100. `AudioOutputService` reports its accepted runtime
configuration. `AudioService` and playback/UI read this shared output configuration. If an
already open codec returns an API error when setting volume, the previous shared configuration
is retained. Ordinary MQTT shadow reports show that retained configuration.
With the codec closed, setting volume accepts configuration without opening it.
The next playback open applies that configuration. If the initial volume API call
fails, the open attempt is closed and its format state is cleared for retry.

This is an API error boundary, not full hardware verification. RodakOS builds a
source-verified overlay for esp_codec_dev 1.5.7 so `esp_codec_dev_set_out_vol`
propagates the selected codec/software-volume driver's error and commits its cache
only on success. The original managed source remains unchanged. See
[dependency maintenance](dependency-maintenance.md). Software success or a reported
volume still does not establish I2C register application or audible speaker output.

Versioned volume desired messages now use root `version`/`shadowVersion` as an ordering
watermark. This is not the output configuration revision. Plain shadow reports still carry
no volume `effectId`, desired applied revision or per-effect result.
Connection establishment and unrelated state updates also publish ordinary shadow
reports. A matching reported value, a newer server shadow version, or a successful
MQTT publish cannot prove that a particular Agent Runtime effect executed on the
device. Rodak's local desired-state compensation likewise does not prove physical
restoration. A single desired publication may carry top-level
`_meta['rodak/deviceEffect']` with `rodak.mqtt-volume-effect.v1` correlation. Its result is
published separately on `devices/{deviceKey}/effects/receipt`, never inside reported shadow.
See [MQTT volume effects](mqtt-volume-effects.md) for strict validation, scope and replay rules.
The inner receipt is `rodakos.volume-receipt.v1` with an output-local configuration revision.
It does not upgrade ordinary shadow reports into execution evidence. The former session-scoped
voice MCP volume path was removed on 2026-10-21
([historical contract](archive/2026-10-21-voice-volume-mcp.md)).
`voice_identity` has its own revision/status semantics below and must not be generalized
into a volume receipt.

## Light configuration and evidence

Desired `light` patches target discovered Board Manager RGB lights. Legacy messages without ID
select the first light. Correlated `light.patch` requires a reported, real light ID and strict
nonempty enabled/brightness/color fields. `LightService` merges and applies under one lock and
commits accepted configuration/revision only on driver success. Failure retains prior values and
reports `available=false`, numeric `last_error`, and `application: unverified`. It cannot guarantee
physical rollback after a partial LED driver write. Each reported light object carries `id`,
`available`, `last_error`, `configurationRevision`, `application`, `enabled`, `brightness` and
`color`. Since 2026-11-05, Rodak's `rymcu-bigsmart` product thing model declares the same
fields. Rodak therefore does not treat these values as unknown properties.

`rodak.mqtt-light-effect.v1` publication metadata and `rodakos.mqtt-light-result.v1` results use the
same desired and independent effects/receipt topics as volume. The metadata schema selects only
one domain, so old fields in the desired shadow cannot cause unrelated mutations. Light receipts
include complete previous/current configuration and actual ID. Ordinary reports remain snapshots
and never complete an effect by value comparison. The 64-result cache permits continued writes.
Version-bound IDs and an authority-wide monotonic watermark prevent re-execution after eviction.
See [MQTT light effects](mqtt-light-effects.md) for the full wire and cancellation contract.

This is the native RGB light capability, not LCD backlight or a brightness MCP tool. Voice identity,
local music playback, streaming and OTA retain their distinct execution and verification contracts.

## Voice identity persistence

Persistent identities have no effective expiry. Temporary identities require a
positive absolute Unix-millisecond `expiresAtMs`. RodakOS checks identity fields and reports
`status`, `runtime`, `model`, `revisionWatermark`, `activeConfirmed` and an optional `error` in the
shadow. The highest accepted request is retained independently of the active identity, so temporary
expiry does not lower the watermark or allow an old request to reactivate. Same-revision content
conflicts are rejected. A bounded schema-1 NVS record holds the complete persistent/active/accepted
snapshot, and uncertain writes or failed runtime recovery stop identity-driven wake listening.
`pending`, `pending_clock` and `recovery_failed` do not claim an active confirmed identity. State
changes are reported proactively. Acoustic recognition and physical NVS power-loss acceptance remain
separate. See [identity persistence and recovery](voice-identity-wake-word.md).

## Verification evidence

### Command host fixture

The independent [command host fixture](../tests/mqtt_volume_service/README.md#independent-command-fixture-and-tests)
uses the production registered MQTT callback, fragments, queue, worker, handler and captured
publications. Its host cases and Rodak's 21 cross-repository cases cover real ping shapes,
unsupported/malformed commands, unavailable camera/display services, terminal replay behavior
and device identity isolation. Host cases also model queued-versus-wire outbox stages, held SDK
events, reconnects, late stream callbacks and synchronous disconnect during direct publication.
The network SDK remains a fake. Direct QoS 0 outbox semantics are separately checked against the
resolved ESP-MQTT source, whose hashes are recorded by the runner. The separate Rodak runner,
`scripts/run-rodakos-command-conformance.mjs`, records frozen source/binary evidence. These counts
do not extend the volume/light or MCP gate, and do not prove real WebRTC or hardware acceptance.

### Current test scope

Snapshot as of the 2026-11-20 split; re-check before you quote it:

- `tests/mqtt_volume_service/mqtt_command_service_test.cc` and the command CLI: 55 command cases
  plus 31 effect cases. Debug and ASan/UBSan/leak runs pass.
- Input-controller validation uses production code and real host LVGL in `tests/remote_input`.
- No serial, flash, device NVS or physical hardware run was performed for this slice.

Use the frozen-input runner and [build evidence](ota-release-readiness.md) for the exact source
identity of a release.

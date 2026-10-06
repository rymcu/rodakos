# RodakOS Roadmap

Updated: 2026-10-06. This stream-lifecycle and command-cache slice starts from `9940bea`.
Original-connection result publication and the SDK event-queue correction remain the baseline;
current source/build identity and validation are recorded in [release evidence](ota-release-readiness.md).

This is the active work list. Completed implementation details live in
[architecture](architecture.md) and the linked feature documents. The former Milestone 0–7
plan is retained in the [documentation archive](archive/README.md).

**Implemented** means the source path exists; **host-verified** means a recorded software test
exercised it; **hardware-verified** requires a recorded device or fixture run. A new host test or
firmware build does not change an existing hardware gate.

## Current baseline

- ESP32-S3 BigSmart: 16 MiB flash, 8 MiB PSRAM, ESP-IDF 6.0.2, LVGL 9.3,
  `esp_lvgl_port` 2.8, local Board Manager definitions and pinned component resolution.
- Static native app registry/host/navigation; Shell-owned Lock Screen and Control Center;
  exact-ID Home layouts, folders, one-save Arrange drafts, and active-plus-neighbors page residency.
- On-demand media hardware, SD/USB MSC, local MultiNet wake, canonical
  `rodak-realtime-voice/v1`, MQTT provisioning/credential refresh, and camera/display WebRTC peers.
- Signed appearance packages have recorded COM3 revision 14 and next-boot trial evidence.
  The signed firmware package `20261001-234748` is the last recorded device package, not an
  identity for subsequent local builds; see [appearance verification](appearance-verification.md).
- Audio volume changes retain the previous service/UI cache if the codec API reports a failed write.
  A closed codec accepts configuration without opening hardware; the next open applies it and
  fails with cleanup if the initial API call fails. Voice MCP now exposes absolute/up/down tools
  with atomic shared configuration and bounded per-session duplicate suppression. Its versioned
  software receipts carry optional effect correlation; MQTT shadow values still do not correlate
  effects, and neither path proves physical speaker output. See [voice volume MCP](voice-volume-mcp.md).
  Single-dispatch MQTT volume effects now use their own correlated result topic, bounded
  same-authority ledger and transport-epoch cancellation; see [MQTT volume effects](mqtt-volume-effects.md).
  The pinned codec dependency now uses a source-verified build overlay to propagate lower-level
  volume driver errors and commit its own cache only on success; see
  [dependency maintenance](dependency-maintenance.md).
- Release-soak collection requires increasing device uptime and both queued and successful
  app-launch completion evidence. Its 17 Python regression tests pass; the eight-hour device
  gate remains open. See [OTA release readiness](ota-release-readiness.md).
- Native RGB light patches now share atomic local/MQTT application, failure-retained configuration,
  discovered identity and correlated software receipts. Recent-64 retention with an authority-wide
  version watermark supports continuous updates without repeating evicted effects. This does not
  add a backlight MCP capability; see [MQTT light effects](mqtt-light-effects.md).
- Rodak's MCP manager now shares the three native audio tools and software receipts with voice
  sessions. The host adds owner/nonce deduplication and a lease on the existing authenticated
  connection, without adding a firmware tool or restarting voice interaction. Five voice and
  eleven manual cross-repository scenarios exercise the production payload dispatcher with SDK
  fakes; full service/envelope coverage remains a separate host target. This host-only change
  does not rebuild firmware or add hardware evidence; see [manual MCP entry](voice-volume-mcp.md#desktop-manual-entry).
- Ordinary command ACKs have desktop failure classification and first-terminal-result freezing.
  Firmware ACKs and camera/display signal/state now retain the original client generation, epoch
  and topic, then use a bounded SDK-event queue and direct QoS 0 publish without an outbox entry.
  Epoch changes discard old results; the host SDK model separates enqueue from wire publication.
  Stream operations now use revocable instance leases and serialized cleanup; remote input checks
  its stream and enable grant at final LVGL execution, and delayed ACKs retain the peer instance.
  A volatile latest-64 command cache replays final ACKs and rejects raw-payload conflicts. Eviction
  and device restart remain outside its deduplication guarantee. See the
  [command contract](rodak-aiot-contract-v1.md#command-results-and-replay-boundary),
  [host target](../tests/mqtt_volume_service/README.md#independent-command-fixture-and-tests)
  and [dated software evidence](ota-release-readiness.md#2026-10-06-stream-lifecycle-validation).

## Remaining acceptance and implementation work

| Area | Remaining work | Evidence / owner document |
| --- | --- | --- |
| Signed firmware release | Production trust root and Rodak signed manifest, wired immutable-Recovery migration, actual power cuts, eight-hour identified-build soak | [OTA release readiness](ota-release-readiness.md) |
| Resource recovery | Embedded image/camera/voice/MQTT allocation-failure runs and full LVGL exhaustion behavior; one-shot hooks are not arbitrary OOM recovery | [OTA release readiness](ota-release-readiness.md#resource-failures-and-soak) |
| Home and Shell | Physical bidirectional swipes, Arrange, page restoration, touch/readability, Shell settings/buttons, three-page turnover using the isolated 25-app flavor | [Home validation](home-layout-design.md#validation-boundary), [hardware flavor workflow](firmware-download.md#three-page-home-hardware-gate) |
| Voice | Six same-session turns, silence timeout, music resume, Recorder preemption, repeated wake suppression, TTS tail, AEC/barge-in, false accept/reject, idle CPU, heap/PSRAM and long-duration measurements | [Voice verification](voice-assistant.md#verification-gates), [AEC integration](voice-aec-integration.md) |
| Voice transport | Remaining terminal-error, stale-audio, and stop/deinitialization cancellation fault injection after recorded bounded reconnect/retry exhaustion | [Voice assistant](voice-assistant.md) |
| Voice identity | Unix expiry versus uptime, revision/status synchronization and high-water mark, atomic persistent/active record, explicit storage/runtime recovery failure | [Identity implementation limits](voice-identity-wake-word.md#shadow-contract) |
| Audio | Codec startup/shutdown and other API failure recovery, hardware volume failure/retry and audible output checks; MQTT/MCP receipts prove only volatile software configuration, and remaining mutations need separate contracts | [Volume MCP](voice-volume-mcp.md), [MQTT volume effects](mqtt-volume-effects.md), [dependency correction](dependency-maintenance.md) |
| Media/storage | Large-file and low-memory SD runs; missing-card/unsupported-media/no-tracks/camera-unavailable empty/error states; Recorder preemption, resume and failure recovery | [Architecture](architecture.md#service-notes), [troubleshooting](../TROUBLESHOOTING.md#audio-assistant-or-camera-unavailable) |
| RGB light | Board driver failures, physical output and recovery on the identified firmware; host receipts remain volatile software evidence. Backlight, voice identity, media and OTA require their own mutation contracts | [MQTT light effects](mqtt-light-effects.md) |
| Ordinary commands | Hardware validation of stream revocation/cleanup, sustained reconnect and resource contention, remote input and ACK timing; recent-cache software tests do not establish persistence, unbounded deduplication or physical rollback | [Command result boundary](rodak-aiot-contract-v1.md#command-results-and-replay-boundary) |
| Board telemetry | Validate battery/charging readings on hardware, plus I2C/SD/memory-pressure diagnostics | [AIoT device properties](rodak-aiot-contract-v1.md#5-shadow-state-and-device-properties) |

Already recorded COM3 voice, provisioning, WebRTC, and appearance gates remain accepted within
their documented limits. Ordinary regression reruns must not be presented as new production-key,
power-loss, acoustic, or resource-exhaustion evidence.

## Deferred design decisions

- Keep free drag deferred until physical paging and touch are proven together.
- Extend host LVGL coverage into PhoneSystem policy when hardware dependencies can be isolated.
- Refine service-backed status, app capability visibility in Settings/System Info, and consistent
  back/home transitions. Decide the preferred capture location between `/photos` and `/DCIM`.
- Native capabilities remain descriptive metadata. Any future untrusted MiniApp runtime needs
  a separate capability broker, per-app storage, resource limits, authenticated transport and
  signed staged installation. See [OpenOS comparison](openos-comparison.md).
- Swipe unlock remains a privacy cover until PIN, encrypted storage, Secure Boot and Flash
  Encryption policies are implemented.
- Consider Board Manager IMU metadata only when a first-class device type exists.

## Maintenance rules

Use [firmware download](firmware-download.md) for the supported build/package/flash flow.
Keep the Recovery partition layout and generated Board Manager ownership intact. Record current
build size/hash with its source baseline in the evidence document; preserve older device-package
identities as dated evidence. Test-only populations and fault-injection flavors must be disabled
before normal device use.

Continue cloud credential diagnostics and retain the non-voice serial/Device Cloud provisioning
gate as a regression check; its prior hardware acceptance does not remove ongoing diagnostics.

Runtime binary plug-in loading, execution of application images directly from SD, and a parallel
hand-written board layer remain outside the current scope.

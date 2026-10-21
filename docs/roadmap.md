# RodakOS Roadmap

Updated 2026-10-14. This page lists the current baseline and the open work.
[OTA release readiness](ota-release-readiness.md) and the feature documents linked below keep
the dated candidate evidence (packages, hashes, serial windows). The
[documentation archive](archive/README.md) keeps earlier versions of this page.

This page uses three evidence levels:

- **Implemented**: the source path exists.
- **Host-verified**: a recorded software test exercised the path.
- **Hardware-verified**: a recorded device or fixture run exercised the path.

A new host test or firmware build does not change an existing hardware gate.

## Release decision

Production release is **NO_GO**. These gates block the release:

- Production signing root deployment and a production-signed Rodak OTA
  ([#30](https://github.com/rymcu/rodakos/issues/30)).
- Real power cuts during download, FAT flush/rename and flash/NVS writes
  ([#29](https://github.com/rymcu/rodakos/issues/29)).
- The eight-hour qualification soak on an identified ordinary build
  ([#28](https://github.com/rymcu/rodakos/issues/28)).

GitHub Actions give a useful regression signal, but they are not a delivery gate. The delivery
gates are local tests, local builds, package verification and recorded device windows. See
[host CI](host-ci.md) and [firmware CI](firmware-ci.md).

## Current baseline

- **Platform**: ESP32-S3 BigSmart, 16 MiB flash, 8 MiB PSRAM, ESP-IDF 6.0.2, LVGL 9.3,
  `esp_lvgl_port` 2.8, `rodakos_hal_boards` + Board Manager, pinned dependency graph with
  source-verified build overlays ([dependency maintenance](dependency-maintenance.md)).
- **OTA**: immutable Recovery, isolated A/B journal, SD-staged `ota_0` updates, per-task RSA
  manifests attached in Rodak, offline production signing tools
  ([production signing](production-signing.md)). End-to-end Rodak OTA (098/099) passes on
  hardware. All software-reset rows of the interruption matrix (100/101) also pass on hardware.
- **Resource recovery**: these failures pass on hardware:
  - Single-point allocation failures for Home, image, Camera, voice, MQTT, OTA download, TLS and
    NVS (095, 102).
  - A real STREAMON OOM with retry (096).
  - An arbitrary LVGL task allocation failure (097). LVGL core allocation sites recover through a
    panic reboot.

  After stop, local and remote Camera return the largest internal/DMA block to 8,192 bytes
  (085, 094). This is also true for the first voice session after Camera (106).
- **Camera and voice**: Camera-time SRTP no longer uses internal DMA bounce buffers (104). A voice
  session stops the remote Camera stream before it opens its WebSocket (105).
- **Networking**: USB-installed server trust with pinned HTTPS/MQTTS/WSS, bounded DNS-SD route
  verification, same-authority MQTT client replacement and connection-epoch result isolation
  ([trusted server discovery](trusted-server-discovery.md), [credential refresh](mqtt-credential-refresh.md)).
- **Apps and Shell**: static registry, Shell-owned Lock Screen and Control Center, exact-ID Home
  layout with folders, and widgets kept only for the active page and its neighbors
  ([Home layout](home-layout-design.md)).
- **Voice**: local MultiNet wake, `rodak-realtime-voice/v1`, volume MCP tools with software receipts,
  versioned voice identity ([voice assistant](voice-assistant.md)).

## Open work

| Area | Remaining work | Owner document |
| --- | --- | --- |
| Production release | Generate and deploy the production key. Do a wired Recovery migration with readback. Run an offline-signed Rodak OTA. | [Production signing](production-signing.md), [#30](https://github.com/rymcu/rodakos/issues/30) |
| Power interruption | Build a controlled power fixture. Cut power during download, FAT flush/rename, flash erase/write and NVS writes. | [OTA release readiness](ota-release-readiness.md#interruption-matrix), [#29](https://github.com/rymcu/rodakos/issues/29) |
| Qualification soak | Run the eight-hour soak on an ordinary build with the 8 KiB gate for closed Camera. Set the open-Camera threshold from a TLS/MQTT/screen test during Camera. | [OTA release readiness](ota-release-readiness.md#resource-failures-and-soak), [#28](https://github.com/rymcu/rodakos/issues/28) |
| Resource failures | DMA/IRQ/cache-off failures, physical SD removal and slow cards. Voice during the local Camera app is not tested. By design, Camera audio focus blocks it. | [Camera diagnostics](camera-teardown-diagnostics.md), [#28](https://github.com/rymcu/rodakos/issues/28) |
| Camera image | Physical color, exposure and optics with a test chart. Camera supply rails. | [Camera diagnostics](camera-teardown-diagnostics.md) |
| Trusted server recovery | New-address and single-interface roaming, stale DNS caches, AP isolation, unknown SSIDs, damaged/missing trust recovery | [Trusted server discovery](trusted-server-discovery.md), [#33](https://github.com/rymcu/rodakos/issues/33) |
| Home and Shell | Physical swipes, Arrange, page restoration, readability, three-page turnover with the 25-app test flavor | [Home validation](home-layout-design.md#validation-boundary), [#27](https://github.com/rymcu/rodakos/issues/27) |
| Voice | Music/Recorder coexistence, repeated-wake suppression, TTS tail, acoustic AEC/barge-in, false accept/reject, long-duration measurements | [Voice verification](voice-assistant.md#verification-gates), [AEC integration](voice-aec-integration.md), [#23](https://github.com/rymcu/rodakos/issues/23) |
| Cloud diagnosis | Real credential expiry/revocation, DNS/HTTP cancellation latency, device typography and touch | [Cloud diagnosis](voice-assistant.md#device-cloud-diagnosis-and-recovery), [#24](https://github.com/rymcu/rodakos/issues/24) |
| Voice identity | NVS power-cut/reboot, clock sync, model recovery under pressure, multi-speaker and two-device isolation | [Identity validation](voice-identity-wake-word.md#validation-gates) |
| Audio and light | Codec start/stop failure recovery, audible output, RGB driver failures and physical light output | [MQTT volume effects](mqtt-volume-effects.md), [MQTT light effects](mqtt-light-effects.md) |
| Media and input | Physical SD/touch, remote-input timing under load, GT911 touch | [Media browsing](media-browsing.md), [Media save](media-save.md), [Command boundary](rodak-aiot-contract-v1.md#command-results-and-replay-boundary) |
| Board telemetry | Battery/charging readings on hardware, I2C/SD/memory-pressure diagnostics | [AIoT device properties](rodak-aiot-contract-v1.md#5-shadow-state-and-device-properties) |

The recorded COM3 voice, provisioning, WebRTC and appearance gates stay accepted within their
documented limits. An ordinary regression rerun is not new evidence for the production key, power
loss, acoustics or resource exhaustion.

## Deferred design decisions

- Keep free drag deferred until physical tests prove paging and touch together.
- Extend host LVGL coverage into PhoneSystem policy when the tests can isolate its hardware
  dependencies.
- Refine service-backed status, app capability visibility in Settings and System, and consistent
  back/home transitions. Decide the preferred capture location: `/photos` or `/DCIM`.
- Native capabilities stay descriptive metadata. A future untrusted MiniApp runtime needs a
  separate capability broker, per-app storage, resource limits, authenticated transport and
  signed staged installation. See [OpenOS comparison](openos-comparison.md).
- Swipe unlock stays a privacy cover until the firmware has PIN, encrypted storage, Secure Boot
  and Flash Encryption policies.
- Consider Board Manager IMU metadata only when Board Manager has a first-class device type for it.

## Maintenance rules

- Use [firmware download](firmware-download.md) for the supported build/package/flash flow. Keep
  the Recovery partition layout and the Board Manager ownership of generated code.
- Record the source, package, hashes and device windows of each new candidate as a dated section.
  Put it in [OTA release readiness](ota-release-readiness.md) or in the feature document that owns
  it. Then update the baseline and the open-work table on this page.
- Do not add dated logs to this page or to `README.md`.
- Disable test-only populations and fault-injection flavors before normal device use.
- Runtime binary plug-in loading, execution of application images directly from SD, and a parallel
  hand-written board layer stay outside the current scope.

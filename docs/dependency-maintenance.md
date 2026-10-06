# Dependency Maintenance

`main/idf_component.yml` pins direct dependencies; `dependencies.lock` records the resolved graph.
Keep ESP-IDF 6.0.2 and the existing SDK versions unless a dependency change is explicitly reviewed.
The component manager owns `managed_components/`; local edits there are not a durable repair.

## Codec volume overlay

RodakOS keeps `espressif/esp_codec_dev` at 1.5.7. That upstream release's
`esp_codec_dev_set_out_vol` discards both hardware and software `set_vol` return values and commits
its cached volume before either call. The project owns a replacement for this one function under
`patches/esp_codec_dev/1.5.7/`, retaining the upstream Apache-2.0 source attribution.

The replacement preserves software-volume priority, automatic software volume with no hardware
codec, volume curves and the existing readiness checks. It returns the selected driver's exact
error code, including negative codes, and commits the cached volume only on `ESP_CODEC_DEV_OK`.
Failure of a selected software handler does not fall back to hardware. Unsupported paths retain
the previous cache. This patch does not change mute, gain, format/open or close error handling.

After the root CMake `project()` has resolved dependencies, `cmake/codec_volume_patch.cmake` runs
`tools/prepare_codec_volume_patch.py`. The generator checks all of these before producing output:

- The project's exact 1.5.7 pin and the lock's version, registry source and component hash.
- The resolved `.component_hash` and component manifest, including the upstream repository commit.
- The SHA-256 of the complete original `esp_codec_dev.c`; line endings are normalized to LF.

Reviewed provenance and hashes are in `patches/esp_codec_dev/1.5.7/provenance.json`.
The original managed files remain untouched. The generated source lives at
`<build>/rodak_patches/esp_codec_dev/esp_codec_dev.c`, and CMake replaces exactly one source entry
on the resolved codec component target. Other codec sources, headers and compile settings remain
those of the resolved component. Fresh dependency resolution and existing dependency builds use
the same hook; no manual pre-build patch command is required.

Repeated generation preserves identical output and its modification time. Patch/provenance,
manifest, lock and upstream source changes trigger CMake configuration again. A missing generated
file is recreated by its build rule. Any unreviewed version, source or target-layout drift stops
configuration rather than compiling an unpatched fallback.

## MQTT custom-event queue overlay

The resolved `espressif/mqtt` 1.0.0 uses one event-loop queue for native lifecycle events and
custom events. With the default one-slot queue, a queued custom event can occupy the slot needed
by a disconnect notification. Its synchronous dispatch ignores a failed post, so application
connection state can remain stale. Raising capacity alone does not establish a lifecycle reserve.

`cmake/mqtt_event_patch.cmake` and `tools/prepare_mqtt_event_patch.py` generate checked build copies
from `patches/esp_mqtt/1.0.0/`. Managed sources stay untouched. The overlay gives custom events a
separate bounded queue; the SDK event loop takes one custom event under its API lock and dispatches
it immediately through the otherwise native event queue. Custom producers therefore cannot occupy
the native lifecycle slot. Source/header identities and resolved component provenance are checked
before CMake substitutes the generated implementation and private header.

RodakOS shares one pending wake notification between command results and effect receipts. Result
payloads retain their own queue limits and connection checks; an old wake notification carries no
authority to publish an old result. Command ACKs use direct QoS 0 publishing, while effect receipts
retain their existing outbox and server-side correlation rules. This correction does not turn
network delivery, allocation failures or device hardware into guaranteed outcomes.

The independent `tests/mqtt_event_patch` target compiles native/custom dispatch and event-pump
functions extracted from the generated SDK copy. Seven scenarios include the upstream failure
control, split queues, nested disconnect and allocation-failure retry; FreeRTOS and event-loop
facilities are fakes. Eight Python checks verify source drift rejection and generation. The
service-level fake remains in `tests/mqtt_volume_service`; full SDK compilation is a firmware
build check, not a host network test.

```powershell
$env:RODAKOS_IDF_PATH = 'C:/esp/v6.0.2/esp-idf'
python -m unittest discover -s tests/mqtt_event_patch -p 'test_*.py'
wsl -d Debian -- cmake -S /mnt/d/workspace/rodakos/tests/mqtt_event_patch `
  -B /home/ronger/.cache/rodakos-mqtt-event-patch -G Ninja `
  -DRODAKOS_IDF_PATH=/mnt/c/esp/v6.0.2/esp-idf
wsl -d Debian -- cmake --build /home/ronger/.cache/rodakos-mqtt-event-patch
wsl -d Debian -- ctest --test-dir /home/ronger/.cache/rodakos-mqtt-event-patch --output-on-failure
```

The release host script also runs this target with sanitizers and the Python checks. Pass the
Linux-visible `RODAKOS_IDF_PATH` (or `IDF_PATH`) when invoking that script in WSL; it refuses an
unavailable source tree instead of skipping the new gate. Retain the source pin when upgrading
MQTT or ESP-IDF, review event-loop semantics again, and rerun these checks plus a firmware build.

## Codec validation

Resolve the pinned dependencies through the normal [firmware build](firmware-download.md) first.
The dedicated host target compiles the generated full upstream `esp_codec_dev.c`, real
`audio_codec_sw_vol.c` and `esp_codec_dev_if.c`, using upstream headers. It fakes only hardware/data
callbacks and ESP error/log facilities. The no-codec case exercises real software PCM gain.

```powershell
wsl -d Debian -- bash -lc '
  cmake -S /mnt/d/workspace/rodakos/tests/codec_volume \
        -B ~/.cache/rodakos-codec-volume -G Ninja -DCMAKE_BUILD_TYPE=Debug &&
  cmake --build ~/.cache/rodakos-codec-volume &&
  ctest --test-dir ~/.cache/rodakos-codec-volume --output-on-failure
'
python -m unittest discover -s tests/codec_volume -p 'test_*.py'
```

`tools/run_release_host_checks.sh` includes this target with ASan/UBSan and leak detection plus
the generator's rejection/idempotence tests. For diagnosis, a separate host build with
`-DRODAKOS_CODEC_VOLUME_USE_UPSTREAM=ON` deliberately uses the original source and reproduces the
regression; this option exists only in the host test project, not the firmware build.
The application service/adapter fixture remains in `tests/app_model/`.

## Updating or removing the overlay

When the generator refuses a build, inspect its diagnostic and compare the project manifest,
lock, resolved component metadata and source with the reviewed provenance. Restore the pinned
dependency through the component manager if the change was accidental. Do not disable the hook,
edit checksum files or copy an old generated output to bypass the failure.

For an intentional update, review the upstream setter and callers, update the manifest/lock and
the project-owned patch provenance together, then run both codec/service host suites, sanitizers
and a firmware build. Confirm the component's compile command still uses the generated source.
Remove the overlay only when an upstream release demonstrably preserves the tested semantics;
keep the runtime regressions while switching their default source to that reviewed release.

Driver success remains software evidence. It does not establish I2C register state, speaker output,
or a correlated MQTT effect receipt. Hardware failure/retry and acoustic verification remain open
in the [roadmap](roadmap.md).

# MQTT volume service host target

This target compiles production `UnifiedMqttService`, `MqttVolumeEffect`, the atomic output service
and codec adapter, plus `MqttLightEffect`, real `LightService` and `board_device_adapter`.
It executes the actual registered ESP-MQTT callback, fragment assembly,
eight-item queue, worker, desired parser and receipt publishing path. Other device services and
ESP-MQTT/network/codec facilities are faked; the service and business result are not copied.

The fake MQTT SDK invokes callbacks under its recursive API lock and posts custom events
asynchronously. This reproduces the lock-order boundary used by the production SDK. Fixtures can
hold a dequeued message, a codec operation or an SDK user event to exercise cancellation.
It distinguishes outbox enqueue from wire publication and models separate custom/native event
queues. The checked [SDK overlay](../../docs/dependency-maintenance.md#mqtt-custom-event-queue-overlay)
has its own source-level tests; this service target does not compile the network SDK.
The light fixture also holds real driver refresh or fails exact pixel/refresh calls. Light business
state is no longer faked; other device services remain fakes.

```powershell
wsl -d Debian -- bash -lc '
  cmake -S /mnt/d/workspace/rodakos/tests/mqtt_volume_service \
        -B ~/.cache/rodakos-mqtt-volume-service -G Ninja -DCMAKE_BUILD_TYPE=Debug &&
  cmake --build ~/.cache/rodakos-mqtt-volume-service &&
  ctest --test-dir ~/.cache/rodakos-mqtt-volume-service --output-on-failure
'
```

For ASan/UBSan, configure another build directory with:

```text
-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
```

Run CTest with `ASAN_OPTIONS=detect_leaks=1`. The service test timeout is 40 seconds. Cases cover
fragment delivery, ordinary reports, malformed metadata, duplicate/stale effects, Stop after
dequeue, same-client reconnect, fragments crossing disconnect, delayed receipt cancellation,
ordinary credential refresh, binding replacement, explicit unbind and codec commit serialization.

## Cross-repository CLI

The same CMake build produces `rodakos_mqtt_volume_fixture`:

```text
rodakos_mqtt_volume_fixture --device-key=device-1 --codec=closed
rodakos_mqtt_volume_fixture --device-key=device-1 --codec=open
rodakos_mqtt_volume_fixture --device-key=device-1 --codec=open --fail-write
```

Send one complete production desired envelope per stdin line. It is delivered as fragmented
MQTT input to the registered callback. Stdout contains only complete production
`rodakos.mqtt-volume-result.v1` envelopes destined for `effects/receipt`; legacy ordinary desired
messages produce no stdout receipt. Each processed input produces a stderr completion marker:

```json
{"fixture":"mqtt-volume","processed":1,"volume":30,"codecWrites":0}
```

The CLI uses a harmless ping marker through the real worker queue to wait for preceding input,
then drains the actual SDK user-event callback before printing captured publications. It does
not manufacture business receipts. `--fail-write` takes effect after a successful host codec
open, so the initial open's volume write remains visible in `codecWrites`. EOF stops the service
and joins its workers. Build/source/binary identity should be recorded by the cross-repository
runner; host results do not establish device firmware or acoustic acceptance.

## Light fixture

The same build produces `rodakos_mqtt_light_fixture`, with the same stdin/receipt-only stdout
contract. It exposes actual Board Manager fixture IDs `board_rgb` (LEDs 0–2) and `accent` (LED 3).
Flags include `--device-key=...`, `--fail-light-refresh`, `--fail-light-pixel=2` (absolute pixel-call
number), and `--light=missing`. The original volume CLI/flags remain available.

Stderr markers use `fixture: mqtt-light`, `processed`, `volume`, `codecWrites`, `lightWrites`
(refresh attempts), `lightPixelWrites`, and the real first light's accepted configuration,
revision and availability. `shadowReport` contains the exact latest production shadow/report
body as a JSON string, including the initial connection report; callers must not construct a
report from diagnostic counters. No business result or report is manufactured by the fixture.

The service target currently runs 31 tests: the original 14 volume cases and 17 light cases.
Light cases include 70 continuous updates, old-frame and cross-light replay after eviction,
partial patches, driver failure retention, metadata routing, reports, Stop, reconnect, fragmented
epochs, ordinary token refresh and replacement bindings. Native driver tests also live in
[`tests/light_service`](../light_service/README.md). See the [wire contract](../../docs/mqtt-light-effects.md).

## Independent command fixture and tests

The same library also provides `rodakos_mqtt_command_fixture` and the separate CTest target
`rodakos_mqtt_command_service` (55 cases). These exercise the production command handler, not
the volume/light effect protocol. Build targets are `rodakos_mqtt_command_fixture` and
`rodakos_mqtt_command_service_tests`.

```text
rodakos_mqtt_command_fixture --device-key=device-1
```

Each stdin line is a transport wrapper whose `payload` is the original command text:

```json
{"topic":"devices/device-1/commands/command-1","payload":"ping"}
```

Stdout wraps only the actual captured production ACK publication as
`{processed,topic,payload}`. The ACK body remains a string; the fixture never constructs a success
or failure result. All inputs of at least two bytes pass through two MQTT fragments; shorter
text uses one complete frame. Initial reports and the internal ping barrier are omitted. Camera
and display services are not injected, so their commands return production `_stream_unavailable`
errors. The fixture accepts its own command topics and reserves `host-barrier` for synchronization.

The independent command cases cover four ping shapes, malformed/unsupported requests, all six
camera/display unavailable paths, replaying cached command results and rejecting raw-payload conflicts, and rejecting an old
queued command after a same-client reconnect. Additional cases inject successful fake streams,
hold result notifications, reconnect or replace credentials, replay saved callbacks, reject event
posting and synchronously disconnect during direct publication. A negative control shows that
stored QoS 0 outbox entries can cross a reconnect; command output must use direct publication.
Rodak's independent runner is
`scripts/run-rodakos-command-conformance.mjs`, with 21 real Broker/handler/ACK tests. It records
separate source and binary evidence; command counts are not part of the existing 31 effect tests.

Ordinary commands use the production latest-64 authority cache. The worker checks generation/epoch
before entering the handler. ACK/sideband output is bound to the original generation, epoch and
topic through a bounded queue, SDK event-loop validation and direct QoS 0 publish. One shared wake
notification allows synchronous stream callbacks and their final ACK to use the single custom
event slot. Epoch changes clear result payloads; held or late callbacks cannot republish on a new
connection. An already admitted direct send may finish on its original connection.

Lifecycle cases verify revocation before/during/after Start, Stop joining a callback on another
thread, terminal state feedback, same-name session replacement, camera/display exclusion and
control lease ownership. The ledger uses the existing production SHA-256 wrapper linked to real
PSA/libmbedcrypto; known-vector and embedded-NUL cases verify the hashing boundary. Other cases
cover immutable outcomes, authority reset tickets, in-flight protection, latest-64 eviction,
reply-budget tombstones, and repeated Start/Stop/signal executing once while retained.

Stream hardware remains fake. Real LVGL input/controller tests live in
[`tests/remote_input`](../remote_input/README.md); the complete production WebRTC display sender
and actual encoded ACK tests live in [`tests/display_control_ack_service`](../display_control_ack_service/README.md).
These tests do not establish persistent or unbounded exactly-once execution, wireless WebRTC
success, physical rollback or hardware acceptance.

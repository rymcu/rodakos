# MQTT volume service host target

This target compiles production `UnifiedMqttService`, `MqttVolumeEffect`, the atomic output service
and codec adapter. It executes the actual registered ESP-MQTT callback, fragment assembly,
eight-item queue, worker, desired parser and receipt publishing path. Other device services and
ESP-MQTT/network/codec facilities are faked; the service and business result are not copied.

The fake MQTT SDK invokes callbacks under its recursive API lock and posts custom events
asynchronously. This reproduces the lock-order boundary used by the production SDK. Fixtures can
hold a dequeued message, a codec operation or an SDK user event to exercise cancellation.

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

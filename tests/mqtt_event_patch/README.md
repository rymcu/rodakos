# MQTT custom-event isolation tests

The source-checked ESP-MQTT 1.0.0 overlay puts custom wakeups in a separate
FreeRTOS queue. The SDK task transfers one wakeup to its native event loop and
runs it immediately while holding the SDK API lock. A custom producer therefore
cannot occupy the native lifecycle event slot. The native queue may remain at
its default capacity of one. Nested native events from a publish failure still
run synchronously.

The independent queue also has `MQTT_EVENT_QUEUE_SIZE` slots (one by default).
Submission never waits for the SDK API lock or a full queue. Transient transfer
failure requeues the coalesced wakeup; if another producer filled the slot,
that pending wakeup drives the same bounded service queues. Each task iteration
attempts only one transfer, with the existing poll timeout capped at 10 ms while
the queue is nonempty. Start and task exit clear pending wakeups; destruction
releases the queue. Native event allocation failure remains the upstream SDK's
resource-failure boundary; this focused overlay removes competition from custom
events, without changing native dispatch or transport behavior.

```sh
cmake -S tests/mqtt_event_patch -B /tmp/rodakos-mqtt-event-patch -G Ninja \
  -DRODAKOS_IDF_PATH=/path/to/esp-idf-6.0.2 -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/rodakos-mqtt-event-patch
ctest --test-dir /tmp/rodakos-mqtt-event-patch --output-on-failure
RODAKOS_IDF_PATH=/path/to/esp-idf-6.0.2 \
  python3 -m unittest discover -s tests/mqtt_event_patch -p 'test_*.py'
```

The C++ target compiles the exact native dispatch, custom dispatch, pump, polling
and queue-reset functions extracted from the generated SDK source, together with
FreeRTOS/event-loop fakes. It also compiles the original custom-dispatch function
as a negative control: queued user events make the original one-slot native loop
drop DISCONNECTED and CONNECTED, while the independent queue preserves both.
Additional scenarios cover bounds, invalid inputs, nested disconnect delivery,
bounded transfer retries, a concurrent replacement wakeup, and cleanup.

This is not a build of the full MQTT SDK or real ESP-IDF event loop. The generator
separately checks the resolved MQTT source/header/metadata and actual ESP-IDF
`esp_event.c` hashes, refuses drift, and leaves managed sources unchanged. Python
tests verify those checks and the start/stop/destruction hooks. The firmware build
and device transport validation remain separate evidence.

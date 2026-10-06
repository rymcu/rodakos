# Assistant / Device Cloud LVGL host regression

This target runs production `AssistantApp`, the Settings lifecycle, the Device Cloud page,
shared UI components and the soft keyboard against real LVGL at 320 × 240. Cloud service,
WiFi, wake and assistant runtime are controlled host dependencies; Device Cloud uses its
production class/DTO declarations. Unrelated Settings pages are link stubs. The Settings test
source alone uses `-fno-access-control` to select its page and inject a generation boundary;
production declarations and access control are unchanged.

Fifteen cases cover typed, fixed diagnoses; explicit Settings navigation; cancellation of queued
navigation at destruction; successful cloud recovery after a voice failure; retry and unbind
hit regions; failed-refresh recovery without raw backend text; late refresh completion after
Settings destruction; stale-generation rejection; worker allocation failure; and a lost completion
after the worker cannot acquire the UI lock or queue its LVGL completion. A host-only linker wrapper
rejects one `lv_async_call` without changing the production LVGL implementation. Idle/error titles
must agree with the selected diagnosis while active phases remain visible. Task-start and delivery
failures retain their own fixed UI text across navigation, timer ticks and theme rebuilding, keep
retry available and preserve the cloud diagnosis/revision and binding. Disabled wake replaces only
otherwise-ready idle guidance; cloud/voice errors, disconnected WiFi and active phases retain
priority, and re-enabling wake restores the ready text. Task-start and delivery
failures clear when a new manual retry is admitted; stale-generation delivery failures cannot
replace current state. The retry case uses a real LVGL pointer click after scrolling the control
into view. Cloud/voice ordering also covers events in the same
millisecond and counter rollover. The cloud service target verifies actual rejection, refresh and
expiry revision transitions; this UI target consumes that shared DTO with controlled service fakes.
Host fonts do not prove Chinese glyph availability
or readability on the device.

```sh
cmake -S tests/assistant_ui -B ~/.cache/rodakos-assistant-ui -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build ~/.cache/rodakos-assistant-ui -j2
ctest --test-dir ~/.cache/rodakos-assistant-ui --output-on-failure
```

For sanitizers, configure a separate directory with
`-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"` and
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`, then run CTest with
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`.

`tools/run_release_host_checks.sh` includes this target. Actual NVS/network preparation is
covered by `tests/server_trust`; voice task/queue/Stop behavior by `tests/voice_volume_service`.
These host runs do not open a real network connection, mutate device bindings, flash firmware,
or prove physical screen, touch, DNS/HTTP cancellation latency or audio behavior.

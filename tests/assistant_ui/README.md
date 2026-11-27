# Assistant / Device Cloud LVGL host regression

This target runs production `AssistantApp`, the Settings lifecycle, the Device Cloud page,
shared UI components and the soft keyboard against real LVGL at 320 × 240 with the production
fonts (Montserrat → Ionicons → PuHui). Cloud service, WiFi, wake, the voice status light and
the assistant runtime are controlled host dependencies; Device Cloud uses its production
class/DTO declarations. Unrelated Settings pages are link stubs. Both test sources use
`-fno-access-control` to reach page state; production declarations and access control are
unchanged.

The 12 Assistant cases cover the ready card with the confirmed wake word from `voice_identity`;
every session phase and turn progress step (已收到, 正在思考, 正在处理, 正在组织回答, a server
label, 正在回复/正在播报); an active turn outranking a stale cloud failure; every cloud diagnosis in
the shared Chinese text, with no raw service text and the fix button only where there is
something to fix; the fix routing (no Wi-Fi → Settings Wi-Fi page, server/credential/trust/voice
→ 连接服务, `LaunchFrom("settings", "assistant", page)`); the 连接服务 row; cancelling a queued
navigation at destruction; the wake row with off versus music-paused text, while a cloud problem
still outranks it; a missing wake runtime; the status light row saving through its service; the
info page and back; and cloud/voice revision ordering, including counter rollover.

The 9 Settings cases cover retry and unbind hit regions, failed-refresh recovery without raw
backend text, late refresh completion after Settings destruction, stale-generation rejection,
worker allocation failure, a lost completion after the worker cannot take the UI lock or queue
its LVGL completion, and a page requested by another app: Settings opens on that page, back
returns to the caller, a normal launch still returns to the Settings list, and destruction
cancels the deferred return. A host-only linker wrapper rejects one `lv_async_call` without
changing the production LVGL implementation. The suite writes `assistant-*.ppm` screenshots.

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

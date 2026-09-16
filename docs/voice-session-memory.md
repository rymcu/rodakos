# Voice session SRAM budget

## Observed failure

On 2026-09-16, two USB-injected speech sessions completed AFE, Opus uplink and
TTS playback. The third failed to create the WebSocket task: internal free heap
was 6,111 bytes, but the largest block was only 5,632 bytes, below its 6,144-byte
stack requirement. The lowest internal free heap seen across the run was 575
bytes. An idle measurement of roughly 18 KiB was therefore insufficient evidence
of a safe session budget. See `build/logs/voice-usb-test.log` and
`build/logs/voice-usb-padded.log`.

## Official guidance and implementation

Sources retrieved online on 2026-09-16:

- [ESP-IDF 6.0.2 RAM usage](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/api-guides/performance/ram-usage.html):
  monitor largest free block as well as total heap because fragmentation can
  prevent allocation even when total free memory looks adequate.
- [ESP-IDF external RAM](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/api-guides/external-ram.html):
  ordinary task creation still uses internal memory; PSRAM stacks require explicit
  support and cache-disabled execution must be considered. Ordinary malloc uses
  the configured internal/external size threshold, while explicitly internal/DMA
  allocations can consume the reserved internal pool.
- [ESP-IDF WiFi Kconfig](https://github.com/espressif/esp-idf/blob/v6.0.2/components/esp_wifi/Kconfig):
  each static TX buffer costs approximately 1.6 KiB; its count is configurable.
  Keep static TX with PSRAM. The recommended RX BA window for PSRAM operation is
  16, and static RX buffers should be at least that window size.
- [WiFi performance and buffer usage](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/api-guides/wifi-driver/wifi-performance-and-power-save.html):
  network buffering and application memory must be budgeted together; reducing
  buffering trades burst throughput for memory.
- [ESP WebSocket client API](https://github.com/espressif/esp-protocols/blob/master/components/esp_websocket_client/include/esp_websocket_client.h):
  configurable stack size does not imply configurable allocation capabilities.
  The project's pinned component creates its task with `xTaskCreatePinnedToCore`.

The selected changes keep the 6 KiB WebSocket stack and its library unchanged:

1. Reduce `ESP_WIFI_STATIC_TX_BUFFER_NUM` from 16 to 8, freeing approximately
   12.8 KiB of constant internal buffer demand. Preserve RX=16, RX BA=16 and
   cache TX=32. This is a peak-transmit-throughput tradeoff, not a protocol change.
2. Move the separate 4 KiB `voice_ws_gc` cleanup stack to PSRAM using matching
   task creation/deletion APIs. Its audited path unregisters/stops/destroys the
   WebSocket and releases network resources; it does not load or write NVS.
3. Log heap/largest-block state before WebSocket task creation and on failures,
   plus cleanup stack high-water marks.

AFE already uses `AFE_MEMORY_ALLOC_MORE_PSRAM`, consistent with xiaozhi. The
WebSocket data buffers already preferentially use PSRAM under the 512-byte malloc
threshold. Shrinking these buffers would not directly recover its internal stack
budget. Provisioning and wake notification stacks stay internal because their
paths access NVS. No arbitrary task stack reduction is part of this change.

## Validation boundary

Build with ESP-IDF 6.0.2, reuse the installed immutable Recovery package, and use
the non-erasing COM3 refresh. Run at least six consecutive USB-injected speech
sessions after the change, measuring memory both during sessions and after
cleanup. Verify WebSocket creation, AFE capture, TTS playback, re-arming, ongoing
MQTT telemetry and absence of allocation errors, resets or watchdogs. Require
actual server session evidence, not just diagnostic command acknowledgements.

This gate does not measure maximum WiFi throughput, SD OTA under simultaneous
voice load, acoustic echo attenuation or device VAD. Device VAD remains disabled.

## COM3 results, 2026-09-16

Package `20260916-141626` built successfully and passed non-erasing refresh,
Recovery handoff, local image confirmation and Home startup. The host app-model
regression suite passed. Final six-cycle evidence is in
`build/logs/session-memory-six-cycle.log` and its `.summary.json` sidecar.

| Measurement | Before | After |
| --- | ---: | ---: |
| Internal free heap at wake-monitor startup | 26,627 B | 40,415 B |
| Lowest internal free heap in conversation tests | 575 B | 10,911 B |
| Largest free block after repeated sessions | 5,632 B | 14,336 B |
| Consecutive sessions established | 2, third failed | 6 of 6 |

Internal free heap immediately after session startup was 14,519 / 11,215 /
13,139 / 13,139 / 13,139 / 13,139 bytes. Every cleanup restored a 14,336-byte
largest block. Cleanup stack minimum headroom was 2,644–2,900 bytes. The serial
gate recorded six starts, six stops, six re-arms, 91 PC-status messages and no
allocation failures, transport failures, resets, panics or watchdogs.

Rodak independently observed six physical-device sessions between 14:18:07 and
14:22:30 CST, each with STT, TTS start/stop and connection closure, plus subsequent
MQTT telemetry at 14:22:33. Five sessions had actual response sentences. The third
had only four placeholder audio packets and 174 ASR interruption probes, without
a response sentence. All six transcriptions were incomplete. Therefore this is
a successful memory/lifecycle regression gate, not six successful question-answer
or acoustic AEC tests. Repeated probing and speech truncation remain separate
issues to investigate.

Rodak subsequently fixed these two server issues in `7ad4de4a`. The same firmware
and fixture passed three further replays with complete source-audio coverage,
one effective recognition/reply per session and no recursive interruption probes.
See `build/logs/endpoint-probe-fixed.log`. Proper-name ASR accuracy and acoustic
AEC/device VAD remain outside this memory gate.

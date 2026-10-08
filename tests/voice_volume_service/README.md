# Voice volume service host tests

This target compiles the production `VoiceAssistantService`, its real inbound queue / I/O loop,
MCP dispatcher, reconnect coordinator and output/codec adapter. It drives `StartInteraction`,
`StopInteraction` and the transport's installed inbound callback. It does not copy lifecycle
guards or add a test-only production friend.

## 034 phase snapshot and terminal capture failure

The added cases exercise the real service's Idle (before Init and after Deinit), Connecting,
Listening, Speaking and Error phases. A thread-local host `operator new` linker wrapper observes
zero allocation requests from `GetPhaseSnapshot`, including a 1024-byte diagnostic message;
the complete `GetState` remains a positive allocation control using the real transport/recorder
name lengths. These are host allocation requests, not ESP32 internal-heap measurements.

The actual I/O loop distinguishes an empty queue with a running recorder from terminal capture
failure in Listening/Speaking, releases focus and transport, permits restart and preserves a
concurrent normal Stop. AFE warmup and resynchronization must keep `IsRunning()` true. The terminal
failure uses a fixed message and never reads a mutable recorder error pointer.

Only `delayed_recording_failure.cc` uses `-fno-access-control`, to deliver an old captured failure
to the real private cleanup endpoint after public Start/reconnect/restart operations. It never
reads or writes private state, and the production TU retains normal access checks. This tests the
final interaction/transport guard; it does not claim public I/O and reconnect run in parallel.

`run_capture_controls.py --idf-path <idf> --output <new-directory>` requires precise assertion
failures from five complete-TU mutations: allocating phase getter, missing terminal handling,
missing transport guard, wake copying a full snapshot, and missing wake-generation recheck.
Compiler failures, timeouts and unrelated crashes are not detection. The explicit historical
self-delete control uses `RODAK_ASSISTANT_LEGACY_BASELINE=ON` to exclude tests for APIs absent
from that unchanged old header; current tests and mutation controls keep it OFF.

The task runtime links `tests/task_retirement`: the complete pinned ESP-IDF 6.0.2 WithCaps
creation/deletion chain and production retirement registry, with host scheduler/allocator/core fakes.
The old no-op self-delete headers have been removed. Other host fakes supply mutexes, a silent recorder,
transport callbacks and response capture, audio focus, Opus and board/codec APIs. Tests can park
the worker at a fake scheduler delay to place a request in the real queue before Stop. They also
inject initialize synchronously from channel opening to exercise the actual startup window.
Normal MCP inputs are wrapped in canonical `{event, sessionId, payload}` messages, decoded by
production `ParseRealtimeVoiceMcpInbound` with a real session gate, then delivered through the
installed service callback. Responses pass through production `BuildRealtimeVoiceMcpMessage`.
The roundtrip assertions preserve numeric/string RPC IDs, effect metadata and receipts; separate
delayed-callback cases still exercise the service guard after transport extraction.

```powershell
wsl -d Debian -- bash -lc '
  cmake -S /mnt/d/workspace/rodakos/tests/voice_volume_service \
        -B ~/.cache/rodakos-voice-volume-service -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DRODAKOS_IDF_PATH=/mnt/c/esp/v6.0.2/esp-idf &&
  cmake --build ~/.cache/rodakos-voice-volume-service &&
  ctest --test-dir ~/.cache/rodakos-voice-volume-service --output-on-failure
'
```

For ASan/UBSan, use a separate build directory and add these configure arguments:

```text
-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
```

Run its test executable with `ASAN_OPTIONS=detect_leaks=1`. CTest has a 30-second timeout so a
lifecycle deadlock fails the run. Cases cover startup initialization, handshake rejection,
repeat calls, stale generations, Stop cancellation, startup cancellation, reconnect scope and
early initialization, bounded queue pressure, canonical envelope rejection and response roundtrips.
Cloud diagnosis cases additionally verify fixed credential/network/capability messages, no
WebSocket on failed preparation, recorder cleanup, successful later wake, and Stop during a
preparation without publishing its late failure into the next interaction.

Passing this target establishes software lifecycle behavior with controlled host dependencies.
It does not validate real FreeRTOS scheduling, WebSocket close latency, I2C, ADC/DAC, or audible
speaker output. Wire details are in [the volume contract](../../docs/voice-volume-mcp.md).

## 031 assistant I/O retirement

24 cases retain the original 12 service/envelope regressions and add 12 retirement cases:
no IDF exit cleanup-task allocation; task scheduling before publication; Stop after the native
handle clears but before the complete body returns; failed replacement preserving the prior
ticket; autonomous session-end reaping; concurrent Stops/Pump; Deinit then Init/Start;
old cleanup and Deinit waiters not following a later generation; destructor admission closure;
an outdated interaction Stop leaving the new session running; and a stopping-branch waiter joining
only its old ticket after a replacement is active.

The semaphore-release hook delays the real worker after native state becomes idle. It does not
set private service fields or emulate retirement. Resource assertions occur before fixture sweep.
Host stack watermarks and heap samples are fixed placeholders, not device measurements.
Same-task recursive `Deinit` through recorder/transport/focus callbacks remains unsupported;
generation-bound external waiters do not establish arbitrary service re-entrancy.

`run_retirement_controls.py` compiles six complete-TU mutations and, with `--baseline-root`,
one unchanged old `.cc/.h` pair. It requires identified assertions or the real IDF cleanup-create
failure markers plus SIGABRT. Build failure, timeout or unrelated sanitizer errors are failures.
No C++ exception is used to simulate task deletion or unwind the old body.

```sh
python3 tests/voice_volume_service/run_retirement_controls.py \
  --idf-path /mnt/c/esp/v6.0.2/esp-idf \
  --output /path/to/new-controls-directory \
  --baseline-root /path/to/frozen-old-source
```

The optional baseline directory must contain `manifest.json` with its fixed `sourceCommit` and
`files` entries (`bytes`/`sha256`) for `phone_os/voice_assistant_service.cc` and `.h`.
Debug, independent ASan/UBSan/leak and the explicit negative subprocesses are recorded separately;
this target does not exercise real WebSocket timing, DMA/audio, hardware OOM or production release.

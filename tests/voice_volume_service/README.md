# Voice volume service host tests

This target compiles the production `VoiceAssistantService`, its real inbound queue / I/O loop,
MCP dispatcher, reconnect coordinator and output/codec adapter. It drives `StartInteraction`,
`StopInteraction` and the transport's installed inbound callback. It does not copy lifecycle
guards or add a test-only production friend.

Host fakes supply FreeRTOS mutex/task APIs using C++ mutexes and threads, a silent recorder,
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
        -B ~/.cache/rodakos-voice-volume-service -G Ninja -DCMAKE_BUILD_TYPE=Debug &&
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

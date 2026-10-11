# BigSmart WebRTC peer integration

RodakOS uses the Espressif `esp_peer` component as the PeerConnection for BigSmart video streams.
This document gives the component source, the local changes, the required configuration, and the
known defects. Release evidence for each firmware package is in
[OTA release readiness](ota-release-readiness.md).

## Component source

| Item | Value |
|---|---|
| Repository | https://github.com/espressif/esp-webrtc-solution |
| Path | `components/esp_peer` |
| Version | `1.5.6` |
| Target | ESP32-S3 |
| Prebuilt library | `libs/esp32s3/libpeer_default.a` |
| Library SHA-256 | `6b2f3856b9a1639b0480c011688c070c132b6399e9863a74d20d20a271dc7da3` |
| License | Espressif Modified MIT, in `components/esp_peer/LICENSE` |

The license permits use only with Espressif products. BigSmart uses an ESP32-S3, so it meets this
condition. The `esp_libsrtp` dependency uses its own component license.

The repository keeps the upstream `include/`, `src/`, `LICENSE`, and the prebuilt `libs/` for all
chips. The build selects the library from `IDF_TARGET`.

## Local changes

The files in `components/esp_peer/src/` are not identical to upstream. Keep these changes when you
upgrade the component:

1. The build selects `src/dtls_srtp_v6.c` for ESP-IDF 6.0 and later, and `src/dtls_srtp.c` for
   earlier versions. `dtls_srtp_v6.c` declares the Mbed TLS 4 private identifiers that it uses.
2. The PSA ECDSA algorithm matches the certificate that the TLS stack selects.
3. `src/media_lib_weak.c` allocates media memory from PSRAM.
4. `src/dtls_srtp_v6.c` frees the CTR_DRBG context before the self-signed certificate path seeds
   it again. See [DTLS CTR_DRBG mutex leak](#dtls-ctr_drbg-mutex-leak).

After an upgrade, compare the prebuilt library version with the source version. The source and
the library must come from the same release.

## Required configuration

ESP-IDF 6.0.2 compiles the `esp_peer` DTLS-SRTP code only with these options:

```ini
CONFIG_MBEDTLS_SSL_PROTO_DTLS=y
CONFIG_MBEDTLS_SSL_DTLS_SRTP=y
CONFIG_MBEDTLS_X509_CREATE_C=y
```

The firmware also sets `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`. This option puts DTLS records,
handshake state, and key material in PSRAM. Without it, the largest free internal block is too
small for the DTLS input buffer when voice wake and the DVP camera DMA run at the same time.
`esp_peer_open` then reports the DTLS failure as `ESP_PEER_ERR_NO_MEM (-2)`.

The `esp_peer` media allocations, the camera task, and the JPEG task also use PSRAM.

## Peer configuration

`main/phone_os/webrtc-peer-config.h` gives the shared settings for the Camera peer and the Display
peer:

| Setting | Value |
|---|---|
| `max_candidates` | 32, set through `extra_cfg` |
| Agent receive timeout | 50 ms |
| Data channel send cache | 400 KiB |
| Data channel receive cache | 400 KiB |
| Data channels | reliable, ordered |
| IPv6 | off |
| TCP candidates | off |

A zero `max_candidates` value means 10 in the ESP32-S3 library. The header says 16. This result
comes from the disassembly and DWARF data of the fixed library. The repository does not change the
library binary.

The SDK configuration structure is 68 bytes. `max_candidates` is at offset 55. The change from 10
to 32 candidates adds 5,984 bytes per peer to the candidate tables, the pair table, and the sort
table. The pair limit stays at 64. The sort table adds 176 bytes, and it prefers internal RAM. The
larger tables prefer PSRAM, but they can fall back to internal RAM.

## Implemented features

- The RodakOS signaling client and the `esp_peer` main loop.
- JPEG frames in chunks on a WebRTC data channel.
- The `camera.stream.start` and `camera.stream.stop` commands.
- The Rodak SDP and ICE session, and JPEG reassembly for playback.
- The screen stream: 320 × 240 LVGL partial-flush regions, a dedicated JPEG worker, and only the
  latest frame in the queue.
- Two Display data channels: `screen-jpeg` (read-only by default) and `screen-control` (needs
  explicit permission).
- Remote input: local touch has priority, the release position stays, and UTF-8 text goes to the
  visible focused text area.
- Home and Back, text confirmation, keyboard close, and release of remote control on stop or
  disconnect.

## Signaling encoding

The Mbed TLS Base64 output buffer must hold `4 * ceil(n / 3) + 1` bytes. The last byte holds the
terminator. If the buffer holds only the encoded length, every non-empty SDP or ICE message returns
`BUFFER_TOO_SMALL`.

MQTT decoding does not add a terminator to the SDP. Add one before you send the SDP to
`esp_peer_send_msg`. Keep `message.size` equal to the text length, without the terminator.

## Camera session deadline

`WebRtcCameraService::Config::connect_timeout_ms` sets a connection deadline. The default value is
20,000 ms. `Start` rejects the value 0.

1. `Start` sets the deadline before it calls `esp_peer_new_connection`.
2. The deadline stops when the `camera-jpeg` channel opens.
3. If the deadline expires first, the peer task stops the session with `CONNECT_FAILED`. It uses the
   same stop path as an SDK failure. The peer closes, the JPEG stream and the remote preview stop,
   and MQTT reports `failed` to Rodak.

The deadline is necessary because `esp_peer` does not report every handshake failure. In one device
test, the DTLS server handshake returned `MBEDTLS_ERR_SSL_ALLOC_FAILED` and then retried with
`MBEDTLS_ERR_SSL_UNEXPECTED_MESSAGE`. The `esp_peer` main loop continued, and no terminal state
came. Without the deadline, the session held the peer and the camera with no limit.

The deadline is longer than the 15-second first-frame timeout in the Rodak renderer. Thus the
device does not stop a session that the user interface still waits for.

## Known defects

### DTLS CTR_DRBG mutex leak

**Status:** fixed locally in `src/dtls_srtp_v6.c`. Not reported upstream yet.

**Symptom:** after the first remote Camera session, the largest free internal block decreases from
8,192 bytes to 6,144–6,656 bytes. It does not recover after the session stops. Later sessions do not
decrease it more.

**Cause:** `dtls_srtp_try_gen_cert` seeds `dtls_srtp->ctr_drbg` with `mbedtls_ctr_drbg_seed`. When
no cached certificate exists, it then calls `dtls_srtp_selfsign_cert`. That function seeds the same
context again. `mbedtls_ctr_drbg_seed` always initializes the context mutex. The second call
replaces the first mutex and does not free it.

With `MBEDTLS_THREADING_C`, each mutex is a FreeRTOS queue of 84 bytes in internal RAM. Only the
first connection makes a certificate. Later connections use the cached certificate and seed the
context one time. Thus the leak occurs one time per boot.

The Mbed TLS contract prohibits this sequence. The `ctx` documentation of `mbedtls_ctr_drbg_seed`
says: "After a successful call to mbedtls_ctr_drbg_seed(), you may not call mbedtls_ctr_drbg_seed()
again on the same context unless you call mbedtls_ctr_drbg_free() and mbedtls_ctr_drbg_init() again
first."

**Fix:** call `mbedtls_ctr_drbg_free` and then `mbedtls_ctr_drbg_init` before
`dtls_srtp_selfsign_cert`.

**Evidence:** a heap trace with 12-frame call stacks found the 84-byte block in the old 8 KiB free
span. The allocation chain was `xQueueCreateMutex` ← `pthread_mutex_init` ← `mbedtls_mutex_init` ←
`mbedtls_ctr_drbg_seed` ← `dtls_srtp_try_gen_cert` ← `peer_open` ← `esp_peer_open` ←
`WebRtcCameraService::Start`. In the heap map, this block split the 8,296-byte span into 6,232 and
1,972 bytes.

**Upstream:** the same code is in the `main` branch of `espressif/esp-webrtc-solution`, file
`components/esp_peer/src/dtls_srtp_v6.c`. Report it in that repository. Do not report it to Mbed TLS
or ESP-IDF. Their behavior matches their documentation.

The leak fix alone does not restore 8,192 bytes. See
[GPIO ISR table in the largest free block](#gpio-isr-table-in-the-largest-free-block) and
[Interrupt vector descriptor in the largest free block](#interrupt-vector-descriptor-in-the-largest-free-block).

### GPIO ISR table in the largest free block

**Status:** fixed locally in `main/main.cc`. This is not an `esp_peer` defect.

**Symptom:** with the DTLS fix, the largest free internal block is 6,144 bytes after the first Camera
session. A used block of 400 bytes is at the end of the 6,144-byte free span.

**Cause:** at boot, no driver installs the GPIO ISR service. The buttons use polling, and the touch
panel interrupt pin is not connected. The DVP camera driver calls `gpio_install_isr_service` in
`esp_cam_new_dvp_ctlr_ext` on the first `VIDIOC_STREAMON`. ESP-IDF then allocates the handler table
(`GPIO_NUM_MAX` entries of 8 bytes, 392 bytes) from internal RAM. ESP-IDF does not free the table.
The table goes into the largest free block that exists at that time.

**Fix:** `app_main` calls `gpio_install_isr_service(ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM)`
before `esp_board_manager_init`. The DVP driver uses the same flags. Its own call returns
`ESP_ERR_INVALID_STATE`, and the driver ignores that result. The button driver uses the GPIO ISR
service only in power-save mode. The BigSmart board does not enable power-save mode.

**Evidence:** a release-test allocation probe recorded each internal allocation of 388 to 400 bytes.
Only one record was in the old 8 KiB span: 392 bytes from `gpio_install_isr_service` ←
`esp_cam_new_dvp_ctlr_ext` ← `dvp_video_start` ← `esp_video_ioctl_streamon`. Full heap tracing did
not find it, because the tracing wrappers changed the heap layout.

### Interrupt vector descriptor in the largest free block

**Status:** fixed locally in `main/main.cc`. This is not an `esp_peer` defect.

**Symptom:** with the DTLS fix and the GPIO ISR fix, the largest free internal block is still
6,144 bytes after the first Camera session. A used block of 16 bytes is at the end of the
6,144-byte free span.

**Cause:** ESP-IDF `intr_alloc` creates a `vector_desc_t` (16 bytes) the first time it uses a CPU
interrupt line. `esp_intr_free` does not free the descriptor. On the first `VIDIOC_STREAMON`, the
DVP camera driver allocates the GDMA RX interrupt on the CPU of the caller. On that CPU, the selected
line had no descriptor yet. Thus the new descriptor went into the largest free block, and it stayed
there after the camera stopped.

**Fix:** before `esp_board_manager_init`, `app_main` runs a warm-up on each CPU through
`esp_ipc_call_blocking`. The warm-up allocates disabled low/medium-priority interrupts from sources
that the firmware does not use, until it uses all free lines. Then it frees all of them. The
descriptors stay in low memory, and later drivers use them again. The boot log gives the number of
lines for each CPU: 10 on CPU 0 and 15 on CPU 1.

The first attempt allocated one disabled shared interrupt per CPU. It did not work: the GDMA RX
interrupt on the ESP32-S3 is not shared, so it did not use the shared line.

**Evidence:** a JTAG read of the `vector_desc_head` list found the 16-byte block at the end of the
free span. It was the descriptor of interrupt 11 on CPU 1, with source `ETS_INTERNAL_UNUSED_INTR_SOURCE`
after the camera stopped. After the warm-up, all descriptors are in the 0x3fce9xxx region. The
8,296-byte span stays complete after each remote Camera session.

## Release-test diagnostics

These serial commands exist only in builds with `RODAKOS_RELEASE_TESTS=ON`. The normal firmware
does not contain them.

| Command | Result |
|---|---|
| `RODAK_RELEASE_TEST_V1 heap_map` | Lists each internal free span of 512 bytes or more, with the size of the used block on each side. |
| `RODAK_RELEASE_TEST_V1 heap_trace_start` | Starts a leak trace. Needs `CONFIG_HEAP_TRACING_STANDALONE=y`. |
| `RODAK_RELEASE_TEST_V1 heap_trace_dump` | Stops the trace and prints the internal allocations that are still alive. |
| `RODAK_RELEASE_TEST_V1 hold_dma <limit>` | Holds internal DMA-capable blocks until the largest free block is less than `limit` bytes. |
| `RODAK_RELEASE_TEST_V1 release_dma` | Frees all the blocks that `hold_dma` holds. |

The heap map and the trace buffer use PSRAM, so they do not change the internal heap that they
measure. Do not commit `CONFIG_HEAP_TRACING_STANDALONE=y` to `sdkconfig`.

## Test coverage

`tests/display_control_ack_service` compiles the production `webrtc_camera_service.cc` and
`webrtc_display_service.cc`. Fakes replace the SDK calls. The tests check configuration transfer,
cleanup after failure, candidate forwarding, the connection deadline, and task retirement. They do
not prove SDK capacity, DMA recovery, or physical results.

`tests/remote_input` checks the remote input controller with real host LVGL.

## History

Each release record gives the package identity, the device evidence, and the open gates. The
earlier version of this document kept that evidence here. Git history keeps that version.

| Change | Record |
|---|---|
| 2026-10-01: first integration, screen stream, remote text input | Git history of this file |
| 2026-10-06: stream instance isolation and exact stop | [Exact stream Stop contract](rodak-aiot-contract-v1.md#exact-stream-stop-026) |
| 027: explicit candidate capacity and resource sampling | [OTA release readiness](ota-release-readiness.md) |
| 018: ACK retry, cancel, and device limits | [Media browsing](media-browsing.md#018-camera-分配保护ack-与阶段诊断) |
| 019: remote gesture cancel | [Media browsing](media-browsing.md#019-取消远程手势的修正与制品) |
| 084: Camera connection deadline | [OTA release readiness](ota-release-readiness.md) |
| 087: DTLS CTR_DRBG mutex leak | [OTA release readiness](ota-release-readiness.md) |
| 091: GPIO ISR table installed at boot | [OTA release readiness](ota-release-readiness.md) |
| 094: interrupt vector descriptors made at boot | [OTA release readiness](ota-release-readiness.md) |

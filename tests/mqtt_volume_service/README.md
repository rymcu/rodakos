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

The service target currently runs 36 tests: 19 volume/lifecycle cases and 17 light cases.
Light cases include 70 continuous updates, old-frame and cross-light replay after eviction,
partial patches, driver failure retention, metadata routing, reports, Stop, reconnect, fragmented
epochs, ordinary token refresh and replacement bindings. Native driver tests also live in
[`tests/light_service`](../light_service/README.md). See the [wire contract](../../docs/mqtt-light-effects.md).

## Independent command fixture and tests

The same library also provides `rodakos_mqtt_command_fixture` and the separate CTest target
`rodakos_mqtt_command_service` (60 cases). These exercise the production command handler, not
the volume/light effect protocol. Build targets are `rodakos_mqtt_command_fixture` and
`rodakos_mqtt_command_service_tests`.

020 adds real state-mutex contention around the captured display-control callback: both current and
revoked leases keep their original dispatch/rejection outcomes while recording wait/check timestamps.
The fixed single sample admits at most one slow event per five seconds. The existing worker copies
and clears it under that same mutex, then logs outside locks; a busy worker may delay output, so the
output spacing is not the sampling interval. It never records payloads or adds a callback
wrapper; the WebRTC timing separately covers dispatch duration. A reentrant-disconnect case verifies
that UI dispatch still runs outside the MQTT state lock. These host checks do not locate the device's
network or SDK latency.

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
separate source and binary evidence; command counts are not part of the existing 36 effect tests.

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

## MQTT drop diagnostics (024)

`rodakos_mqtt_queue_diagnostics` adds 11 cases against the complete production
`unified_mqtt_service.cc`. It fills the actual eight-slot host queue, rejects the ninth queued
message, checks FIFO execution and recovery after draining, and records the real `ESP_LOGE/W`
format arguments. A real dequeue between rejection and the depth sample verifies that
`queue_depth_sample` is observational: the send result determines `queue_send_rejected`.

The allocation case arms only scalar `operator new(size_t, const std::nothrow_t&)` on the
synchronous final-fragment thread for exactly `sizeof(PendingMessage)`. Throwing/string and
array allocation remain available. Fixed pointer watches observe actual deletion of the two
assembled strings and, where allocated, the queued object. Each watch is consumed once so
later address reuse cannot masquerade as release of the original owner. Tests also cover
successful admission, Stop cleanup, connection epochs, the eight-publication count limit,
the exact 128 KiB total payload budget, count-first precedence, and the separate 64 KiB
single-payload limit. Synthetic topic/body markers must not appear in failure diagnostics.

`rodakos_mqtt_diagnostic_negative_controls` compiles six isolated full-TU mutations: merged
inbound reasons, allocation mislabeled as queue rejection, classification from sampled depth,
false drop logging after successful admission, merged outbound reasons, and byte-first
classification when both budgets are full. Each must fail its intended assertion without a
sanitizer error. The generated source, build logs, result log and compiled source hashes are
retained under `negative-controls/`; `production-sources.json` identifies the compiled service.
`RODAK_MQTT_SERVICE_SOURCE` selects an isolated baseline/mutation only in this host target.

The host heap values check that diagnostics query `MALLOC_CAP_DEFAULT`; they do not measure
device free memory, fragmentation or allocation headroom. No production test switch, queue
capacity, publication limit, zero-timeout behavior or stream lease is changed by this fixture.

## MQTT 凭据刷新与 SDK 生命周期（025）

`rodakos_mqtt_credential_replacement` 通过完整生产服务执行 18 个案例。同一 authority
刷新凭据时，旧 SDK client 必须先退出、清空 outbox/custom events 并销毁，之后才创建新
client；effect/command 去重结果在同一 authority 内保留。此目标与已有 volume/light 36、
command 60、voice identity 8、diagnostics 11 个案例合计 133 个正常回归案例。

SDK 替身区分 `start()` 已接受、task 已进入 `run`、task 退出清理和 `stop()` 已确认退出，
保留每个实例的实际凭据、生命周期顺序与 wire/outbox 输出。真实 SDK 源码仍由
[`tests/mqtt_event_patch`](../mqtt_event_patch/README.md) 的独立目标验证，网络和设备硬件未模拟
成验收结果。替身的 `Load()` 返回 AIoT 配置完整性，允许旧 MQTT-only 缓存返回 false，
完整 Cloud 持久化与 MQTT 的联合覆盖见
[`tests/mqtt_cloud_integration`](../mqtt_cloud_integration/README.md)。

案例覆盖 HTTP 期间旧拒绝合并、新 client 在 `start()` 返回前被拒绝、旧回执/命令/可靠
PUBACK 等待者取消、片段与已出队命令的代次隔离、语音期间延后、init/register/start
失败后的本地重试、Stop 与 peer callback 并发、同代次 token 在 attach 前被替换，以及
身份残缺后的离线行为。SDK 的 `run=false` 可能处于启动或退出过程；`stop()` 失败时
生产代码保留 callback context 并以重启隔离。两个异常案例明确允许该重启，不能把它们
描述为正常免重启成功。测试结束时只在 SDK 实际退出后由 host 显式 join，避免把测试
资源清理当作生产退出证明。

`rodakos_mqtt_credential_negative_controls` 编译四个独立完整服务源码变体，分别模拟
吞掉新 client 拒绝、丢失同 authority 去重结果、未确认 stop 就调用 destroy，以及旧拒绝
跨代次继续触发 HTTP。每个变体必须在指定断言失败，构建失败、超时、崩溃或 sanitizer
错误均不算检测成功。源码、日志与来源哈希保存在 `replacement-negative-controls/`。
两个 negative-control 目标与五个正常目标合计 7 个 CTest；凭据正常目标限时 90 秒，
negative-control 目标各限时 480 秒。

完整旧版对照可同时设置 `RODAK_MQTT_SERVICE_SOURCE`、`RODAK_MQTT_POLICY_SOURCE`
和 `RODAK_MQTT_INCLUDE_ROOT`，使 service、policy 与 header 均来自同一 baseline。
这些选项只影响 host 编译；`production-sources.json` 记录实际选用的 service/header 哈希。

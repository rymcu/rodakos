# BigSmart WebRTC 组件集成记录

RodakOS 将 Espressif `esp_peer` 组件作为 BigSmart 视频流的底层 PeerConnection 实现。组件来源：

- 仓库：https://github.com/espressif/esp-webrtc-solution
- 路径：`components/esp_peer`
- 版本：`1.5.6`
- 目标：ESP32-S3
- 许可证：组件随源代码附带的 Espressif Modified MIT，仅允许配合 Espressif 产品使用；
  BigSmart 使用 ESP32-S3，符合该限制。`esp_libsrtp` 使用其组件许可证。
- 完整保留上游 `include/`、`src/`、各芯片的预编译 `libs/` 和 `LICENSE`；构建按
  `IDF_TARGET` 选择库，BigSmart 使用 `libs/esp32s3/libpeer_default.a`。

本地适配包括 IDF 6 / Mbed TLS 4 的 DTLS 源码选择及私有标识符声明、PSA ECDSA
算法与 TLS 证书选择保持一致，以及 `media_lib_weak.c` 使用 PSRAM 分配。升级组件时必须
保留这些改动及许可证，并同时检查预编译库与源码版本。

当前已接入以下代码路径：

- RodakOS 信令客户端和 `esp_peer` 主循环；
- WebRTC DataChannel JPEG 分片发送；
- `camera.stream.start/stop` 命令；
- Rodak 端 SDP/ICE 会话和 JPEG 重组播放。
- 320×240 LVGL partial flush 区域拼接、独立 JPEG worker 和仅保留最新帧的屏幕流；
- 默认只读的 `screen-jpeg` 与显式授权的 `screen-control` 双 DataChannel；
- 本地触摸优先、保持释放坐标，以及当前可见焦点 textarea 的 UTF-8 文本注入；
- Home / Back、文本确认与键盘收起、停止和断线时释放远程控制。

实机内存注意事项：BigSmart 同时运行语音唤醒和 DVP DMA 时，内部堆的最大连续块不足以容纳
mbedTLS 的 DTLS 输入缓冲。固件使用 `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`，把 DTLS
记录、握手状态和密钥材料放到 PSRAM；`esp_peer` 的媒体分配、摄像头任务和 JPEG 任务也使用
PSRAM。否则 `esp_peer_open`
会把 DTLS 初始化失败折叠为 `ESP_PEER_ERR_NO_MEM (-2)`。

ESP-IDF 6.0.2 需要以下配置才能编译 `esp_peer` 的 DTLS-SRTP 实现：

```ini
CONFIG_MBEDTLS_SSL_PROTO_DTLS=y
CONFIG_MBEDTLS_SSL_DTLS_SRTP=y
CONFIG_MBEDTLS_X509_CREATE_C=y
```

当前 `CameraService` 已提供 `CaptureJpeg` 和有界速率 `StartJpegStream`，由 `WebRtcCameraService` 发送器消费。

信令编码注意：Mbed TLS Base64 编码目标容量必须为 `4 * ceil(n / 3) + 1`，最后一字节供终止符使用；容量只分配编码长度会使所有非空 SDP/ICE 返回 `BUFFER_TOO_SMALL`。MQTT 解码后的 SDP 送入 `esp_peer_send_msg` 前也必须补终止符，`message.size` 保持文本长度。

## 2026-10-06 软件生命周期补充

流实例绑定 MQTT generation/epoch 和不复用 nonce；Start/Stop/远程信令串行化，断线先撤销，
再在 worker 中停止旧 peer。旧同名 session 回调不能清除替换实例；已经进入 SDK 的单次操作
允许完成，随后收尾，不代表物理撤回。

026 的精确 Stop 使用原 Start 命令号 `startCommandNo`，与 `sessionId` 一起指向实例。
成功结果以 `stopOutcome: stopped / already_stopped` 区分本次完成停止和此前已完成清理；
后者要求当前 generation/epoch/authority 内最新已完成实例的证明，未知或不匹配身份仍失败。
证明只在成功 Start 对应的原生 Stop 返回后建立，不把 terminal 回调、撤销或 UI 已关闭当成
清理完成。精确 Stop 缓存成功还核验原实例和连接作用域；原记录不改写。未传启动身份的
旧客户端仅保留活跃 session 的 Stop，无法对同名重用提供强实例隔离。字段、错误、去重
窗口与软件/物理边界统一见 [Exact stream Stop 合同](rodak-aiot-contract-v1.md#exact-stream-stop-026)。

屏幕输入由生产 `RemoteInputController` 管理，文本/按键/指针/延迟导航在最终 LVGL 入口
复查 stream lease 和独立 enable grant；旧 cleanup 不释放新 owner 的指针，析构使遗留回调
失效。`DisplayControlAckTracker` 用实际 peer 实例标识隔离旧 reply、旧已取出批次和新实例
复用的序号，最终 sender 还在 peer API 锁内复查。显式控制授权、只读默认和限速保持。

`tests/remote_input` 的 15 项真实 LVGL/生产 helper 测试，以及完整编译生产显示服务的
`tests/display_control_ack_service` 的 13 项 ACK 编码/发送测试均通过 Debug 和 sanitizer。
后者仅用 peer API fake 捕获真实参数和字节，不证明 SCTP/WebRTC 出线。结合真实 MQTT 服务
的生命周期测试，软件可验证实例隔离；下方旧 COM3 证据不自动覆盖本轮改动。

## 2026-10-07 / 018 ACK 重试、取消与设备边界

018 源码 `d251791` 中的 `c48d55a` 在原 peer instance 内保留 ACK 队首，仅对
`WOULD_BLOCK/NO_MEM` 做后续轮次重试；SDK 接受后出队，后续 ACK 不越过 FIFO，设备动作
不重放。上限是从首次发送尝试起一秒或 50 次，不是从浏览器点击计时；桌面三秒 timeout
未修改。等待时继续 `esp_peer_main_loop` 并让 JPEG 发送让路，分片之间发现待发 ACK 时
放弃剩余 latest-only 帧。

JSON 的所有字段和输出缓冲都成功后才发送；不发送 `{}` 或残缺 ACK。reason 复制 OOM
保留队首身份与预算，入队 OOM/32 项溢出、不可恢复错误或重试耗尽终止原实例。Stop/Start
仍撤销旧 ACK；旧序号或同名 session 不能将回执迁到新 peer。

RemoteInputController 在 page transition/local touch/disable 时，使用固定容量集合移出
待取消回调并在锁外回复 `page_transition/local_touch_active/control_disabled`；已经执行的
导航保留其实际结果，尚未执行的导航取消后不重复回复。pointer 的 `accepted=true` 只证明
最终输入入口接受样本，不能视为其后 LVGL 点击及业务操作成功。

完整生产 ACK 与取消入口 **21 项**、生产 helper 与真实 host LVGL **20 项**分别通过
Debug/ASan/UBSan/leak。旧 `c755358` 生产源码同套件分别有 **8 / 4 项失败**；覆盖临时压力、
FIFO、实例替换、部分 JSON、reason/队列分配失败、满队列无分配取消、真实取消回调末端
OOM、限时终止与 JPEG 让路。这些 host tests 不模拟无线调度或证明端到端时延。

018 包 `20261007-120809` 已通过保 NVS 刷写与启动确认，但首个 Camera 退出窗口在
`Closing app: camera` 后串口静默，直到受控 RTS reset，不能算通过。复位后的独立窗口
Camera 正常退出后，A3 三次解码成功；第三次 down83 的 accepted 回执耗时 **7.745 秒**，
up84 在 **7.741 秒**后被拒绝 `control_disabled`，自动 disable85 的 accepted 回执耗时
**4.741 秒**。不能把这些结果写成“动作均成功，只是 ACK 丢失”；取消释放是否触发后续
LVGL 点击及迟到发生在哪个阶段，需独立软件/实机验证。

JPEG 分阶段采样记录约 **8,084 B** 编码器内部瞬时占用；MbedTLS 和任务栈使用 PSRAM
不代表 JPEG codec 全部工作区也在 PSRAM。018 没有 allocator 迁移。完整包身份、两段
日志和开放门禁见 [媒体验收](media-browsing.md#018-camera-分配保护ack-与阶段诊断) 与
[发布检查](ota-release-readiness.md#2026-10-07-resource-and-ack-diagnostics-018)，发布保持 NO_GO。

## 019 取消手势与 LVGL 状态

源码 `42b12ccd183577a331433a69218c8fcc6191a0ee` 修正已在真实 host LVGL 复现的取消边界：
远程 down 已读取时，仅清除待处理 up 并回复拒绝仍可能让下一次普通 RELEASED 触发 CLICKED。
现在取消无条件推进代次，controller 先交付取消释放，bridge 在 LVGL 线程重置旧远程手势，
再读取新的按下。物理 pressed 先发布、OnLocalTouch 后执行的交接也会重置；正常 up、
物理自身点击及已最终准入操作保留原语义。此修正不解释所有远控迟到，也不改变 ACK 的
原实例 FIFO 或一秒/50 次发送重试预算。

RemoteInput **30 项 Debug/ASan/UBSan/leak**、ACK **21 项 ASan**、Home **43 项 ASan**通过。
真实 LVGL 新场景包括 disable/revoke/page transition/local takeover、快速 reenable/new-down、
up 出队后取消，以及两种物理接管先后顺序；条件代次/缺少 reset 的负变异分别失败 **2 / 8 项**。
记录位于 WSL `~/.cache/rodakos-cancel-019-evidence/`，不借 018 全量结果证明 019。

包 `20261007-122610` / `media-control-cancel-019` 的 main **7,119,152 B**，SHA-256
`665fffea5212885d839290bebaec20500d7690367b80cfb89436281cd454e278`；保 NVS 刷写及
Recovery/main/Home/OTA 确认通过，原 bound/tokenVersion=4、MQTT 连接保持。
随包 Camera 阶段日志 `13b8d3f` 仅帮助定位退出停顿；JPEG allocator 未改变。
019 screen-first Camera 仍在 DMA largest=6,656 B 时失败；停止 screen 后恢复，实际运行
56.779 秒 / 792 帧并完成全部退出阶段，不能因此关闭 018 停顿的原因调查。

定向取消窗口中，held-down93 accepted 后停用远控，up94 rejected/control_disabled（20 ms）、
disable95 accepted（22 ms），没有额外 PNG 加载；重新授权后正常 retry97/98 accepted，
A3 190 ms 解码成功。后续压力 retry99/100 仍在 3.870/3.869 秒后拒绝，disable101 accepted
耗时 868 ms，三个输入在 controller 解析入口同一设备日志时间出现，没有第三次 PNG 加载。
日志未记录 ACK retry，入口之前的 SDK/网络/调度迟到仍需拆分验证；取消误点击的定向通过
不等于端到端响应通过。串口入口日志到达主机分别在发送后 3,861/3,860/857 ms，ACK 在其后
9–11 ms 到达；该主机时间包含 USB/日志缓冲，不能作为精确网络时延。

已记录 JPEG 周期合计148/148/0，无捕获到的 panic/abort/reboot；Home、screen/remote stop
收尾没有重启，COM3 已释放。最终 MQTT/wake 连接与监听正常，但 internal largest 7,680 B
仍小于 8 KiB soak 门槛。证据为 Rodak `.codex-temp/resource-window-019/serial.log`、
`serial-timing.jsonl`、`control-final.json` 与 `result.json`，发布 NO_GO 保持；制品完整身份见
[019 媒体记录](media-browsing.md#019-取消远程手势的修正与制品)。

## 2026-10-01 验证

- ESP-IDF 6.0.2 构建通过；`tests/home_ui` 使用真实 LVGL，34 tests / 0 failures。
- COM3 非擦除刷新包 `20261001-062518`，Recovery → main → Home → 本地 OTA
  确认全部通过。主镜像 SHA-256：
  `f9a173df857d7d2af6944798fc03209a25f72f787ce8a2e6ffdca410dc9a8bb5`。
- Rodak 文本 E2E 通过视觉复验，Folder 字段实际显示 `远程控制中ABC`；无焦点拒绝保留
  控制租约，Cancel 和重复 Home 恢复原桌面，退出后两个通道及 PeerConnection 关闭。
- 五分钟屏幕流验证包含 4 轮、3 次重建、57 个采样及 283 张 JPEG；6 个总会话均为
  320×240，双通道、只读默认值、camera/display 互斥以及停止/导航清理通过。相机启停
  3/3 通过，流运行期间没有设备复位。

实测首帧为 5.228–11.861 秒，静态 Home 约 1 FPS；请求上限 5 FPS 不代表实测持续帧率。
服务关闭和恢复后，既有 MQTT outbox 隔离策略触发过一次主动复位，不能归为屏幕流崩溃。

以下证据为本机生成物，不提交到 Git：

- `build/host-home-ui/Testing/Temporary/LastTest.log`
- `build/logs/display-text-final-soak/serial.log`
- `build/logs/display-text-final-soak-tail/serial.log`
- 邻接 Rodak 仓库的 `build/logs/e2e-display-text-result.json`
- 邻接 Rodak 仓库的 `build/logs/display-text-final-soak/e2e-display-result.json`

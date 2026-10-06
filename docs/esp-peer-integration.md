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

屏幕输入由生产 `RemoteInputController` 管理，文本/按键/指针/延迟导航在最终 LVGL 入口
复查 stream lease 和独立 enable grant；旧 cleanup 不释放新 owner 的指针，析构使遗留回调
失效。`DisplayControlAckTracker` 用实际 peer 实例标识隔离旧 reply、旧已取出批次和新实例
复用的序号，最终 sender 还在 peer API 锁内复查。显式控制授权、只读默认和限速保持。

`tests/remote_input` 的 15 项真实 LVGL/生产 helper 测试，以及完整编译生产显示服务的
`tests/display_control_ack_service` 的 13 项 ACK 编码/发送测试均通过 Debug 和 sanitizer。
后者仅用 peer API fake 捕获真实参数和字节，不证明 SCTP/WebRTC 出线。结合真实 MQTT 服务
的生命周期测试，软件可验证实例隔离；下方旧 COM3 证据不自动覆盖本轮改动。

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

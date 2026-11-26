# 固件源码导读、构建期补丁避坑与真机语音/OTA 排障手册

> **参考规范**：本文基于仓库内的 `docs/aiot-implementation-notes.md`、`docs/mqtt-ota-sd-recovery.md`、`docs/production-signing.md`、`docs/voice-assistant.md`、`docs/voice-aec-integration.md` 以及 `cmake/`、`patches/` 目录整理，帮助固件开发者将前 10 篇协议规范直接落到代码位置与真机排障流程。

> 新增排障入口：内部内存看 Main health 的[最大连续块](https://github.com/rymcu/rodakos/blob/f4d834d3df8ea03f0a4e914d0e7558b78b3a7fa5/TROUBLESHOOTING.md#internal-sram-pressure)；btn_bind 的 PSRAM 栈只能读 RAM 缓存，NVS 由初始化或绑定更新路径处理。LVGL 优先 PSRAM，不借用为 DMA 保留的内存池。升级组件先核对 [codec overlay](https://github.com/rymcu/rodakos/blob/f4d834d3df8ea03f0a4e914d0e7558b78b3a7fa5/tools/prepare_codec_volume_patch.py) 的源码校验和来源，不直接改托管目录。[语音排障](https://github.com/rymcu/rodakos/blob/f4d834d3df8ea03f0a4e914d0e7558b78b3a7fa5/docs/voice-aec-integration.md#interruption-policy)应区分串口注入、无真人发言对照和真人唤醒／插话。

## 1. 协议到固件源码的快速定位表

当你准备修改某项协议行为或排查线上问题时，可直接按功能域定位到 `main/phone_os/` 与 `components/`：

| 协议功能域 | 核心实现文件（`main/phone_os/` 或 `components/`） |
| :--- | :--- |
| **AIoT 身份、绑定与凭据续期** | `device_cloud_config.*`、`device_pairing_protocol.*`、`device_pairing_policy.h`、`cloud_credential_freshness.h`、`cloud_credential_renewal.h`、`cloud_diagnostic.h` |
| **统一 MQTT、指令账本与 Effect** | `unified_mqtt_service.*`、`mqtt_command_ledger.*`、`mqtt_volume_effect.*`、`mqtt_light_effect.*`、`mqtt_credential_refresh_policy.*`、`stream_lease.h` |
| **实时语音契约与传输层** | `realtime_voice_contract.*`、`realtime_voice_transport.*`、`realtime_voice_reconnect_policy.*`、`voice_assistant_service.*`、`voice_assistant_reconnect_coordinator.*` |
| **声学前端、唤醒与打断策略** | `voice_audio_frontend.*`、`voice_wake_service.*`、`voice_wake_frontend.*`、`voice_identity.*`、`voice_barge_in_policy.*`、`voice_conversation_policy.*` |
| **入网配网与可信发现** | `serial_provisioning_*`、`setup_qr_*`、`trust_setup_*`、`lan_onboarding_service.*`、`server_trust*` |
| **WebRTC 推流与远程输入** | `webrtc-peer-config.h`、`webrtc_camera_service.*`、`webrtc_display_service.*`、`remote_input_controller.*`、`display_control_ack_tracker.h`、`components/esp_peer/` |
| **OTA 状态机与签名日志** | `ota_update_service.*`、`components/rodak_ota_state/`、`recovery/` |

## 2. 构建期补丁体系（`cmake/` & `patches/`）避坑指南

RodakOS **不会**直接篡改托管组件源目录，而是通过 `cmake/*_patch.cmake` 与 `patches/<组件>/<版本>/` 在构建期应用经过源码校验的覆写（Overlay），每个补丁目录均附带 `provenance.json` 与 `tools/prepare_*_patch.py`。升级任何组件前必须检查对应补丁是否仍匹配：

| 补丁目录 / CMake 脚本 | 覆盖组件与版本 | 解决的核心协议/稳定性问题 |
| :--- | :--- | :--- |
| `patches/esp_codec_dev/1.5.7/`（`codec_volume_patch.cmake`） | `esp_codec_dev` 1.5.7 | 输出音量仅在驱动成功时更新缓存；覆写同时覆盖 codec/I2S 打开失败与输入增益，校验 `esp_codec_dev.c`、`audio_codec_data_i2s.c`、ES7210 源文件的受审查来源。 |
| `patches/esp_mqtt/1.0.0/`（`mqtt_event_patch.cmake`） | `esp_mqtt` 1.0.0 | 提供定制 `MQTT_USER_EVENT` 派发与任务存储布局，保障锁外 QoS 0 直接发布与 025 无重启客户端替换。 |
| `patches/esp_websocket_client/1.8.0/`（`websocket_redirect_patch.cmake`） | `esp_websocket_client` 1.8.0 | 在二次握手前直接拒绝 HTTP 重定向，防止 Pin 模式与数值路由下的 Bearer Token 泄露。 |
| `patches/aes_dma_cleanup/6.0.2/` | ESP-IDF 6.0.2 | 修复 AES DMA 清理路径隐患。 |
| `patches/camera_teardown/`、`dvp_deinit/`、`dvp_rcc/`（`2.3.0`） | 相机 / DVP 2.3.0 | 保障相机启停与 DVP 反初始化时正确归还时钟与 DMA 资源。 |
| `patches/lvgl/9.3.0/`（`lodepng_patch.cmake`） | LVGL 9.3.0 | 约束 LodePNG 解码内存分配行为。 |

## 3. 16 MiB Flash 布局、SD Recovery OTA 与离线生产签名

### 3.1 分区表与容量红线（`docs/mqtt-ota-sd-recovery.md`）

| 分区名 | 偏移 | 大小 | 用途 |
| :--- | ---: | ---: | :--- |
| `nvs` | `0x9000` | `0x6000` | 设备与应用配置 |
| `otadata` | `0xF000` | `0x2000` | 启动槽选择与回滚状态 |
| `phy_init` | `0x11000` | `0x1000` | PHY 射频校准 |
| `ota_state` | `0x12000` | `0x6000` | 独立 OTA Recovery 日志（A/B 双份 + generation + CRC32） |
| `recovery` | `0x20000` | `0x280000` | 不可变出厂 Recovery 镜像（2.5 MiB） |
| `app`（`ota_0`） | `0x2A0000` | `0xD50000` | 主应用槽（13.3125 MiB，发布镜像应控制在 **< 12.5 MiB**） |
| `coredump` | `0xFF0000` | `0x10000` | 崩溃转储 |

### 3.2 双门控启动确认与 Recovery ABI 约束

* **本地健康确认 vs 云端确认解耦**：新固件启动跑完本地服务与 UI 初始化后，即取消 bootloader 回滚、持久化 `confirmed` 并打印 `OtaUpdate: Local boot confirmation complete`（有线刷机脚本必须等此日志后再附着 `--no-reset` 监视器，避免在 `PENDING_VERIFY` 窗口误复位触发回滚）。随后 MQTT 连上后才将 SD 卡 `pending.bin` 晋升为 `installed.bin` 并上报 HTTP 结果，持久化 `report_acknowledged`。因此短暂局域网断网不会误回滚健康固件。
* **不可变 Recovery ABI**：`ota_state` 的 Schema v1（阶段码、记录大小与字段上限）属于不可变 Recovery ABI，普通 OTA 必须持续写入 v1；变更 Schema 必须走有线迁移同时替换 Recovery 与主程序。

### 3.3 Manifest v2 离线生产签名流程（`docs/production-signing.md`）

生产私钥**严禁**离开离线签名机（`build/test-keys/` 下的开发密钥绝不可用于发布）。RSA-2048 PKCS#1 v1.5 对以下 8 行 UTF-8 原文（含末尾换行符）的 SHA-256 摘要签名，生成 512 个小写十六进制字符的 `signatureValue`（256 字节 RSA 签名）：

```text
rodakos-ota-v2
rymcu-bigsmart
ota_0
rsa2048-sha256
<taskNo>
<version>
<fileSize>
<lowercase SHA-256>
```

标准 5 步离线发布流：
1. **离线机生成密钥（仅一次）**：`openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -aes-256-cbc` 生成加密私钥与公钥，并记录公钥 DER SHA-256 指纹（固件内嵌为 `RODAK_OTA_TRUST:<fingerprint>`）。
2. **构建机仅持公钥打包**：运行 `.\build_ota_bundle.ps1 -VerificationKeyPath <pub.pem> -SigningTaskNo <taskNo> -SigningVersion <ver> -OfflineSigning`，输出 `signing-request.json`。
3. **离线机签名**：逐字节提取 `signing-request.json` 中的 `payload`，使用 `openssl dgst -sha256 -sign` 生成 `signature.hex`。
4. **构建机挂载并验包**：运行 `python tools/ota_security.py attach-signature ...` 与 `verify-package --directory <pkg>`，并在 Rodak 侧通过 `server:attachOtaTaskSignature` 绑定同一任务签名（Rodak 重试会生成新 `taskNo`，必须重新签名）。
5. **首次有线迁移回读校验**：刷写后使用 `esptool read-flash` 回读 `0x20000`（`recovery`）与 `0x2a0000`（`app`）逐字节比对，并确认 `flash_and_test.ps1 -VerifyOnly` 通过。

## 4. 语音端侧机制：后台 Token 续期、AEC 链路与打断调优

### 4.1 为什么必须有后台 Token 续期（`cloud_credential_renewal.h`）

唤醒时的凭据准备仅分配 **3 秒**共享预算；但在热点网络下，单次 WebSocket 建连中位数就达 1.9 s，一旦 Token 过期需串行跑 `bootstrap` + `auth/token` 两次 HTTPS，必然导致唤醒超时。为此 MQTT worker 在以下条件下静默续期：

* **触发窗口**：Token 过期前 `1/4` 生命周期（钳位在 **60 s – 300 s**，且不早于半衰期；如 3600 s Token 提前 300 s 续期），仅在 MQTT 在线、无活跃语音会话且无待恢复任务时执行。
* **非阻塞让路**：非阻塞获取刷新锁，优先让位给唤醒、OTA 或 MQTT 恢复；单次续期预算 **20 s**，网络失败时保持旧 Token 并按 **15 s 起步倍增至 8 分钟**重试；Rodak 下发同 `tokenVersion` 的新 Token，不会踢掉正在使用的会话。

### 4.2 BigSmart 硬件 AEC 链路与打断现状（`docs/voice-aec-integration.md`）

* **硬件通道与增益**：ES7210 暴露 4 路 16 kHz TDM 槽位（`MIC1 = ADC_MIC2P/N`、`MIC2 = ADC_MIC1P/N`、`MIC3 = ES8311` 扬声器回采参考 `R`）。对话采音采用 `InputGainProfile::kAecReference10Db`（麦克风 30 dB、参考通道 10 dB / 量化后 9 dB），送入 ESP-SR AFE（`AEC_MODE_VOIP_HIGH_PERF`）与 WebRTC VAD（`VAD_MODE_0`）。高电平回采时可在 ADC 保持打开的情况下将麦克风／参考增益临时切为 24／0 dB；回采不活跃满 1 秒后恢复正常档。
* **ADC 优先级与连续采音**：对话前端优先级 **30** > 录音 **20** > 本地唤醒 **10**。`StopRecorderForPlayback()` 刻意为空操作，以便播放 TTS 时持续提供 AEC/VAD 信号并保留 **80 帧（4.8 s）**预录队列。
* **设备侧打断门限与证据**：实际播放开始后先忽略 400 ms，再要求至少 180 ms 新鲜静音及 360 ms 连续 VAD 语音；末 360 ms 内至少 180 ms 同时通过去直流 RMS ≥ 256 及回声功率门限。回声比较取过去 200 ms 的麦克风输入峰值功率，通常按 1/32，强语音路径按 1/96；输入功率缺失时跳过该门限。确认后发送 `playback.abort`，`vad` 是否发送仍取决于策略和所处阶段。已验证串口注入和真人不发言的播放对照；真人插话仍需实测。

## 5. 真机串口语音与 AEC 诊断工具箱

固件内置 `RODAK_VOICE_TEST_V1` 串口诊断协议，不修改用户配置即可复现完整语音链路与声学采音：

| 诊断用途 | 串口命令（加前缀 `RODAK_VOICE_TEST_V1 `） | 配套自动化脚本与边界 |
| :--- | :--- | :--- |
| **注入 PCM 跑端到端生命周期** | `audio_begin <count>`、`audio_chunk <offset> <PCM16LE_hex>`、`wake`、`stop`、`audio_clear` | 存 PSRAM，最长 **10 s**，单块 ≤ 256 samples；配合 `tools/run_serial_voice_test.py`、`tools/capture_voice_stability.py` 验证传输/门控/清理（不替代真人声学测试）。 |
| **四通道 TDM + AFE 声学抓取** | `aec_arm <ms>`、`aec_stop`、`aec_status`、`aec_read <ch> <offset> <count>`、`aec_clear` | 最长 **6 s**，停采后按每块 ≤ 256 samples 回读；使用 `tools/capture_aec_diagnostics.py` 导出 WAV 与 `metadata.json`，再用 `tools/analyze_aec_diagnostics.py` 分析电平、削波与 `tdm-slot-1` 参考时延相关性。 |
| **唤醒词评估与压测** | — | `tools/run_wake_eval.py`（结合 `docs/voice-wake-evaluation.md`）与 `tools/capture_release_stability.py`。 |

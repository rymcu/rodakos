# Rodak 协议开发者上手手册总索引与学习路线图

> **系列定位**：本套技术文档基于 [rymcu/rodakos](https://github.com/rymcu/rodakos) 仓库的权威契约（[rodak-aiot-contract-v1.md](https://github.com/rymcu/rodakos/blob/main/docs/rodak-aiot-contract-v1.md)、[rodak-realtime-voice-contract-v1.md](https://github.com/rymcu/rodakos/blob/main/docs/rodak-realtime-voice-contract-v1.md)）及各子系统设计文档编写，帮助新人开发者从零理解协议边界、落地代码实现并通过自动化契约与真机验收。

> 版本基线（2026-11-28）：[RodakOS main @ 38f16b0d](https://github.com/rymcu/rodakos/commit/38f16b0d2f916106349fd93206edf16854ccae8f)，源码版本 0.1.6（含 PR #71 全局语音字幕浮层与消息中心）。协议与实现定位以该固定提交为准；草案事件（含 `turn.accepted`、`turn.progress`、`output.caption` 与消息中心 `inbox/changed` / HTTPS 接口）端云均已落地，主机检查、开发包和指定端云组合的真机验收分别取证。入口：[MQTT 与消息中心（03）](./03_rodak_aiot_v1_mqtt_commands_shadow_and_voice.md)、[语音信令（05）](./05_rodak_realtime_voice_v1_architecture_and_signaling.md)、[验收界限（07）](./07_rodak_realtime_voice_v1_reliability_gating_and_testing.md)、[固件排障（11）](./11_rodak_firmware_source_map_patches_and_troubleshooting.md)、[模拟器（13）](./13_rodak_protocol_minimal_simulator_walkthrough.md)。

## 1. 全系列文章目录（共 13 篇实战指南）

### 第一部分：Rodak AIoT v1 核心控制面（`01`–`04`）

| 编号 | 文件名 | 核心内容 |
| :---: | :--- | :--- |
| `01` | [01_rodak_aiot_v1_overview_and_architecture.md](./01_rodak_aiot_v1_overview_and_architecture.md) | 协议权威边界、核心模块分工（含 `InboxService`）、5 类凭证术语、`rymcu_bigsmart` vs `rymcu-bigsmart` 防坑、ESP-IDF 6.0.2 构建与烧录红线。 |
| `02` | [02_rodak_aiot_v1_binding_auth_and_security.md](./02_rodak_aiot_v1_binding_auth_and_security.md) | 9 步入网绑定时序、状态机、HTTP API 端点（含消息中心快照与动作草案）及统一信封错误码陷阱、Token 刷新、LAN Trust Setup 与安全模式对比。 |
| `03` | [03_rodak_aiot_v1_mqtt_commands_shadow_and_voice.md](./03_rodak_aiot_v1_mqtt_commands_shadow_and_voice.md) | Unified MQTT 连接与 Topic 清单、消息中心草案（§7.6：`inbox/changed` 无正文脏信号 + HTTPS 快照与幂等待办动作）、指令回执与推流互斥、Shadow/Telemetry、L0–L4 证据等级与实时语音交接。 |
| `04` | [04_rodak_aiot_v1_ota_and_contract_verification.md](./04_rodak_aiot_v1_ota_and_contract_verification.md) | OTA 协同流程与 Recovery 分区刷写、`contracts/rodak-aiot/v1/` 机器可读契约校验（含 `inbox-*`、`output-caption` 与 `x-orderedFields`）、宿主测试与提 PR 自检清单。 |

### 第二部分：Rodak Realtime Voice v1 实时语音（`05`–`07`）

| 编号 | 文件名 | 核心内容 |
| :---: | :--- | :--- |
| `05` | [05_rodak_realtime_voice_v1_architecture_and_signaling.md](./05_rodak_realtime_voice_v1_architecture_and_signaling.md) | `realtimeVoice` 服务发现（含描述符 `features: ["output_caption"]`）、WebSocket 鉴权头、12 层 JSON 嵌套与大小限制、固定的 12 项描述符兼容事件名，以及 14 项控制事件联合体（含 `turn.accepted`、`turn.progress` 与 `output.caption` 协商及 Shell 全局浮层）。 |
| `06` | [06_rodak_realtime_voice_v1_audio_rav1_vad_and_announce.md](./06_rodak_realtime_voice_v1_audio_rav1_vad_and_announce.md) | Opus 上下行规格、RAV1 `12+N` 二进制帧、按 RAV1 帧区间同步的回复字幕（`output.caption` §6.2）、3 种 VAD 策略、1.5 秒多设备唤醒仲裁、MQTT 触发的 Announce 纯下行播报（支持字幕协商）。 |
| `07` | [07_rodak_realtime_voice_v1_reliability_gating_and_testing.md](./07_rodak_realtime_voice_v1_reliability_gating_and_testing.md) | 四重代次门控与 `output.caption` 序/区间门控、重连资格矩阵、20s/120s/130s 看门狗、契约测试套件及 2026-11-27 实机联调与未覆盖边界。 |

### 第三部分：进阶子系统、固件落地与零硬件联调（`08`–`13`）

| 编号 | 文件名 | 核心内容 |
| :---: | :--- | :--- |
| `08` | [08_rodak_provisioning_and_lan_trust_setup.md](./08_rodak_provisioning_and_lan_trust_setup.md) | USB 串口配网（`RODAK_PROVISION_V1` + `bindingProof`）、扫码配网（`RODAK:`/`WIFI:`）、无 USB SRP-6a 局域网建信与 mDNS 可信发现。 |
| `09` | [09_rodak_mqtt_advanced_semantics_and_effects.md](./09_rodak_mqtt_advanced_semantics_and_effects.md) | 64 条指令去重账本与精确 Stop（026）、音量/灯光 Effect 与 L3 回执、025 同 Authority 无重启替换客户端、Shadow 语音身份。 |
| `10` | [10_rodak_webrtc_streaming_and_remote_control.md](./10_rodak_webrtc_streaming_and_remote_control.md) | `esp_peer` 1.5.6 推流与 MQTT 边带信令、屏幕分享首帧前隐藏字幕与消息正文、`screen-control` 远程输入协议、ESP32-S3 内部 RAM 碎片化 3 大真实缺陷。 |
| `11` | [11_rodak_firmware_source_map_patches_and_troubleshooting.md](./11_rodak_firmware_source_map_patches_and_troubleshooting.md) | 协议到 `main/phone_os/` 源码定位表（含消息中心与字幕浮层）、`patches/` 补丁清单、16 MiB Flash 分区与离线生产签名、后台 Token 续期、AEC 与字幕/消息中心串口诊断。 |
| `12` | [12_rodak_appearance_package_and_verification.md](./12_rodak_appearance_package_and_verification.md) | 外观资源包物理触摸公钥固定、断点续传、RAP1 二进制格式、RSA-2048 清单验签、内存/SD 双槽预算与启动试用回滚。 |
| `13` | [13_rodak_protocol_minimal_simulator_walkthrough.md](./13_rodak_protocol_minimal_simulator_walkthrough.md) | 基于仓库夹具的 Python 分段联调示例：Bootstrap、绑定、MQTT（含可选 `inbox_changed`）、消息中心快照与幂等动作、WebSocket 握手（含 `output_caption` 协商）与离线 RAV1 解析；并非完整语音闭环。 |

## 2. 按角色推荐的上手路线

| 开发者角色 | 推荐必读顺序 | 重点关注 |
| :--- | :--- | :--- |
| **服务端开发（Rodak / IntelliConnect）** | `01` → `02` → `03` → `05` → `06` → `08` → `09` → `13` | HTTP 信封、消息中心 `inbox/changed` 与 `opId` 幂等、SRP-6a 建信、QR 公网 Nginx 限制、Effect 回执、RAV1、`output.caption` 与多设备仲裁。 |
| **固件开发（RodakOS / ESP32-S3）** | `01` → `04` → `07` → `08` → `09` → `10` → `11` → `12` | 锁序与四重门控、`assistant_play` 字幕同步、`InboxService` 内存/栈与让路边界、屏幕分享隐私遮挡、内部 SRAM 连续块、构建期补丁、`ota_state` Recovery ABI 与主机测试。 |
| **测试 / 模拟器 / 协议联调** | `01` → `02` → `03` → `05` → `06` → `07` → `13` | `contracts/` 正反例夹具（含 `inbox-*` 与 `output-caption`）、`check_aiot_contract.py`、`RODAK_VOICE_TEST_V1` 与 Python 模拟器。 |

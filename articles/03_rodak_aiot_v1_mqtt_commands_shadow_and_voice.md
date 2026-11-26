# 统一 MQTT、指令系统、设备影子与实时语音交接指南

> **参考规范**：本文依据 [rodak-aiot-contract-v1.md](https://github.com/rymcu/rodakos/blob/main/docs/rodak-aiot-contract-v1.md)（§7–§11）整理，帮助开发者实现 `UnifiedMqttService`（UMS）、设备指令处理、影子同步以及 `RodakRealtimeVoiceTransport` 的交接逻辑。

## 1. Unified MQTT 连接配置与 Topic 全景

绑定确认或刷新 Token 后，服务端返回的 `unifiedMqtt` 描述符给出了完整的 MQTT 连接与路由信息（对应的 AsyncAPI 定义位于 `contracts/rodak-aiot/v1/asyncapi.yaml`）：

* **核心连接字段**：`protocol_version: 2`、`broker_address`、`broker_port`、`username`（即 `deviceKey`）、`password`（即 `accessToken`）、`keepalive: 240`、`device_key`、`http_base_url`、`http_bearer_token`、`home_enabled: 0` 以及完整的 `topics` 映射。
* **传输层安全**：Legacy 模式允许明文 MQTT；Pin 模式强制使用 `mqtts` 并校验服务端信任锚点与固定域名。

### Topic 订阅与发布清单

以下 `{k}` 代表设备的 `deviceKey`。全链路默认以 **QoS 0** 为主，唯一特例是 `ota/progress` 上报 `staged` 状态时 RodakOS 使用 **QoS 1** 确保送达。

| 方向 | Topic 路径 | 默认 QoS | 核心用途 |
| :--- | :--- | :--- | :--- |
| **设备订阅（下行）** | `devices/{k}/shadow/desired` | 0 | 接收服务端下发的期望影子状态（如音量、唤醒配置等）。 |
| **设备订阅（下行）** | `devices/{k}/ota/notify` | 0 | 接收 OTA 升级通知，触发检查与下载流程。 |
| **设备订阅（下行）** | `devices/{k}/commands/+` | 0 | 订阅 `devices/{k}/commands/{commandNo}` 接收单次指令。 |
| **设备订阅（下行）** | `devices/{k}/pc_status` | 0 | 接收宿主 PC 状态同步消息。 |
| **设备发布（上行）** | `devices/{k}/telemetry` | 0 | 上报设备周期性或事件触发的遥测数据。 |
| **设备发布（上行）** | `devices/{k}/shadow/report` | 0 | 上报设备当前实际影子状态。 |
| **设备发布（上行）** | `devices/{k}/ota/progress` | 0（`staged` 为 **1**） | 上报 OTA 各阶段进度与最终结果。 |
| **设备发布（上行）** | `devices/{k}/effects/receipt` | 0 | 上报执行效果回执（用于证据等级声明）。 |
| **设备发布（上行）** | `devices/{k}/commands/{commandNo}/ack` | 0 | 回复对应 `commandNo` 指令的执行结果或错误码。 |

> **上线标准动作**：设备成功连接 MQTT Broker 后，应立即完成 4 个下行 Topic 的订阅，并主动发布初始 `telemetry` 与 `shadow/report`。

## 2. 指令系统（Commands）实现规范

当设备从 `devices/{k}/commands/{commandNo}` 收到指令后，必须向对应的 `devices/{k}/commands/{commandNo}/ack` 发布回执。

### 2.1 标准 Ack 响应格式

* **执行成功**：

```json
{
  "status": "ok",
  "result": {}
}
```

* **执行失败或不支持**：

```json
{
  "status": "error",
  "errorCode": "unsupported_command"
}
```

### 2.2 支持的指令集与语义约束（§8）

| 指令名 | 关键参数 / 机制 | 实现注意事项 |
| :--- | :--- | :--- |
| `ping` | 连通性探测 | 立即返回 `status: "ok"` 响应。 |
| `voice.announce` | 优先级支持 `normal` / `urgent` | 播报语音提示，需区分普通与紧急优先级队列。 |
| `camera.stream.start` / `stop` / `signal` | 基于 WebRTC datachannel（固件侧依托 `components/esp_peer/`） | **与屏幕推流互斥**；启动前需校验当前无活跃的屏幕推流会话。 |
| `display.stream.start` / `stop` / `signal` | 基于 WebRTC datachannel | **与摄像头推流互斥**；启动前需校验当前无活跃的摄像头推流会话。 |
| 其他未定义指令 | — | 必须明确回复 `{"status": "error", "errorCode": "unsupported_command"}`，禁止静默丢弃。 |

> **设计边界提醒**：**音量控制不走指令系统**，而是通过设备影子（Shadow）下发期望状态并上报实际状态。请勿自行扩展音量类 Command。

## 3. Telemetry、Shadow 同步与证据等级（§9 & §10）

### 3.1 遥测与影子协同

* **Telemetry**：按 `contracts/rodak-aiot/v1/schemas/` 定义的结构通过 `devices/{k}/telemetry` 上报设备运行指标。
* **Shadow**：服务端通过 `shadow/desired` 下发期望配置（例如音量、`VoiceWakeService` 唤醒配置等），设备应用变更后通过 `shadow/report` 回报当前状态。注意：任何敏感凭证绝对不允许写入 Shadow。

### 3.2 证据等级（Evidence Levels，L0–L4）

为了客观衡量设备执行动作的可信度，协议 §10 定义了从 **L0（仅完成 MQTT 发布）** 到 **L4（硬件物理观测闭环验证）** 的五个证据等级：

* 目前 RodakOS 固件对**音量**与**灯光**控制最高仅声明到 **L3**（通过 `devices/{k}/effects/receipt` 发布效果回执）。
* 在回执中必须诚实标记 `physicalVerified: false`，不得在缺乏物理传感器闭环反馈的情况下虚报 L4。

## 4. 实时语音传输层交接（§11）

虽然实时语音的具体信令与帧格式由独立契约 `Rodak realtime voice v1` 规范，但设备端必须正确完成从 `rodak-aiot/v1` 到 `RodakRealtimeVoiceTransport` 的参数交接：

| 交接项 | 规范要求 |
| :--- | :--- |
| **接入地址** | 由 `realtimeVoice` 字段引导至 `<ws\|wss>://<http origin>/api/v1/aiot/devices/realtime-voice`（Legacy 允许 `ws://`，Pin 强制 `wss://`）。 |
| **鉴权方式** | 复用绑定/刷新获得的 `accessToken` 作为 `Bearer` 凭证。 |
| **音频编码规格** | **Opus** 编码，上行 **16 kHz**、下行 **24 kHz**，帧长 **60 ms**。 |
| **协议能力集** | 支持 **7 种能力**、**12 种事件**与 **3 种 VAD 策略**（其中首选 `hybrid-fallback`）。 |

---

**下一篇推荐**：阅读《OTA 安全升级协同、契约校验与联调测试清单》，掌握 Manifest v2 验签、Recovery 分区升级闭环以及提交代码前的自动化校验流程。

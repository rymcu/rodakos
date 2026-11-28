# Rodak Realtime Voice v1 架构、服务发现与控制面信令详解

> **参考规范**：本文依据 [rodak-realtime-voice-contract-v1.md](https://github.com/rymcu/rodakos/blob/main/docs/rodak-realtime-voice-contract-v1.md) 与 [rodak-aiot-contract-v1.md](https://github.com/rymcu/rodakos/blob/main/docs/rodak-aiot-contract-v1.md) 整理，面向需要实现或联调实时语音协议的固件端、服务端及模拟器开发者。

> 区分[描述符和会话事件](https://github.com/rymcu/rodakos/blob/38f16b0d2f916106349fd93206edf16854ccae8f/docs/rodak-realtime-voice-contract-v1.md#61-turn-acceptance-and-progress-draft)：描述符 `events` 保持固定的 12 项兼容事件名（保留 `mcp` 仅作兼容，普通 RodakOS 会话声明 `features.mcp: false`）。`turn.accepted` 和 `turn.progress`（§6.1）不在描述符声明，仅通过普通 `session.open` 协商（`turn_progress: 1` 依赖 `turn_accepted: 1`，纯下行 Announce 不请求）。`output.caption`（§6.2）则先由服务端在描述符 `features` 列出 `"output_caption"`，再由设备在 `session.open` 请求 `features.output_caption: 1`（普通与 Announce 会话均允许）。接受后亮紫色等待灯，450 ms 后在 [Shell 底部全局语音浮层](https://github.com/rymcu/rodakos/blob/38f16b0d2f916106349fd93206edf16854ccae8f/docs/voice-assistant.md#reply-captions-and-turn-progress)显示进度并在播放回复时切换为字幕（已取代旧版 Home 顶栏进度），过期或乱序事件静默丢弃且不延长超时。

## 1. 协议定位、模块归属与历史隔离

`rodak-realtime-voice/v1` 是 RodakOS 与规范服务端（Rodak）之间的标准实时语音线级契约（Wire Contract），基于 **WebSocket** 承载控制面 JSON 信令与二进制 Opus 音频帧。

* **单一真值来源**：以 Rodak 服务端实际行为为准；机器可读规范位于 `contracts/rodak-realtime-voice/v1/`；固件侧核心实现位于 [rymcu/rodakos](https://github.com/rymcu/rodakos) 仓库的 `main/phone_os/realtime_voice_contract.*` 和 `main/phone_os/realtime_voice_transport.*`。
* **与 XiaoZhi 协议彻底隔离**：规范解析器**必须拒绝**任何 XiaoZhi 相关字段或标记（包括 `hello`、`listen`、`tts`、`goodbye`、旧式 `type` / `session_id`、遗留 WebSocket 描述符及 `/xiaozhi` 路径）。运行时必须使用独立的协议无关会话控制器与门控，**严禁**复用历史 `xiaozhiSessionMachine`。

## 2. 通过 AIoT 描述符完成服务发现

实时语音不独立做配网，而是由 `rodak-aiot/v1` 在 `bootstrap` 与每次 AIoT Token 响应中下发 `realtimeVoice` 描述符（固件同时兼容 `realtime_voice` 别名；对于纯 MQTT 服务端，该字段视为可选）。

### 2.1 描述符核心字段与强校验规则

| 字段 | 规范值 / 要求 | 说明 |
| :--- | :--- | :--- |
| `schema` | `"rodak-realtime-voice/v1"` | 描述符结构标识。 |
| `protocol` / `protocolVersion` | `"rodak-realtime-voice"` / `1` | 协议名与版本号。 |
| `transport` / `authMode` | `"websocket"` / `"device-token"` | 传输方式与设备令牌鉴权模式。 |
| `endpoint` | `ws://` 或 `wss://` 地址 | **必须**包含 authority 与 path；**必须拒绝**含 query、fragment、userinfo 或 `/xiaozhi` 等遗留路径的 URL。 |
| `uplink` / `downlink` | 上下行音频编解码参数 | 规定 Opus 采样率、声道数与帧长。 |
| `limits` | `maxAudioFrameBytes`、`maxControlBytes` | 约束单帧音频与单条控制 JSON 的最大字节数。 |
| `capabilities` | 7 项能力集合 | `session`、`audio-input`、`audio-output`、`speech-boundaries`、`interrupt`、`assistant-events`、`mcp`。 |
| `events` | 12 个描述符兼容事件名 | `session.open`、`session.ready`、`input.start`、`input.stop`、`wake.detected`、`playback.abort`、`vad`、`output.start`、`output.stop`、`session.end`、`mcp`、`error`（旧固件会拒绝未知 `events` 名，故草案事件不加入该数组）。 |
| `vadStrategies` / `preferredVadStrategy` | VAD 策略列表与首选策略 | 缺失 `vadStrategies` 时安全默认仅启用 `server-authoritative`。 |
| `features` | 服务端特性字符串数组（唯一，最多 32 项） | 当前允许 `"output_caption"`，表示服务端支持按需下发草案 `output.caption`（§6.2）；不得包含任何历史遗留标记。 |

### 2.2 建连时机与鉴权请求头

设备在空闲监听本地唤醒词时**不会**保持云端 WebSocket 长连接，仅在以下两种场景按需建连：
1. 本地 MultiNet 引擎检测到唤醒词，发起语音对话。
2. 收到 MQTT `voice.announce` 指令，进入服务端主动播报（纯下行）会话。

建立 WebSocket 连接时，设备端必须携带以下 HTTP 请求头：

| Header | 格式与来源 |
| :--- | :--- |
| `Authorization` | `Bearer <AIoT access token>`（若 token 已自带 auth scheme 则原样使用） |
| `Protocol-Version` | `1` |
| `Device-Id` | 设备 Wi-Fi STA MAC 地址 |
| `Client-Id` | 持久化的客户端 UUID |

### 2.3 安全与凭证红线

* **Pin 模式强制加密**：若设备安装了服务端信任锚点（见仓库 `trusted-server-discovery.md`），运行时**必须**验证当前语音地址对应固定 HTTPS 源站的 `wss://` 与固定预期 TLS 域名，确认无误后才可发送 `Bearer` Token。
* **唯一凭证来源**：AIoT 下发是语音端点与访问令牌的唯一权威来源，**禁止**通过串口配网协议提交语音 token 或端点。
* **零日志泄露**：令牌、密钥及原始 `Authorization` 请求头严禁打印到日志或串口。

## 3. 控制面消息封装与解析限制

所有控制面消息均为 UTF-8 编码的 JSON 对象，解析前必须通过以下三道硬性门槛：

1. **大小限制**：单条控制消息不得超过有效传输或协商的 `maxControlBytes` 上限（协商上限最大 **256 KiB**，默认 **64 KiB / 65536 字节**；注意当前固件 WebSocket 帧累积器对入站文本硬性限制在 **64 KiB**）。超限消息直接丢弃，不送入状态机。
2. **嵌套深度限制**：外层控制 JSON 在解析前限制最大 **12 层嵌套**，防止深层嵌套耗尽嵌入式栈空间。
3. **必备字段**：必须包含 `event` 字段；握手完成后的事件必须携带非空（trim 后不为空白）的 `sessionId`。

## 4. 握手信令（`session.open` 与 `session.ready`）

每个 WebSocket 连接代次（`generation`）执行且仅执行一次握手：

* **`session.open`（Device → Server）**：
  * 包含 `protocol`、`protocolVersion`、`generation`、`vadStrategies`、`preferredVadStrategy`、`uplink`、`downlink` 以及 `features` 对象（布尔或数值标志，例如 `mcp: false`、`device_vad_epoch: 0|1`、`turn_accepted: 0|1`、`turn_progress: 0|1`、`output_caption: 0|1`；缺省等同 `0`，服务端不得发送未协商的草案事件）。
  * 仅当描述符 `features` 列出 `"output_caption"` 时，设备才在 `session.open` 中发送 `features.output_caption: 1`（普通唤醒会话与 Announce 会话均允许）。
  * 若为服务端主动播报会话，还需携带 `features.announce: true` 与 `features.announceSeq: <N>`，且不携带 `turn_accepted` / `turn_progress`。
* **`session.ready`（Server → Device）**：
  * 服务端返回一次匹配的 `session.ready`，包含匹配的连接 `generation`、`transport: "websocket"`、非空 `sessionId`、协商后的单声道 Opus `downlink` 参数以及可选的 `vadStrategy`（未提供时默认为 `server-authoritative`；若提供则必须在描述符已声明的策略列表内）。
  * **严格防重入**：重复或延迟到达的 `session.ready` 一律视为协议错误，绝不能重置正在进行的播放状态或音频序列号计数器。

## 5. 会话内控制事件全表

握手完成后，设备端与服务端通过以下事件协同完成多轮对话、打断、字幕与进度反馈及错误处理（含握手事件在内，`control-message.schema.json` 当前覆盖 **14 项**控制事件）：

| 方向 | 事件名 | 关键字段与协议语义 |
| :--- | :--- | :--- |
| Device → Server | `wake.detected` | `sessionId`、`text`（配置的唤醒词）。**仅首轮唤醒发送**，同一会话的后续追问轮次直接发 `input.start`。 |
| Device → Server | `input.start` | `sessionId`，可选 `mode`（`auto-stop` / `manual-stop` / `realtime`；唤醒首轮使用 `realtime`）。 |
| Device → Server | `input.stop` | `sessionId`，标识当前上行语音输入结束。 |
| Device → Server | `playback.abort` | `sessionId`、`reason`（`user` / `wake-word` / `vad_detected`），可选 `playbackEpoch`。用于触发播报打断（barge-in）。 |
| Device → Server | `vad` | `sessionId`、`state`（`start` / `end`）、`source`、正整数 `sequence`、`triggerMs`，可选 epoch。 |
| Server → Device | `turn.accepted`（契约草案 §6.1，端云已实现） | `sessionId`、正整数 `turnId`（每会话从 1 递增）。仅当 `session.open` 声明 `features.turn_accepted: 1` 时发送，表示服务端已决定回复该轮话语（静默、空识别或回声丢弃不发送）。 |
| Server → Device | `turn.progress`（契约草案 §6.1，端云已实现） | `sessionId`、`turnId`、正整数 `seq`（每轮从 1 递增）、`stage`（`thinking` / `tool` / `composing`），可选 `label`（≤32 Unicode 码点的固定文案，**严禁**包含工具名、参数或用户原话）。需 `features.turn_progress: 1` 且依赖 `turn_accepted: 1`。 |
| Server → Device | `output.start` | `sessionId`，可选 `text` 与 `playbackEpoch`。收到后设备准备解码下行 RAV1 二进制 Opus 帧；未协商 `output.caption` 时，短句 `text`（≤48 码点 / ≤192 UTF-8 字节）可作整轮兜底字幕。 |
| Server → Device | `output.caption`（契约草案 §6.2，端云已实现） | `sessionId`、正整数 `playbackEpoch`、`captionSeq`（每轮从 1 递增）、`firstAudioSeq`、`lastAudioSeq`（`firstAudioSeq <= lastAudioSeq` 且同轮区间不重叠递增）、`text`（去首尾空白后非空，≤48 Unicode 码点且 ≤192 UTF-8 字节，仅含播报句，**严禁**含用户原话或工具信息）。仅当 `session.open` 声明 `features.output_caption: 1` 时在对应 `output.start` 与 `output.stop` 之间下发。 |
| Server → Device | `output.stop` | `sessionId`，可选 `reason` 与 `playbackEpoch`。自然结束时表示没有更多下行音频，设备排空本地尾音后再进入下一轮；打断后的确认不应被误判为自然结束并截断已保留的插话。 |
| Server → Device | `session.end` | `sessionId`，可选 `reason`（如 `goodbye-intent`、`wake-arbitration-lost`、`announce-finished`、`announce-expired`）。设备统一按普通会话结束处理并重新布防本地唤醒；注意 `goodbye` **不是**线级事件。 |
| Server → Device | `error` | 可选 `sessionId`，必填非空 `code`、非空 `message`、布尔值 `retryable`（缺任一必填字段将被拒绝）。 |

---

**下一篇推荐**：阅读《RAV1 二进制音频帧、VAD 策略、多设备仲裁与主动播报实战》，掌握下行二进制帧解析、多设备同屋唤醒防撞以及 MQTT 触发的纯下行播报闭环。

# RAV1 二进制音频帧、VAD 策略、多设备仲裁与主动播报实战

> **参考规范**：本文依据 [rodak-realtime-voice-contract-v1.md](https://github.com/rymcu/rodakos/blob/main/docs/rodak-realtime-voice-contract-v1.md) 整理，重点拆解实时语音数据面的 RAV1 音频帧封装、VAD 协商与多设备防撞机制，以及标准对话流与主动播报（Announce）两条核心业务链路。

> 音频格式有两层：下行表列的是线级 Opus 协商参数，[当前固件按本地 16 kHz](https://github.com/rymcu/rodakos/blob/f4d834d3df8ea03f0a4e914d0e7558b78b3a7fa5/main/phone_os/voice_assistant_service.cc#L49-L66) 单声道 PCM 播放；不应据此修改握手中的下行能力。唤醒交接保持 ADC 打开。设备端插话[以发出打断前的播放状态](https://github.com/rymcu/rodakos/blob/f4d834d3df8ea03f0a4e914d0e7558b78b3a7fa5/main/phone_os/voice_assistant_service.cc#L766-L790)区分自然结束和服务端确认，避免确认触发额外 input.start 而截掉插话；真正发生在已收到自然结束的本地尾音期，则按下一轮路径重新布防。

## 1. 音频编解码规格与 RAV1 二进制帧格式

### 1.1 上下行编解码参数与载荷上限

| 方向 | 编码格式 | 声道 | 采样率 | 帧长 |
| :--- | :--- | :--- | :--- | :--- |
| **上行（Uplink）** | Opus | 单声道（1 channel） | 固定 **16 kHz** | 固定 **60 ms** |
| **下行（Downlink）** | Opus | 单声道（1 channel） | 支持 `8 / 12 / 16 / 24 / 48 kHz`（示例默认 **24 kHz**） | 支持 `5 / 10 / 20 / 40 / 60 ms`（示例默认 **60 ms**） |

* **`maxAudioFrameBytes`**：仅约束 Opus 载荷本身的大小，默认 **8 KiB（8192 字节）**，协商上限最高 **64 KiB**。

### 1.2 RAV1 二进制帧结构（`12 + N` 字节）

上下行音频都不包装在 JSON 中；当前固件发送上行、接收下行时均使用 WebSocket 二进制消息及 RAV1 包头。每条二进制消息必须严格符合 `12 + N` 字节的 RAV1 封装格式：

| 字节偏移 | 长度（Byte） | 字节序 / 类型 | 校验规则 |
| :--- | :--- | :--- | :--- |
| `0` | `4` | ASCII 字符串 | 固定魔数 `RAV1`，不匹配立即拒绝。 |
| `4` | `4` | `uint32`（大端序） | 帧序列号，必须**非零**且在当前会话/代次内**严格递增**（必须大于上一帧序列号）。 |
| `8` | `4` | `uint32`（大端序） | 载荷长度 `N`，必须严格等于消息剩余字节数（`total_len - 12 == N`），且 `N <= maxAudioFrameBytes`。 |
| `12` | `N` | 二进制载荷 | 单个完整的 Opus 音频数据包。 |

> **实现提醒**：解析器必须同时校验“剩余长度精确匹配 `N`”与“序列号严格递增”，绝不能容忍粘包、截断包或乱序/重复序号帧进入 Opus 解码器。

## 2. VAD 策略与多设备协同机制

### 2.1 三种 VAD 策略的语义区别

设备在 `session.open` 中上报支持的 `vadStrategies` 与 `preferredVadStrategy`，服务端在 `session.ready` 中确定本次会话生效的 `vadStrategy`：

| 策略名 | 决策权归属 | 设备端行为要求 |
| :--- | :--- | :--- |
| `device-authoritative` | 设备端权威 | 以设备侧上报的 `vad` 事件（`start`/`end`）作为语音起止权威边界。 |
| `server-authoritative` | 服务端权威 | 设备正常上传音频流，但**抑制发送** `vad` 控制事件。若描述符未给出 `vadStrategies` 或 `session.ready` 未指定策略，**安全默认**采用此策略。 |
| `hybrid-fallback` | 混合兜底（**默认推荐**） | 正常由服务端判断边界；当服务端未及时返回边界时，设备侧可发送 `vad` 事件作为兜底。 |

### 2.2 同屋多设备唤醒仲裁与跨设备回声抑制

在家庭或办公环境中存在多台 RodakOS 设备时，服务端承担协同仲裁职责，设备端只需遵循标准协议即可无缝配合：

* **1.5 秒唤醒仲裁**：同一 Rodak 服务端下若有多台设备在 **1.5 秒**窗口内上报 `wake.detected`，服务端将其判定为同一次唤醒，仅响应最先到达的设备；其余落选设备会收到 `session.end`（`reason: "wake-arbitration-lost"`）并被服务端断开连接。落选设备无需编写特殊分支，按普通 `session.end` 流程重新布防本地唤醒监听即可。
* **跨设备回声抑制**：服务端会追踪各设备正在播报的回复文本，若某设备在采集窗口内识别出的语音内容与自身或其他设备正在播放的文本高度重合，服务端会自动将其作为回声丢弃（不改变线级协议格式，且不会下发 `turn.accepted`）。

## 3. 标准对话流与服务端主动播报（Announce）

### 3.1 标准多轮对话生命周期

```mermaid
sequenceDiagram
    autonumber
    participant Dev as 设备端 (RodakOS)
    participant Srv as Rodak Realtime Voice 服务端

    Note over Dev: 本地 MultiNet 检测到唤醒词，建立 WebSocket
    Dev->>Srv: session.open (generation, vadStrategies, features, uplink/downlink)
    Srv-->>Dev: session.ready (generation, sessionId, downlink, vadStrategy)
    Dev->>Srv: wake.detected (sessionId, text)
    Dev->>Srv: input.start (sessionId, mode="realtime")
    Dev->>Srv: 上行 RAV1 帧（Opus 16 kHz，60 ms）
    Srv-->>Dev: turn.accepted (sessionId, turnId) [可选特性]
    Srv-->>Dev: turn.progress (stage, label) [草案/可选特性]
    Srv-->>Dev: output.start (sessionId, playbackEpoch, text)
    Srv-->>Dev: RAV1 二进制帧序列 (下行 Opus 音频)
    Srv-->>Dev: output.stop (sessionId, playbackEpoch)
    Note over Dev: 排空本地 TTS 尾音缓冲
    alt 继续多轮对话
        Dev->>Srv: input.start (复用当前 sessionId)
    else 结束对话
        Srv-->>Dev: session.end (sessionId, reason)
        Note over Dev: 关闭连接并重新布防本地唤醒
    end
```

### 3.2 服务端主动播报（Announce，纯下行模式）

由于设备空闲时不保持实时语音 WebSocket 长连接，家庭告警、定时提醒等主动播报场景采用 **MQTT 唤醒 + WebSocket 纯下行播报** 的两段式协同：

```mermaid
sequenceDiagram
    autonumber
    participant MQTT as Rodak MQTT Broker
    participant Dev as 设备端 (RodakOS)
    participant Voice as Rodak Realtime Voice 服务端

    MQTT-->>Dev: commands/{commandNo}: voice.announce {announceSeq, priority}
    Dev->>MQTT: commands/{commandNo}/ack (ok 或 busy)
    Note over Dev: 抢占独占音频焦点（暂停本地音乐），建立 WebSocket
    Dev->>Voice: session.open {features.announce: true, announceSeq}
    Voice-->>Dev: session.ready (sessionId, downlink)
    Voice-->>Dev: output.start + RAV1 下行音频帧
    Voice-->>Dev: output.stop
    Voice-->>Dev: session.end (reason="announce-finished")
    Note over Dev: 释放音频焦点（恢复本地音乐），关闭 WebSocket
```

### 3.3 实现 Announce 模式必须遵守的 5 条硬约束

1. **严格纯下行**：不开启麦克风，**严禁**发送 `wake.detected`、`input.start`、`vad` 或任何上行音频。
2. **无对话状态**：服务端不发送 `turn.accepted`，且该次播报不写入对话历史。
3. **独占音频焦点**：播报开始时抢占独占音频焦点（暂停本地音乐），播报结束后恢复。
4. **30 秒注册窗口与过期处理**：若 `announceSeq` 未知或超过 **30 秒**注册窗口，服务端直接返回 `session.end`（`reason: "announce-expired"`）。
5. **中断与零重连原则**：播报期间若触发本地唤醒或用户手动打断，设备直接关闭 WebSocket 终止播报（不转为普通对话）；**播报会话绝不触发自动重连**。

---

**下一篇推荐**：阅读《实时语音四重代次门控、超时重连与契约测试指南》，掌握防并发乱序的 4 层门控模型、重连退避矩阵、看门狗超时与自动化测试套件。

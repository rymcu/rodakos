# 实时语音四重代次门控、超时重连与契约测试指南

> **参考规范**：本文依据 [rodak-realtime-voice-contract-v1.md](https://github.com/rymcu/rodakos/blob/main/docs/rodak-realtime-voice-contract-v1.md) 整理，重点剖析高并发异步回调下的 4 重陈旧结果隔离机制、重连策略与看门狗超时设计，并提供完整的自动化契约测试与提 PR 自检清单。

> 验收分三层：先验证 Schema／夹具与宿主状态机，再用串口注入验证传输、取消和回合状态，最后在固定的固件及服务端组合上验证真人声学路径。[高音量台架记录](https://github.com/rymcu/rodakos/blob/f4d834d3df8ea03f0a4e914d0e7558b78b3a7fa5/docs/voice-aec-integration.md#high-level-capture-and-delayed-afe-input-power-comparison)中，60／80／100 档无人说话共 14 次未误打断，另有 3 次注入打断成功；不是 14 次真人插话。真机仍要验证唤醒词后的首词、播放尾部插话、下一轮只回复一次及无人说话对照，并记录双方 SHA、包内应用哈希和音量。

## 1. 四重门控：彻底隔离陈旧回调与乱序音频

在嵌入式异步网络与音频任务并行运行的环境中，重连切换或快速打断极易引入“旧连接的回调污染新会话”的幽灵 Bug。协议要求传输层严格实施以下四重门控：

| 门控层级 | 标识字段 | 核心规则 |
| :--- | :--- | :--- |
| **第一重：连接代次** | `generation` | `0` 保留表示未建立；每次建连从非零值严格递增（`UINT32_MAX` 回绕至 `1`）。传输层同时维护交互代次（单次本地唤醒生命周期）与重连策略代次；任何不匹配当前代次的回调或错误直接丢弃。**重连恢复期间**，新代次仅暂存回调，待完整恢复序列全部成功后才正式提交生效。 |
| **第二重：会话标识** | `sessionId` | 握手后所有控制面事件必须精确匹配当前已协商的非空 `sessionId`。 |
| **第三重：播放轮次** | `playbackEpoch` | 每次显式 `output.start` 的 `playbackEpoch` 必须递增，并与对应的 `output.stop` 匹配。**不可回退规则**：一旦本会话观测到显式 `playbackEpoch`，后续任何不带 epoch 的延迟 `output.start` 必须直接拒绝。 |
| **第四重：音频帧序号** | RAV1 `sequence` | 每帧二进制音频的 32 位无符号序列号必须非零且**严格大于**上一帧序列号。 |

## 2. 故障分类与自动重连策略

设备端不得盲目重连，必须先判断**交互阶段**与**故障类型**：

### 2.1 阶段准入门槛

* **交互建立前（不可重连）**：在 `session.ready`、`wake.detected` 与首轮 `input.start` **全部成功之前**发生的任何失败，直接结束本次唤醒交互，**不触发重连**。
* **主动播报模式（不可重连）**：Announce 纯下行播报会话**绝不自动重连**。
* **交互建立后（按条件重连）**：最多重试 **3 次**，采用 **250 ms – 8 s** 退避策略。每次重连必须在新的 `generation` 上完整重跑 `session.open / session.ready → wake.detected → input.start`，全部完成后再提交新代次。

### 2.2 可重试 vs 禁止重试判定表

| 分类 | 具体触发条件 |
| :--- | :--- |
| **允许重连（Retryable）** | 底层网络断开、建连超时、`session.ready` 超时、发送失败、WebSocket 关闭码 `1011` / `1012` / `1013`、服务端下发 `error` 且 `retryable: true`。 |
| **禁止重连（Fatal / Stop）** | 配置或鉴权错误、协议校验错误、WebSocket 关闭码 `1002` / `4001`、收到 `session.end`、用户主动停止（Stop）或模块反初始化（Deinit）。 |

## 3. 端云超时看门狗矩阵

为防止设备在弱网、大模型长思考或服务端异常时无限挂起，双方必须对齐以下超时看门狗：

### 3.1 端侧强制看门狗

| 看门狗项 | 超时阈值 | 触发条件与动作 |
| :--- | :--- | :--- |
| **回复停滞超时** | **20 秒** | 自 `output.start` 或最后一帧下行 RAV1 音频起，20 s 内未收到下一帧或正常结束时，设备终止停滞会话。仅有效音频重置播放停滞计时，进度和 ping/pong 不能替代。 |
| **回合接受后首包超时（服务端）** | **120 秒** | 服务端下发 `turn.accepted` 后，必须在 120 s 内发出 `output.start`、`error`（超时返回 `code: "voice-turn-timeout"`, `retryable: false`）或 `session.end`。 |
| **回合接受后首包超时（设备端）** | **130 秒** | 收到 `turn.accepted` 时**停止 30 s 追问窗口**并启动 130 s 计时器（比服务端多留 10 s 余量，以便优先收到服务端返回的超时 `error`）。 |

### 3.2 服务端分步规划参考超时

| 服务端处理阶段 | 参考超时上限 |
| :--- | :--- |
| ASR 语音识别 | 20 s |
| 意图检查 | 3 s |
| 大模型首块输出 / 块间间隔 | 90 s / 20 s |
| 设备工具执行 / 等待 MQTT 回执 | 15 s / 30 s |
| Web 工具调用 | 8 s |
| 外部 MCP 工具调用 | 30 s |

## 4. 契约校验与多层次测试体系

`rodak-realtime-voice/v1` 的机器可读契约资产位于 `contracts/rodak-realtime-voice/v1/`，包含：
* 每个控制事件的独立 JSON Schema；
* 显式拒绝 XiaoZhi 键值的 `control-message` 联合体 Schema；
* RAV1 帧 Schema 及以十六进制编码存储的二进制帧测试夹具。

### 4.1 自动化主机测试（无硬件运行）

| 测试工具 / 套件 | 验证范围 |
| :--- | :--- |
| `python3 tools/check_aiot_contract.py` | 联合 AIoT 契约与实时语音契约进行跨文件一致性校验。 |
| `tests/app_model/voice_contract_fixture_test.cc` | 向固件解析器灌入服务端事件与 RAV1 夹具，并将设备端生成的 `session.open`、`wake.detected`、`input.start`、`playback.abort` 与标准夹具做逐字节（JSON）比对。 |
| `tests/app_model/realtime_voice_contract_test.cc` | 覆盖描述符校验、VAD 协商、RAV1 解析、代次隔离、`playbackEpoch`、重连策略及播报标志逻辑。 |
| `tests/app_model/voice_assistant_reconnect_coordinator_test.cc` | 专门验证重连协调器的退避、代次提交与取消逻辑。 |

服务端测试套件也会将实际生成的下行控制事件与 RAV1 二进制帧对照 `contracts/rodak-realtime-voice/v1/` 进行双向校验。

### 4.2 真机实测边界（Host Test 无法替代的部分）

上述宿主单元测试**不会**实例化 `VoiceAssistantService` 或 ESP/FreeRTOS 底层传输层。因此涉及以下改动时，必须在 BigSmart 真机上结合仓库文档 `voice-assistant.md` 与 `voice-aec-integration.md` 完成实测验证：
* 主动播报（Announce）状态机与音频焦点抢占/恢复；
* Stop / Deinit 时的异步取消与资源清理；
* 打断或异常结束后的 Opus 解码器与音频管线重置。

## 5. 新人开发者提 PR 前自检清单

| 检查维度 | 必检项 |
| :--- | :--- |
| **服务发现与安全** | `endpoint` 是否拒绝了 query、fragment、userinfo 与 `/xiaozhi`？Pin 模式是否校验 `wss://` 与固定 TLS 域名后再发送 Bearer Token？是否做到零日志打印 Token 与 `Authorization` 头？ |
| **历史隔离** | 解析器是否拒绝 `hello`、`listen`、`tts`、`goodbye`、`type`、`session_id`？是否未复用历史 `xiaozhiSessionMachine`？ |
| **控制面防御** | 入站控制 JSON 是否在解析前限制了最大 12 层嵌套与 `maxControlBytes`（固件硬限 64 KiB）？重复或延迟的 `session.ready` 是否按错误处理？`turn.progress.label` 是否确保 ≤32 码点且不含敏感原话/工具名？ |
| **音频与 VAD** | RAV1 是否校验魔数、非零严格递增序列号、精确载荷长度与 `maxAudioFrameBytes`？`server-authoritative` 下是否抑制发送 `vad` 事件？ |
| **门控与重连** | 是否严格执行 `generation`、`sessionId`、`playbackEpoch`、RAV1 序号四重门控？交互建立前失败及 Announce 会话是否确保不重连？`1002`/`4001` 是否禁止重试？ |
| **超时与测试** | 是否实现 20 s 帧停滞超时与 130 s（服务端 120 s）首包超时？是否全部通过 `check_aiot_contract.py` 与三套 `tests/app_model/` 实时语音宿主测试？ |

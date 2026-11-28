# MQTT 进阶语义：指令去重账本、音量/灯光 Effect 回执、凭据无重启替换与语音身份

> **参考规范**：本文基于仓库内的 `docs/mqtt-command-semantics.md`、`docs/mqtt-volume-effects.md`、`docs/mqtt-light-effects.md`、`docs/mqtt-credential-refresh.md` 与 `docs/voice-identity-wake-word.md` 整理，衔接 [rodak-aiot-contract-v1.md](https://github.com/rymcu/rodakos/blob/main/docs/rodak-aiot-contract-v1.md)，面向需要深入实现或排障 `UnifiedMqttService` 与服务端协同逻辑的开发者。消息中心（§7.6 草案：`inbox/changed` 无正文脏信号 + HTTPS 幂等 `opId`）详见[第 03 篇](./03_rodak_aiot_v1_mqtt_commands_shadow_and_voice.md)。

## 1. 指令去重账本、精确推流 Stop 与断连回执边界

在真实弱网与重连环境中，同一条 MQTT 指令可能被重复投递，旧连接的异步回调也可能在新连接上迟到。`docs/mqtt-command-semantics.md` 定义了以下三道防线：

### 1.1 易失性 64 条指令重放账本（Replay Ledger）

固件在同一 `Authority`（签发当前凭据的服务端身份）内维护最近 **64 条**已准入 `commandNo` 的内存账本：

* **精确重放**：按原始载荷字节计算 **SHA-256**；字节完全一致时直接重放首次终态 ACK，**不重入业务处理器**。
* **冲突拦截**：同一 `commandNo` 但原始字节不同（哪怕只是 JSON 空格变化）直接返回 `command_conflict`；若原请求仍在执行中（in-flight），重复到达的报文静默忽略。
* **容量与墓碑（Tombstone）**：`commandNo ≤ 128 B`，输入载荷 `≤ 256 KiB`，缓存的 ACK 响应体共享 **64 KiB** 预算。若某条响应体超出缓存预算，账本仅保留其 ID 作为墓碑，后续重放返回 `command_result_unavailable`（不重新执行）。满载时按首次准入顺序淘汰最早的已完成记录（不淘汰 in-flight）。
* **生命周期**：同设备重启前，普通重连、Token 刷新与同对象 Stop/Start 均保留账本；更换 Authority 清空账本；设备重启后不提供跨启动 exactly-once 保证。

### 1.2 精确推流 Stop（026）与流租约（Stream Lease）

每个流会话通过四元组 **Stream Lease**（`generation`、`connection epoch`、永不复用的 `instance nonce`、`sessionId`）绑定，避免同名 `sessionId` 的新旧会话互相误杀：

* **精确 Stop**：`display.stream.stop` / `camera.stream.stop` 携带首次 Start 的 `startCommandNo`（非空，≤ 128 B），成功返回 `result: {sessionId, startCommandNo, stopOutcome}`：
  * `stopOutcome: "stopped"`：本次请求撤销了匹配的活跃实例且原生 Stop 已返回。
  * `stopOutcome: "already_stopped"`：同 `generation` / `epoch` / `Authority` 下，最新一次成功 Start 的实例已完成原生清理（依赖每类流仅保留 1 份的易失性 `completed-cleanup proof`）。
* **边带回调与有界发送队列**：摄像头/屏幕的 `signal`/`state` 异步回调复用 Start 指令的 `/ack` Topic（位于 `result.cameraStream` / `result.displayStream`），服务端将其视为边带消息，不会覆盖首条终态 ACK。固件结果队列上限为 **8 条、单条 ≤ 64 KiB、总计 ≤ 128 KiB、Topic ≤ 512 B**，通过 SDK `MQTT_USER_EVENT` 在锁外直接以 QoS 0（`retain=false`）发送，切换 `epoch` 时自动丢弃过期回调。

## 2. 音量与灯光 Effect 协议及 L3 软件回执

RodakOS 已经移除会话级语音音量 MCP 工具，**音量控制与板载 RGB 灯光控制均走 Shadow 单次派发 + 独立回执通道**，不走 `commands/+`。

### 2.1 单次派发格式（Non-retained `shadow/desired`）

服务端在一次**非保留（non-retained）**的 `devices/{deviceKey}/shadow/desired` 发布中附加 `_meta["rodak/deviceEffect"]`（不落盘进持久化 desired，避免重连同步时误重放）：

* **强一致性校验**：外层 `deviceKey` 必须匹配当前身份；`shadowVersion` 与可选 `version` 必须一致；`desired` / `state.desired` 中的目标字段必须与 `_meta` 中的 `requested` 与 `shadowVersion` 完全一致。
* **域隔离与零降级**：一条报文只能触发一种 Effect（灯光帧绝不顺带应用旧的 desired 音量，反之亦然）；一旦携带了已知 `_meta` 但校验失败，**绝不降级**走旧版无元数据 setter。

### 2.2 独立回执通道（`devices/{deviceKey}/effects/receipt`）

设备以 **QoS 0、retain=false** 发布软件回执（普通 `shadow/report` 永远不包含 `effectId`）。回执中明确声明 `persistence: "volatile"` 与 `physicalVerified: false`（仅代表驱动/Codec API 受理，不宣称 I2C 回读或物理声光实测）。若在执行前被协议层拒绝，则只返回互斥的外层 `errorCode`（绝不同时包含 `receipt` 与 `errorCode`）。

### 2.3 音量 Effect vs 灯光 Effect 关键差异对比

| 对比项 | 音量 Effect（`mqtt-volume-effects.md`） | 灯光 Effect（`mqtt-light-effects.md`） |
| :--- | :--- | :--- |
| **请求 / 回执 Schema** | `rodak.mqtt-volume-effect.v1` / `rodakos.mqtt-volume-result.v1` | `rodak.mqtt-light-effect.v1` / `rodakos.mqtt-light-result.v1` |
| **控制范围** | 绝对音量 `0–100`（相对调节由服务端基于 report 换算） | Board Manager 发现的 RGB 灯（如 `board_rgb`），不含 LCD 背光；支持 `enabled`、`brightness`（0–100）、`color`（完整 `r/g/b`） |
| **`effectId` 格式** | 1–128 字符 `[A-Za-z0-9:_-]` | 固定 `light:<shadowVersion>:<UUID>`（防淘汰后篡改版本） |
| **原生执行结果状态** | `deferred`（Codec 关闭）/ `codec-applied` / `rejected`（`codec-volume-rejected`） | `driver-applied` / `unverified`（`light-driver-rejected`，置 `available=false`） |
| **账本容量与淘汰策略** | 最多 **64 条，不淘汰**，满载返回 `effect-capacity` | 保留最近 **64 条，按最旧淘汰**，结合全局与单灯水位线防回退 |
| **执行前拒绝 `errorCode`** | `stale-shadow`、`effect-conflict`、`effect-capacity`、`invalid-request`、`outcome-unknown` | `stale-shadow`、`effect-conflict`、`invalid-request`、`outcome-unknown` |

## 3. 同 Authority 凭据刷新与无重启 SDK 客户端替换（025）

为解决旧 ESP-MQTT 句柄热更新凭据时的状态残留问题，025 规范要求在**同一 Authority 下始终完整替换整个 SDK Client**（而不是在旧句柄上调用 `set_config + reconnect`），从而在**不重启整机**的前提下平滑恢复连接。

### 3.1 免重启边界：`HasSameEffectAuthority()` 与 `GOT_IP` 迁址例外

* **常规条件**：固定服务器信任、连接地址、Broker/端口、设备/用户名、所有 Topic、Provisioning URL、绑定 Secret 与绑定状态全部一致（仅密码 / `accessToken` 轮换）。
* **受限 `GOT_IP` 例外**：当 STA 的 IP/子网掩码/网关三元组变化触发网络版本递增时，若设备处于已固定证书（Pin）模式，经真实 `DeviceCloud` TLS 校验与原凭据认证后，**允许仅变更数值连接地址**而免重启替换客户端；若证书、端口、Topic 或 Authority 发生变化，仍维持整机重启隔离。

### 3.2 替换流程与严苛锁序

1. **摘除旧实例**：在短临界区（`client_api_mutex_ → mqtt_mutex_`）摘下旧实例唯一 owner，推进 `generation`/`epoch`，清空半包、发布队列与流租约。
2. **锁外清理与停止**：释放两锁后清理已撤销的媒体流，再**仅持 `client_api_mutex_`** 停止并销毁旧 SDK（绝不持 `mqtt_mutex_` 等待 SDK 回调，防止与 ESP-MQTT 内部递归锁形成死锁）。
3. **精确快照附着**：新建空 `DeviceCloudConfig` 构造并注册候选 SDK，最后调用 `ApplyIfMqttConfigCurrent()`（锁序 `client_api_mutex_ → DeviceCloud config_mutex_ → mqtt_mutex_`）在配置锁内做最终精确比对与附着，锁外再 `start` SDK 任务。
4. **失败底线**：
   * 若候选创建或附着失败，释放未启动候选并按 **2 s → 60 s** 退避重读配置（不重复已成功的 HTTP 刷新）。
   * 若旧 SDK `stop` **未确认成功**，严禁销毁可能仍在运行的句柄或新建客户端，必须保留隔离 owner 并执行 **fail-safe 整机重启**。

## 4. Shadow 语音身份与动态唤醒词同步（`voice_identity`）

`voice_identity` 通过设备影子动态同步设备称呼与本地唤醒词（初始身份 `罗达克（Rodak）`，默认展示唤醒词 `你好达克`）：

* **ESP-SR 运行时边界**：固件加载 `espressif/esp-sr` 2.2.2 与中文 `mn5q8_cn` MultiNet 模型，通过 `esp_mn_commands_add()` + `esp_mn_commands_update()` 在运行时更新拼音指令（因此展示词 `wakeWord` 与拼音指令 `wakeCommand` 拆分为两个字段；当前基线阈值从 `0.20` 调至 `0.14` 以提升远场召回，需权衡误唤醒）。
* **Desired 契约**：`name` 与 `wakeWord` 各限 **32 字节 UTF-8**，`wakeCommand` 限 **128 字节**，拒绝控制字符；`revision` 为正 `uint32`；`mode: "temporary"` 必须携带正整数 Unix 毫秒时间戳 `expiresAtMs`。
* **Reported 与防误注入**：上报对象包含 `revisionWatermark`、`activeConfirmed`、`status`、`runtime`、`model` 与可选 `error`。只有 `applied` / `expired` 代表配置与存储均已确认；处于 `pending`、`pending_clock`、`recovery_failed` 时，`activeConfirmed` 不成立，**服务端绝不可将未确认候选注入 Base System Prompt**。
* **存储与时钟保护**：NVS `voice_wake/identity` 以单条 schema-1 JSON 原子保存 `persistent`、`active`、`lastAccepted`（保存后强制新句柄回读校验；结果不确定时冻结更新并停用身份驱动监听）。临时身份过期采用 2020 年时钟合理性底线，并用单调 uptime 计算剩余寿命（防止系统时钟回拨延长过期时间）；重启后时钟尚未同步时先回退至 `persistent` 并上报 `pending_clock`。

## 5. 对应源码与主机测试套件

| 能力模块 | 核心固件源码（`main/phone_os/`） | 主机测试套件（`tests/`） |
| :--- | :--- | :--- |
| **指令账本与精确 Stop** | `mqtt_command_ledger.*`、`unified_mqtt_service.*`、`stream_lease.h` | `tests/mqtt_volume_service/mqtt_command_*`、`mqtt_stream_lifecycle_test.cc` |
| **音量 / 灯光 Effect** | `mqtt_volume_effect.*`、`mqtt_light_effect.*`、`light_service.*` | `tests/app_model/mqtt_volume_effect_test.cc`、`tests/light_service/`、`tests/mqtt_volume_service/` |
| **凭据无重启替换与迁址** | `mqtt_credential_refresh_policy.*`、`cloud_credential_freshness.h`、`cloud_credential_renewal.h` | `tests/mqtt_volume_service/mqtt_credential_replacement_test.cc`、`mqtt_network_route_test.cc`、`tests/mqtt_cloud_integration/` |
| **Shadow 语音身份** | `voice_identity.*`、`voice_identity_record.cc`、`voice_wake_service.*` | `tests/app_model/voice_identity*`、`tests/voice_identity_integration/`、`tests/voice_wake_service/` |

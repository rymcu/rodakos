# MQTT 凭据刷新与客户端替换

025 将同一授权身份下的自动 MQTT 凭据刷新改为完整替换 SDK client。正常路径先撤销旧连接的应用准入，停止并销毁旧 SDK client，再使用当前已持久化凭据建立新 client；设备服务和整机继续运行。

本文说明实现合同和验收方法。025 具名包已通过有效 token 短停、实际认证拒绝后的
generation 1→3 替换、65 秒服务中断和后续屏幕观察；精确包、窗口及限制见
[025 发布证据](ota-release-readiness.md#2026-10-07-mqtt-credential-client-replacement-025)。
本地软件与硬件结果不代替当前 main/PR 的独立 CI 工件核验。

## 适用范围

替换路径要求 `HasSameEffectAuthority()` 成立。它比较固定服务器信任、连接地址、broker/端口、设备与用户名、各 MQTT topic、provisioning URL、绑定 secret 和绑定状态等字段。正常密码或访问令牌更新不改变该去重域。

这是一项保守的范围判断。`tokenVersion` 相同本身不等于全部 authority 相同；同一笔记本或同一设备也不能替代上述字段检查。authority、路由或显式 USB provisioning 变化继续使用既有隔离策略，可能重启设备。025 不承诺所有换网或配置变更都免重启。

同 authority 的自动刷新始终替换整个 SDK client，包括旧 client 已连接、正在重连、已断开以及 outbox 非空的情况。实现不再通过旧 handle 的 `set_config + reconnect` 应用新凭据。

## 正常替换流程

1. SDK callback 使用注册时固定的 `ClientInstance` 和 generation。认证拒绝待办记录原 generation，不能在稍后处理时重新归入当时的全局 generation。
2. worker 执行已有的配置刷新流程。HTTP 期间旧 SDK 可能继续产生认证拒绝或自动恢复连接；这些事件仍属于旧实例。
3. 刷新成功后读取新的空配置快照，检查 MQTT 配置、trust、解绑状态和 authority。语音会话在延期检查点处于 active 时保留已完成的刷新结果，稍后重新读取当前配置，不为同一次已成功的恢复重复 HTTP 刷新。
4. 在短状态临界区摘下旧实例的唯一 owner，标记离线、撤销 effect/stream 准入并推进 generation/epoch。清除旧分片、发布队列、CONNECT 待办和可靠发送状态，唤醒等待 PUBACK 的调用者。旧 generation 的认证待办在此合并。
5. 释放状态锁与客户端 API 锁后清理已撤销的媒体流；随后仅持客户端 API 锁停止旧 SDK。只有停止成功并完成销毁后，才允许构造下一 SDK client。
6. 再次读取当前已持久化配置，创建候选 SDK client 并注册它自己的 callback context。通过 DeviceCloud 的精确凭据检查后附着新实例，随后启动 SDK task。
7. 新连接重新订阅并发布当前状态。新 generation 的认证拒绝保留为新待办，不能被上一轮完成动作清除。

同 authority 的 command、volume、light 已执行去重记录继续保留。旧连接的 outbox、未发送结果、半包、流 lease 和 PUBACK 等待不迁入新实例。去重记录仍受原容量和同一 boot 边界约束，不构成跨设备重启的执行保证。

## 配置快照与附着时点

`DeviceCloudConfigService::Load()` 的布尔返回值表示 AIoT 身份是否完整，并非通用的“存储读取成功”。合法的旧版 MQTT-only 配置可以返回 `false`，同时具有可用的 MQTT 配置。

服务每次读取都创建空 `DeviceCloudConfig`，避免复用先前快照的字段。它依据 `has_mqtt_config`、trust 错误/待确认状态、解绑状态和 broker/TLS 名称等判断可用性；要求绑定身份却没有完整 AIoT 身份的快照不能上线。分配失败不会把旧快照冒充为新读取结果。

`cloud_generation` 也不能单独证明凭据当前有效：同 authority 的语音刷新可以轮换令牌而不增加它。最终附着调用 `ApplyIfMqttConfigCurrent()`，在 DeviceCloud 配置锁内重新读取并精确比较当前 credential、authority、路由及 MQTT material。

该检查中的回调只负责附着实例、提交其配置和准入状态；这是新客户端被接受的确定时点。SDK `init/register` 在检查之前，`start` 在检查之后，均不在该配置锁回调中执行。若语音刷新已在候选初始化期间更新令牌，旧候选不能附着，且未启动的候选会被销毁。附着之后的新一轮凭据变化属于后续事件，仍由原代次事件与恢复流程处理。

语音延期是各检查点的协作式判断，不是对任意时刻语音状态变化的硬实时排他保证。

## 锁序与对象寿命

| 路径 | 持锁关系与约束 |
| --- | --- |
| 候选附着 | `client_api_mutex_ → DeviceCloud config_mutex_ → mqtt_mutex_`；配置锁回调只附着，不调用 SDK、HTTP 或媒体清理 |
| SDK callback | SDK API 锁下进入短 `mqtt_mutex_`；不取得应用 `client_api_mutex_` 或 DeviceCloud 配置锁 |
| worker 退休旧实例 | `client_api_mutex_ → mqtt_mutex_` 摘 owner 并撤销准入，随后释放两锁 |
| 媒体流清理 | 不持 `client_api_mutex_` 或 `mqtt_mutex_` 等待 peer 回调退出 |
| SDK stop/destroy | 仅持 `client_api_mutex_`；不能持 `mqtt_mutex_` 等 SDK callback |
| 外部 `Stop()` | 持 service lifecycle 锁；先短状态锁摘 owner，锁外清理媒体和 SDK，最后等待 worker 结束 |

worker 不调用整个 service `Stop()`，也不取得 `lifecycle_mutex_`，避免与外部 `Stop()` 等待 worker 的过程互相等待。

当前实例被摘下后只有一个销毁 owner。外部 Stop 可以取消尚在构造或启动中的候选，但 SDK 操作由同一 API 锁串行化，双方不能重复销毁。每个实例独立持有 callback context、generation、TLS certificate/common-name、URI 和 client ID 存储，直到其 SDK 生命周期结束；不会提前复写旧 SDK 借用的字符串。

## 失败边界

| 情况 | 处理与限界 |
| --- | --- |
| HTTP 刷新失败 | 保留失败结果，不宣称凭据已恢复；后续请求继续遵循既有认证/传输恢复策略 |
| HTTP 成功后的配置快照分配失败 | 延后重试；已成功的 HTTP 结果保持为待应用状态，不用旧字段伪造新读取。HTTP 前分配失败则结束本次请求，由既有认证/传输恢复策略再次触发 |
| 刷新后的持久配置不可用 | 撤销准入并隔离旧 client，保持离线；不把损坏 trust 或解绑状态转换为新的自动信任 |
| 候选 `init/register/start` 失败，或精确快照检查拒绝附着 | 清理未运行的候选，保持离线，按 2 秒递增至最多 60 秒的间隔重新读取当前配置；不因此重复已经成功的 HTTP 刷新 |
| authority 改变 | 保留既有重启隔离；不把它算作同 authority 免重启路径 |
| SDK stop 未确认成功 | 保留旧实例的唯一 owner 和所有借用存储，进入终态失败隔离，并使用异常 fail-safe 整机重启；禁止销毁仍可能运行的 client 或建立新 client |

SDK `start` 返回成功只证明任务创建请求成功，不能证明任务已经进入运行态。SDK `stop` 返回失败也不能证明任务已退出：可能处于尚未开始运行或退出清理尚未完成的窗口。因此不能用 `run=false`、generation 变化或后续 `destroy()` 代替明确的停止确认。

真实 `esp_restart()` 不返回。测试替身若让它返回，隔离 owner 仍保留在 service 内，worker 不再建立 client；故障测试必须先释放受控退出窗口并等待模拟 SDK 任务结束，再销毁 service。这个测试清理步骤不代表生产代码已成功停止旧 SDK，异常重启用例也不计入免重启成功。

SDK stop 内部等待、网络/TLS 调用和媒体 peer 清理仍有各自阻塞边界。客户端替换没有给这些调用增加硬取消保证。收到 SDK 停止确认仅说明最后一次 client 访问和相关清理已结束，不证明 FreeRTOS idle 已立即回收任务 TCB/stack，也不证明全部堆资源归还。

## 025 软件验证

源码 `8d5cf99f95b59d45b0a1e66fc3a02d1a502618a6` 的 MQTT 完整服务回归在
Debug 与 ASan/UBSan/leak 各通过 133 个正常案例，其中客户端替换为 18 个；
四个完整服务负变体均命中指定断言。ASan 的七个 CTest 还包含六个已有诊断负变体。
共享语音身份服务在两个构建中各通过四个案例。

真实 DeviceCloud 合同在两个构建中各通过 44 个案例；同时链接真实 MQTT 与
DeviceCloud 的组合目标各通过六个案例及两个完整 Cloud 负变体。旧版完整生产源码
在新生命周期与组合回归中按预期失败，未将编译错误、超时或 sanitizer 异常当作成功检出。
SDK overlay 的七个函数级场景及八个生成器检查另外验证，其生成源码与固件实际输入相同；
这仍不等于在 host 执行完整 SDK 或验证实际网络。

测试与边界见 [MQTT suite](../tests/mqtt_volume_service/README.md) 和
[Cloud 组合 suite](../tests/mqtt_cloud_integration/README.md)。完整仓库回归、固件和设备
窗口分别记录，不能由这些定向案例代替。

## 实机验收方法

使用具名的 025 源码、构建和签名包记录进行验证，保持同一台已绑定设备与同一服务器 authority。

- 刷写和测试前保存设备 ID、绑定状态、`tokenVersion`、MQTT 在线状态以及可关联的设备 uptime；凭据内容不写入日志。
- 分别测试有效凭据期间的短暂服务器中断，以及凭据确实需要恢复后的认证拒绝路径。刚刷完立即重连可能没有触发凭据刷新，不能代替后者。
- 用串口生命周期标记关联 `auth refresh requested by generation`、旧 generation 的 `retired`、`stop/destroy confirmed`、新 generation 的 `attached` 与 `connected`。同时记录旧请求合并，以及属于新 generation 的后续拒绝是否仍被处理。
- 核验新连接产生 fresh shadow/telemetry，设备 ID、原绑定和 `tokenVersion` 保持；以连续递增的 uptime 和无新启动标记证明正常窗口没有整机重启。只看到 MQTT `connected` 不足以证明替换或免重启成立。
- 保留外部 Stop、语音延期、旧 outbox/结果隔离、SDK 创建失败及正常恢复的独立软件证据。SDK stop 未确认的异常 fail-safe 窗口单独记录，不能合并进正常成功窗口。
- 结束时记录 MQTT/语音/媒体状态、串口释放情况和观测时长。短时恢复不替代声学、Camera/媒体资源归还、真实存储故障或八小时稳定性门禁。

025 已取得上述有限实机恢复证据。更广语音并发、撤权、异常 SDK 停止及资源门禁仍需
独立验收；024 的主动隔离重启历史继续保留，不能追溯改写为已通过。

相关合同：[MQTT volume effects](mqtt-volume-effects.md)、[命令结果与重放边界](rodak-aiot-contract-v1.md#command-results-and-replay-boundary)、[语音与 Device Cloud 恢复](voice-assistant.md)、[可信服务器与迁址](trusted-server-discovery.md)、[发布验收](ota-release-readiness.md)。

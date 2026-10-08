# 语音任务回收合同与分层验证

更新：2026-10-08。031 把 Assistant I/O、前端采集和唤醒 supervisor 的三个 WithCaps
任务接入 [030 共享回收器](task-retirement.md)。032 保持这三条服务及回收器源码不变，新增
测试专用 USB lifecycle cycle；软件、测试包和普通 OFF 包分别验证。独审确认测试镜像下
一次 idle、三次 Listening 的有限任务退出与恢复观察。随后恢复普通 OFF 包，完成受保护
刷写、Recovery/main/OTA/Home 启动及一次会话 stop/rearm；完整物理资源和生产门禁仍开放。

031 源码提交：[`7ad01b7452102fd1a6a2f64d4102ae42031509b2`](https://github.com/rymcu/rodakos/commit/7ad01b7452102fd1a6a2f64d4102ae42031509b2)。

032 源码提交：[`514ebb8c47b0409e4bc1ac6dff95e57af6cc615e`](https://github.com/rymcu/rodakos/commit/514ebb8c47b0409e4bc1ac6dff95e57af6cc615e)。
测试包 `20261008-080207` 与普通 OFF 包 `20261008-081054` 共享该源码，具有不同 Main/ELF
和构建开关。制品、部署与硬件结果见 [032 readiness](ota-release-readiness.md#2026-10-08-voice-lifecycle-diagnostic-and-restoration-032)。
030 的五条视频路径、7 条关联停止、359 B 同 boot internal 最低值和 NO_GO 保持原记录；
不把它们改写成 032 的资源观测。

033 源码 `78917fae1b9acb02010cef026facefc96a008c5c` 在保留退出机制的基础上修正模型
清理、AFE 取消分类并缩减凭据刷新栈帧。033结束时普通 OFF 包为 `20261008-092316`，当时已恢复
启动；有限任务/自然刷新与普通 stop/rearm 已封存独审，见[033 证据](ota-release-readiness.md#2026-10-08-voice-health-and-credential-refresh-033)。

034 已实现完整AFE输出帧门控、暖机/停滞诊断、有限自动恢复和phase快照，软件与TEST112651制品独审通过；TEST一格idle、三格Listening及同第三会话自然TTL600刷新已闭合独审；普通OFF包20261008-113338已恢复并完成独立cold与stop/rearm。TEST generation15仍有一次running stall待定位。033及更早历史保持原身份，资源/生产NO_GO不变。

## 修复的退出边界

原先三个业务函数先清空活动 handle，再调用 `vTaskDeleteWithCaps(nullptr)`。该版本
IDF 的自删除仍需创建清理任务；低内存时可能因创建失败而 abort。清空 handle 不表示
任务栈已释放，也不保证 C++ 局部对象执行析构。CaptureTask 的函数级
`std::vector<int16_t> afe_feed_buffer` 因此不能依赖 self-delete 释放容量。

| 服务 | 回收任务 | 保持的栈与调度设置 |
| --- | --- | --- |
| `VoiceAssistantService` | `assistant_io` | 49152 B PSRAM，优先级 4，双核时 core 1 |
| `VoiceAudioFrontend` | `voice_frontend` | 8192 B PSRAM，优先级 4，双核时 core 0 |
| `VoiceWakeService` | `voice_wake` | 4096 B PSRAM，优先级 2，双核时 core 0 |

每个服务持有自己的 owner 与最近一代 ticket。创建前保留记录，保存 ticket，再通过共享
入口创建任务并发布 handle；创建失败取消新记录并恢复上一张 ticket。业务 handle 只表达
活动状态，保留 ticket 不表示任务仍在工作。

停止在服务锁内捕获精确票据，锁外等待。共享入口等完整业务函数及局部析构返回后才发布
finished，再由外部 Stop 或常驻 Pump 调用真实 IDF WithCaps 删除路径。外部删除等待跨核
收敛，删除任务并释放 TCB/栈；不会在退出时申请新回收记录或创建临时清理任务。

普通 Deinit 保留重新 Init 的能力。只有析构执行 `Close → Deinit → Drain`，等所有所属代
回收后才销毁服务锁和状态；不能把永久关闭 owner 的 Drain 放进每次普通 Deinit。
自身任务调用 Stop 不等待自己，析构自身 owner 则属于无效生命周期。

## 服务特有顺序

Assistant 先停止录音输入，关闭 transport 并等待音频通道关闭，再 Join 原代 I/O；该顺序
允许阻塞在重连 OpenAudioChannel 的任务退出。并发 Stop 的早退路径也等待捕获的 ticket，
cleanup 与 Deinit 等待固定在各自操作代号，不追等随后启动的会话。业务会话 generation
与任务回收 generation 是两种不同的标识。

Frontend 的普通 Stop/StopListening 只停止录音或监听模式，常驻采集任务在 Deinit 才退出。
CaptureTask 正常返回，使 `afe_feed_buffer` 自然析构；Deinit 等这次物理任务回收完成后
才释放模型。等待通知 callback 结束仍在 lifecycle 锁外，允许 callback 重入 Stop/Deinit。

AFE 保持原顺序：停止新 feed，等待正在执行的 feed 返回且 fetch 继续排空，随后停止 fetch，
从外部删除 fetch 任务，最后 destroy AFE。`afe_fetch` 已使用外部 WithCaps 删除，不属于
本次三条 self-delete 迁移。`wake_notify` 保持普通 internal 栈任务，其 callback 可能访问
NVS，不迁至 PSRAM；本切片不扩展其 callback 局部析构或任意异常的保证。

Wake 的 SetEnabled(false) 保留 supervisor，以便禁用时继续处理身份到期；Stop/Deinit
才请求 supervisor 退出。并发 Stop/Deinit 可升级正在执行的 Disable，由同一 stop epoch
完成清理并发布完成代号；外部等待者先 Join 原 ticket，再等待该 epoch。自身 Stop 及同一
清理任务上的 Assistant 同步回调不等待自己，旧任务逻辑退出前也不能被 Start 复活。

上述规则不承诺任意用户回调可重入：Wake 仍在普通 mutex 内调用部分 runtime、clock 和
GetState 路径；Assistant 同任务递归 Deinit 也不属于本切片支持的调用合同。

## 软件证据

| 验证目标 | 已记录结果 | 主要边界 |
| --- | --- | --- |
| [Assistant 实际服务](../tests/voice_volume_service/README.md) | Debug 24、ASan/UBSan/leak 24；6 个完整 TU 变异负控及旧完整源/头红例 | I/O 尾部窗口、并发停止、原代等待、Deinit 重启及代号替换；transport/codec 仍为替身 |
| [Frontend 实际服务](../tests/voice_audio_frontend_identity/README.md) | Debug 13、ASan/UBSan/leak 13；旧完整源/头与跳过 vector 析构两个负控 | 实际 CaptureTask 至少一次 MR feed，观察真实 vector 分配在任务删除前释放；覆盖 callback 重入与 lifecycle 锁顺序 |
| [Wake 实际服务](../tests/voice_wake_service/README.md) | Debug 35、ASan/UBSan/leak 35；旧完整源/头红例 | Stop/Disable/Deinit 合并、旧代等待、自身停止、失败创建与身份到期 |
| [身份集成](../tests/voice_identity_integration/README.md) | Debug 4、ASan/UBSan/leak 4 | MQTT/身份链连接真实 Wake 服务；硬件、音频和 NVS 为替身 |
| [共享回收器](../tests/task_retirement/README.md) | Debug 14 CTest、ASan/UBSan/leak 13 CTest | 新增普通任务通知/外删宿主能力；原 6 个完整 TU 变异未另跑 sanitizer |
| 共享 fixture 兼容检查 | Camera 7、Display 1、Peer 1 CTest；MQTT 默认分支 5 CTest | 兼容回归保持原场景范围，MQTT 既有负控未重跑 |

测试直接编译完整生产 TU 和固定 ESP-IDF 6.0.2 的 WithCaps 函数链，替换调度器、核查询、
底层分配与硬件依赖。host 外部删除实际 join worker 后才允许取出并释放模拟 TCB/stack。
普通通知任务也运行真实宿主线程，不再使用“创建即悬停”的空对象。

Frontend 在 AFE feed 记录局部 vector 的实际地址，通过链接器包装 C++ delete 观察释放，
仍调用原始 delete。旧源码及配套头文件都冻结到 `34c9e645`；legacy 宏只隔离依赖新字段的
绿色测试，不改变旧业务体。红例必须在真实 feed 后出现指定 cleanup-task 创建拒绝双标记
及 SIGABRT，且不得出现 vector 释放或无关线程析构终止标记。跳过 vector 析构的变体则必须
精确触发“任务删除前缓冲区已释放”断言。超时、编译失败及任意非零退出均不算检出。

031 当时本地 ESP-IDF 6.0.2 构建的主应用为 **7,148,928 B**，SHA-256
`cd3942e01f201569534a0deab7ed0b86680ce1094894986fa0903dd832fd5c07`。
编译前后核对的生产输入未变化；这仅是本地构建产物，不是签名包、已安装固件或新设备证据。

## 032 诊断与普通固件的边界

`RODAK_RELEASE_TEST_V1 voice_cycle <id>` 仅在 `RODAKOS_RELEASE_TESTS=ON` 时存在。单槽
保留 accepting/pending/executing，请求 ID 每 boot 严格递增且只有 accepted 消耗；现有 main
internal 栈任务执行一次 Wake.Deinit 与一次恢复 Wake.Start，不新增 worker、不改 enabled
偏好。after 仅读取三个任务名和不会 Init 的 Assistant 快照，不调用 Wake.GetState。

三任务结果要求 before 为 enabled、Listening、非 stopping 且 assistant_io、voice_frontend、
voice_wake 均存在；after 三者缺席；恢复后 assistant 缺席、capture/supervisor 存在、监听
恢复且 enabled 前后一致。idle/disabled-idle 单独分类，不能代替三任务验收。忙态门禁是
两次快照及串口变更阻断，不是 UI/MQTT 全局互斥，也不覆盖 Music library scan。

[诊断宿主目标](../tests/voice_lifecycle_diagnostic/README.md) 的 14 个用例在 Debug 与
ASan/UBSan/leak 通过，另实际检查 JSON 输出和 OFF TU 空符号。普通包最终 ELF、bin、
main/serial 对象不含诊断/fault marker 或 hook，新 TU 不在编译数据库和 map；两种包的
bootloader、分区表、otadata、Recovery、公钥及 merged 的非 Main 区域与 030 一致。

矩阵请求 3201（idle）与 3202–3204（Listening）分别得到完整结果链，complete 后到下一次
TX 前的实际 RX 跨度为 73.625 / 92.265 / 199.890 / 85.828 s。测试包保留四条 MultiNet
清理错误、两条 MQTT fragment、三条 AFE empty 和两条 fetch rejected 警告；通知任务
栈最低采样为 508 B，不能宣称零错误或资源余量充分。

普通 OFF 包恢复后，独立记录 151.125 s cold RX 跨度及一次合成静音会话，实际 stopped 后
覆盖 73.203 s、wake rearm 后覆盖 72.656 s；保留 AFE empty/fetch 各一条警告及启动诊断。
测试 flavor 的实际 Deinit 结果只归该测试镜像；普通 OFF 包启动与会话检查不等于执行不存在
的诊断入口。普通串口 wake/stop 也不等同于常驻 Capture/Wake Deinit。
最终身份和证据摘要以 [032 readiness](ota-release-readiness.md#2026-10-08-voice-lifecycle-diagnostic-and-restoration-032) 为准。

## 033 模型所有权、取消分类与栈边界

固定 ESP-SR 2.2.2 的 `mn5q8_cn` create/destroy 拥有全局命令表，Frontend 不再额外
alloc/free，创建前拒绝未审模型。真实 SDK 注册表 host 用例和模型库反汇编共同限定此合同；
不覆盖 SDK 任意 OOM 或其他模型，详见[测试说明](../tests/voice_audio_frontend_identity/README.md)。

AFE fetch 返回后在锁内复核 mode/generation/stopping。当前会话的失败保留原 WARN 并标记
PCM/AEC 不连续；已取消结果不计为 current failure，但在原 feed 仍需排空时继续 fetch，
只有 stopping 置位才能退出。有效 PCM 保留下游二次代次检查。退出摘要分列当前失败与取消，
不删除 SDK empty 警告、不提高 100ms 参数，也不改外部删除/destroy 次序。
[五项真实 TU 用例与三类精确负控](../tests/voice_audio_frontend_identity/afe-lifecycle.md)
覆盖取消失败/有效帧、新代交付、活跃 gap、背压排空和诊断输入 EOF 后的持续补零。

RefreshAiot 使用顺序阶段与更短 NVS key 临时生命周期，保留 TLS、generation 与事务/回滚。
两包最终 ELF 已确认应用层子链帧减少，但 6,144 B internal wake_notify 栈不变；其
stack_min_free 是任务 lifetime 历史水位，不是当前 SP，frontend TAG 也不是采集线程名。
硬件水位与自然刷新会话必须独立记录；不把静态帧差额加到 032 的 508 B。

## 034 输出就绪与恢复边界

034源码 `66ab25cd7d1b2af8aa0fa1de8d4012fbc3d51781` 按固定ESP-SR成功输出bytes累计credit，
只有完整1024B帧才开始current fetch，保留100ms参数与可见的停滞诊断。单次异常后
credit作为保守下界自动恢复；不确定库存达到四帧、已有不确定库存时feed返回0，或非零非法feed返回/credit溢出，才在旧lease排空后重置buffer+VAD，
epoch隔离旧read/拼接尾部。同代未AppendRaw的被拒read单列raw gap，已保存raw只丢
feed尾部则仅AFE gap。SDK仍保留不足160样本的WebRTC残余与AEC状态，不代表全新DSP。
只有连续三次可观察reset失败才终止Running；Assistant清理前核对interaction/transport
双代次与阶段。USB/wake只取同锁phase快照，原准入和wake代次检查保留。

35项frontend及30/36项Assistant/Wake host验证、TEST/普通两包和有限硬件窗口已分别独审；
TEST一格idle、三格Listening及同第三会话自然TTL600刷新已闭合独审；普通OFF包20261008-113338已恢复并完成独立cold与stop/rearm。TEST generation15仍有一次running stall待定位。任务观察不证明全PCM声学、全物理
资源归还、任意OOM或真实安全余量。见[034证据](ota-release-readiness.md#2026-10-08-afe-output-readiness-and-voice-recovery-034)。

## 仍未关闭的门禁

不据此宣称物理输入或所有资源完全归还、净内存节省、任意 OOM 恢复、DMA/IRQ/cache-off
安全、音频/SD/TLS 并发、识别与 AEC/音质或长稳通过。回收可能等待业务退出与跨核收敛，
没有硬性延迟期限；Capture 内存分配异常等业务失败也不属于本轮已验证的恢复保证。

资源和生产发布继续 **NO_GO**。034 generation15的running stall和实际余量仍待验，
不得由SDK empty消失替代输入健康。031 软件记录与
[030 已部署视频证据](task-retirement.md#030-制品与有限设备证据) 分开保留。

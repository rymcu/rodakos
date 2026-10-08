# 语音任务回收合同与分层验证

更新：2026-10-09。031 把 Assistant I/O、前端采集和唤醒 supervisor 的三个 WithCaps
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

当前 040：已完成 TEST 固件四点准备优先级观察和一次真实 USB wake 取证；普通 OFF 已恢复。真实记录为 `prepare_begin → open_acquired → cloud_returned → open_released`，四点有效优先级均为 4、`flags=0`，同一任务 handle，串口完整收集。该观察只覆盖端点，不证明整个等待区间的优先级继承、锁归属、CPU 使用或 stall 根因；根因 **INCONCLUSIVE**，资源与生产 **NO_GO**。

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

## 035 AFE停滞观测边界

源码 `b5c17a9e5d35e07714b8f3b07e9160be0a319512` 在普通与TEST中使用同一64B诊断对象。
Capture单写者通过独立portMUX发布56B元组；timer在临界区外，临界区仅作有界字段更新/
复制，没有SDK、业务锁、日志或分配。SDK返回状态在重取frontend mutex前发布，
区分raw、准备/准入、API边界及return-to-publish。API与锁调用只测墙钟，不能据此
断言DSP执行时间或TLS根因；返回记账完成也不保证0/非法输出增加credit。

同generation/epoch的gap保留首W的gap_id与各次count，瞬态错误后的重复W不合并删除。
仅有效完整fetch记recovered，取消/reset分别闭合；stall与closed在业务锁内先复制
producer元组、后采时，锁外打印冻结记录。闭合包含producer epoch最大值以及首W前后、
fetch返回再次取锁的consumer最大观察/锁墙钟。旧producer scope只标stale，最大值
不能当本gap唯一原因；`UINT32_MAX`耗时是饱和下界。原100ms、credit、取消排空、
raw gap、epoch、有限重同步与terminal准入完全保留。

完整TU49项Debug、49项ASan/UBSan/leak、五组CTest及14个精确负控通过；详见
[域测试合同](../tests/voice_audio_frontend_identity/README.md#035-afe-阶段与输出等待观测)。
隔离目标检查为Frontend576→640B、Capture固定帧176→240B、Fetch112→368B；普通/TEST
布局一致，仍有正常热路径开销。这些不是运行时HWM或实际资源余量。
035两份制品已独审，TEST四个running gap与普通OFF一个warmup gap均恢复完整输出；
普通OFF已恢复原绑定与空闲终态，闭合硬件独审通过有限窗口检查。五次W与阶段差异仍保留；
不把恢复输出等同根因已解，也不改写034历史。
当前根因和资源/生产**NO_GO**见[035证据](ota-release-readiness.md#2026-10-08-afe-stall-observability-035)。

## 036 TEST 调度与 cache 观察边界

独立 `voice_tick_observer` 模块只在 `RODAKOS_RELEASE_TESTS` 下编译，内部 DRAM 为两核各 128B 加控制 64B，共 320B。frontend ABI 仍为 640B，不新增任务或条件成员。每 generation 固定 20 秒 deadline；轮窗、resync 与 epoch 切换不延长窗口。20 秒到期不会注销 tick hook；inactive/expired 仍有回调前置开销，只有普通 OFF 完全缺席。ISR 单次 try-lock，发布失败仍推进私有前驱；前景三锁失败不部分切换，旧 generation/epoch/token 不能关闭新窗。

摘要保留真实相邻 ISR 入口间隔、getter 包络、异常计数/末次异常点及未覆盖前缀/尾段，不把 tick 次数乘 10ms，也不把 scheduler/cache 端点状态当作完整区间状态。AFE 事件时间与冻结包络分开，双核非同时采样。普通 OFF 隔离对象的 ALLOC sections/relocations 与 035 基线一致，最终 OFF 制品也已独审，observer 符号/状态缺席，已核静态布局与 035 普通 OFF 一致。

完整 OFF AFE 49 项与独立 observer 11 组分别在 Debug 和 ASan/UBSan/leak 下通过，精确负控共 18 个。gap-stop 的 generation 范围与非法 feed 的合法 resync 排空断言经固定 035/036 对照修正；没有改写真实 gap 或资源验收。模块合同见[observer 测试说明](../tests/voice_tick_observer/README.md)；两份制品及闭合硬件/普通恢复证据见[036 证据](ota-release-readiness.md#2026-10-08-bounded-tick-and-cache-observation-036)；四个真实 gap 均 recovered，根因与资源/生产 NO_GO 仍开放。

## 037 TEST 逐 feed 身份与进度观察边界

`voice_feed_progress_observer` 与 036 tick observer 同为 TEST-only，不新增任务或 frontend
条件成员。Capture 在 SDK feed 之前 arm，在返回之后、等待业务 mutex 之前 close；完整
generation / epoch / feed seq / ticket / opaque owner 身份控制当前 slot。Fetch 只在真实
current `api_boundary` 域关联 slot，不能以相同数字把 read / between_reads seq 借作 feed。

每 generation 固定 20 秒 deadline，epoch、轮窗和 resync 不续期；open 与 complete 共用
8 条日志额度，flow_stop 另有 summary。Close 在完整 immutable 身份匹配后、可能失败的
try-lock 之前撤销采样许可；flow_stop 最先 retire 当前 generation，先于外部 owner 回收
Capture。失败 Close 保留 unfinished slot，retired 记录不能伪装成成功完成，也不能继续把
后来相同 opaque 数值的句柄计入已退役 Capture。这个合同依赖既定 core0 单 writer 和
生命周期顺序；没有扩大为任意并发 task-handle 的通用所有权方案。

open 没有已知 arm upper；post-return、clock invalid、drop、saturated、retired、日志
抑制及未覆盖尾段都保留。`strict_counts_known` 仅说明被接受 getter 端点位于 API marker
到返回包络中，不推断 Ready/Blocked、连续 Running、调度次数、SDK 指令进度或 CPU/PCM
时长。getter 返回 opaque identity，不能将 target 命中当作整个前后区间都在运行，也不能把
other 命中解释成特定 Ready 或 Blocked 状态。

两个模块目标静态存储为 tick 320B + feed 144B = 464B。最终 TEST linked 静态独审已核
getter 的 IRAM 布局和新增观察路径；已知局部 Fetch 链 1072B、Capture 链 736B、所审 ISR
分支 208B 均尚未计入完整 SDK/logger/ROM/context/nesting 和实际 HWM，不能称为安全余量。
20 秒到期与日志额度耗尽不会去掉全部前景/ISR 前置开销；普通 OFF 才编译隔离 observer。

软件验证按 49 项完整 OFF、23 组模块和 7 项真实 TEST frontend 分层，不合并成一个没有
边界的总套件。30 个负控分别为 14 个旧 OFF、4 个 tick、8 个 feed、4 个 frontend 整合。
037 双包及闭合硬件独审已完成：fresh TEST 未记录 gap，自然到期 TEST 两个 gap、
普通 OFF 一个 gap 均 recovered，普通 `20261008-202609` 已恢复。seq3 的 target1 / other63
是有限 getter 端点，other 聚合所有非 target；第二 gap 缺 seq13 complete，不借先前 seq4
的 681147us。实际 heap/stack 余量、物理资源、声学与生产仍 **NO_GO**。

## 038 TEST 快照先于日志

源码 `6c807b87d164c38794b48a97b1632c8c1788ee4c` 仅调整 TEST 既有调用顺序：stall 的 tick/open feed
以及 recovered、两类 cancelled、resync 的 tick，在首条相关日志前冻结，仍在业务
mutex 释放之后。Capture 的 arm/Close/credit、旧 token 身份、固定20s和8+1配额、
普通 OFF 合同不变；停止处在原位置直接 Freeze/Log，避免嵌套 helper 的快照帧，
flow-stop采时阶段、Retire/summary顺序保持。独立时间包络和stale仍有意义，不宣称原子快照。

13项完整 TEST 与49项 OFF 分别通过 Debug、ASan/UBSan/leak，6个旧顺序负控精确拒绝。
目标构建与新栈帧按[038证据](ota-release-readiness.md#2026-10-08-snapshot-before-log-software-correction-038)核验，旧1072 B局部链
不复用。本轮无签包、硬件、串口或复位，设备仍沿用037普通包/source及终态记录，
未重新实测；这不是stall根因修复，资源与生产NO_GO不变。

## 039 PC-status 消息流对照

039 完成同 boot 普通 037 包 `20261008-202609` 上一次 normal→quiet→normal 对照，
沿用源码 `d852d9fdb6b16a0a34a49eb7555ddd5a9b944396`，桌面 PID2140。
三格均各有两个 recovered gap；Q 在110.1013822秒内抑制36次目标发布、转发0次，
因此目标 PC 消息流不是这格 stall 的必要条件，其他 MQTT/TLS、日志与 PI 候选仍未排除。
prime仅作准备，各格并非严格等时，不作统计效应或单 API 因果结论。

16条命令均有成功ACK；关联WSS关闭、rearm与IPC空闲后，原publish、临时inspector和
串口均已收尾，原绑定/tokenVersion4/authority保留。无刷写/擦除/复位/主动NVS操作；
自然刷新可能持久化凭据，不宣称NVS全字节不变。038 TEST快照修正未部署到本轮设备。
根因INCONCLUSIVE，资源与生产NO_GO；详细gap身份、完整哈希和独审见
[039证据](ota-release-readiness.md#2026-10-09-pc-status-causal-comparison-039)。

## 040 准备阶段自身优先级观察

040 在提交 `d2de3d3392d2f7f21e355911746dbf49bb2f27fb` 的 TEST 固件中加入一次性、单槽
观察器，只在当前任务的四个端点读取自身有效优先级：`prepare_begin`、
`open_acquired`、`cloud_returned`、`open_released`。Start/取锁失败使用两点或三点终态；
普通 OFF 不编译观察器和读取命令。观察器不增加任务、轮询、实时日志或音频参数，完成后
通过 `RODAK_RELEASE_TEST_V1 voice_prepare_take` 读取并消费记录。

Debug、ASan/UBSan/leak 的 observer、diagnostic 和真实 transport host 目标均通过；真实
transport 覆盖 9 个返回路径、真实 Cloud 单次调用、锁持有 `false/true/true/false` 与 OFF
缺席检查。目标静态审查确认 TEST 增量 DRAM 为 200 B（旧 tick/feed 为 320/144 B），并保留
个别函数未链接或内联、单函数入口帧不等于峰值栈的边界。目标审查为
`PASS_TARGET_COST_STATIC_ONLY`，资源和生产仍 NO_GO。

TEST 包 `20261009-014715` 通过官方验包与 immutable Recovery 锚定检查后，以增量方式只写
`otadata` 和 `ota_0`。设备 MAC `44:1b:f6:c3:b4:30` 的一次真实 USB wake 取得完整四点：
scope=1、同一任务 handle、优先级 `4/4/4/4`、`flags=0`、串口原始 16608 B、无丢行；
WSS 因目标资源不足未建立，不能将这次证据解释为实际 PI 发生或未发生。随后已恢复普通
OFF 包 `20261009-014905`，绑定/tokenVersion4、WiFi、MQTT 和 boot confirmation 保持正常。

## 仍未关闭的门禁

不据此宣称物理输入或所有资源完全归还、净内存节省、任意 OOM 恢复、DMA/IRQ/cache-off
安全、音频/SD/TLS 并发、识别与 AEC/音质或长稳通过。回收可能等待业务退出与跨核收敛，
没有硬性延迟期限；Capture 内存分配异常等业务失败也不属于本轮已验证的恢复保证。

资源和生产发布继续 **NO_GO**。039 已完成三格与现场清理，目标 PC 消息流不是本 Q 格 stall 的必要条件，其他候选未排除；038 快照顺序修正保持软件证据身份。034—036 的 gap 与资源边界按原身份保留；037 的三个 gap 均 recovered，有限端点观察仍未区分 Ready/Blocked 或证明实际余量、全部物理资源归还。031 软件记录与
[030 已部署视频证据](task-retirement.md#030-制品与有限设备证据) 分开保留。

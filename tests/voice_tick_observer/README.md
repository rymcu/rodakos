# 036 TEST tick/cache观察合同

此suite直接编译生产`voice_tick_observer.cc`全文，以host假的IDF timer、getter、动态hook
注册与critical入口隔离平台。测试include实际TU仅为读取/复位私有静态寿命状态；生产
未增加测试回调。它验证观测账本与并发边界，不模拟真实调度、cache、DSP或无线硬件。

```sh
cmake -S tests/voice_tick_observer -B /tmp/rodak-voice-tick -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/rodak-voice-tick
ctest --test-dir /tmp/rodak-voice-tick --output-on-failure
python3 tests/voice_tick_observer/run_negative.py --output /tmp/rodak-voice-tick-negatives
```

11组正控覆盖：注册成功/部分失败撤销及寿命锁存；真实tick间隔/补偿tick；丢样后私有
prev推进；三锁任意失败无副作用；generation/token/epoch fence；旧boot最大值隔离和
跨界predecessor；freeze后日志不读live；中间scheduler/cache异常及全窗getter包络；
固定deadline、负时钟恢复/计数饱和；sample跨scope和旧sample迟发布；未发布丢样尾段、
finish成功/失败及inactive计数基线；token耗尽明确退出。四个精确负控分别移后私有prev、
丢弃跨窗间隔、删除token fence、日志读取live；只有专属断言命中才算拒绝。

模块仅在既有`RODAKOS_RELEASE_TESTS`构建中编译，不添加frontend条件成员或新任务；
2核各128B与控制64B总计320B内部DRAM。实际IRAM链、目标固定帧、OFF符号/存储缺席仍
必须由隔离对象和最终包独审确认，host sizeof不是运行时余量或目标链接证明。

每generation在Fetch创建前启窗，deadline固定20秒；stall/closure/flow-stop才轮窗，
正常不足credit不轮窗。前景仅按control→core0→core1各try一次；失败无部分切换。
ISR每次推进私有prev/calls，即使发布锁失败；reader不读取私有值。已发布最大值为相邻
ISR入口的真实墙钟间隔，但有丢样时最大值可能漏失，不代表全窗最大实际中断延迟。
tick可被调用补偿、延迟或与调度器状态交错；这些观察不能自动归因TLS/DSP/NVS。

`published_calls/drops`为发布过的累计计数，baseline是轮窗时此前已发布的累计值。
未发布的边界前丢样可在边界后才出现，故不能无条件把差值归为本gap的精确丢样数。
inactive callback更新累计基线；deadline后拒绝更新该活动窗的计数和accepted coverage。
`first_published_sample_end_us=0`表示无accepted样本；first/last仅界定已发布样本端点，
不是完整覆盖证明。当前freeze到last之间永远是独立未覆盖尾段，尤其尚未发布的drop
不能由`drops=0`排除。prefix/crossing/drop/deadline/clock/saturation flags必须原样保留。

`max_sample_span_us`是全窗accepted getter包络的最大值；`max_state_bits`只对应最大
相邻入口间隔之后采样的状态。suspended/cache_disabled计数与最后异常sample-end保留
中间事件，不因最大gap/最后一次状态正常而清除。getter只读端点，0次异常不能排除两次
采样之间的scheduler/cache变化；达到u32上限的字段按饱和下界处理。

每条日志分开记录035事件时点与锁外freeze_begin/end包络。stall使用035 observed_us；
recovered/cancelled使用closed_us；resync存在实际gap闭合时同样使用该closed_us，未闭合
gap或无gap时使用reset_observed_us（reset决策时点）；flow_stop/create_failed为各自独立清理边界。
每核sample取得锁前可能属于旧scope：sample-end早于open拒绝推进新窗coverage，入口/
predecessor/getter包络跨open须留flag。持三锁只串行切scope，不表示双核同时采样。

保留五个035 gap及NO_GO；本模块用于下一层有限观测，未改变100ms、TTL600、优先级、
核绑定、frame credit、reset或terminal判定。

## 037 逐 feed 的 current-task 身份点

`voice_feed_progress_observer.cc` 是额外 TEST-only 模块，目标静态状态严格 144B，host
64 位指针布局为 160B（含新增两项 atomic32 生命周期门禁）。Capture 自己登记 current opaque handle 与 generation/epoch/feed
sequence；tick 只比较本核当前身份，不解引用 TCB、不反查任务名、不读取优先级或 EPC1。
既有 tick 的 entry→sample_end 包络包住 current getter 与 scheduler/cache getter。
`xTaskGetCurrentTaskHandle` 仅在 TEST 通过精确 linker fragment 迁入 IRAM；最终字面量、
ROM 恢复中断级调用链与成本仍需单独目标/最终 ELF 审查。FromISR 名称不等于有界：
`uxTaskPriorityGetFromISR` 会取 kernel spinlock，本实现没有使用它。

Arm 位于 api_begin 标记之后，Close 位于 api_return 之后、返回记账之前；complete 还带
同 seq 的 credit_published。它们是加有观测开销的 API 边界墙钟，不能当成 SDK CPU 时间。
所有 timer/log 在观测锁外，每次获取只有一次 try。完整 Arm 包络在 Capture 的 ticket
中；open 记录明确 `arm_after_us=0`/ArmAfterUnknown，不假造 late-Arm 前缀上界。
每类样本只保 first getter-begin 与 last getter-end，无 ring。最后 accepted end 超过
api_return 时，strict_counts_known=false，全部计数只能归 arm→close 包络，不补扣尾部；
getter 包络跨边界也按同一规则处理。跨 callback 时钟倒退置 ClockInvalid/drop，不能降低
已发表 last 来洗掉超界样本。零样本、私有未发表丢样、getter 间隙、尾段与 catch-up 均
禁止完整覆盖或“无暂停”结论。命中表示被中断/补偿期间的 current 身份点，不是独立调度
次数、任务执行时长或 SDK 指令进展；其他身份只能反证整段 Capture 一直 current。

stall 只在 producer stage 为 ApiBoundary 且 generation/epoch 匹配时请求同 feed seq；
read 序号域或旧 scope 显式标 ProducerUnaligned/stale，不借数值相同的后续 feed。open
不清槽，可与未来 complete 配对；036 原 window 轮转不重置 feed 槽。Close 失败保留旧
身份，但在取锁前已撤销采样授权；同代下一 Arm 返回 Conflict，余下该代观测可不完整；不重试或改业务行为。新代
清除旧身份并保留 UnfinishedReplaced，旧 ticket 不能清新槽；expired 新代不能挂旧 seq。

生命周期 API 不支持通用并发多 caller：只有固定 core0 Capture 调用 Arm/Close，immutable
身份由该唯一 writer 改写；Close 在完整 gen/epoch/seq/ticket/owner/起始时点核对后，先以
atomic32 ticket.store(0) 撤销，随后才尝试复制。Fetch 在 flow_stop、suspend 前 Retire；
StopAfe 先等 Capture feed lease 排空，再 join/delete Fetch，Deinit 随后才 join Capture。
新 generation 的 Arm 不会插入旧 Retire 的 load/check/store 之间；旧代号也不能撤新门禁。
ISR 在自己的 try-lock 成功后再次核 generation/ticket。未闭合 slot 可保为 Retired，但
退休后同 opaque 地址复用不再增加旧 owner 的计数。两个原子只用 load/store，无 CAS/RMW；
S3 的 is_always_lock_free=false 不代表这两种指令会调用 helper，尺寸与最终目标反汇编
分别核验，不能借 host 原子实现宣称 cache-off 链已通过。

每 generation 固定 afe_started+20s；epoch/window 切换不续期。每代最多 **8 条 feed
记录**（first3、open、complete 都计入），阈值 50ms 只决定日志，不改变业务 100ms。
额外仅在已有 flow_stop 输出一条 summary，故完整新日志上限为 **8+1 条/代**；quota
超额累计 suppressed。日志抢锁失败的数量明确 unknown，不把它并入已知 suppressed。
20s 到期不注销 hook；inactive/expired 仍有回调前置开销，Capture 仍有 Arm/Close、采时
与有限日志判断开销，只有普通 OFF 完全不编入这些调用/状态。

`feed_progress_test.cc` 直接编译实际生产模块，12 组覆盖身份/采时包络、open/seq/epoch/
ticket fence、返回后和跨 return getter、Arm 前缀、跨 callback 时钟倒退、单次锁失败与
unfinished conflict、新代过期身份、固定 deadline、丢样/饱和、跨 epoch 的8条额度、
阈值及冻结日志、完整身份撤销与退休后的同 opaque 复用。8 个精确负控分别删除 overrun、
忽略倒退、去掉 seq fence、按 epoch 重置 quota、失败 Close 清槽、日志读 live、Retire
不撤授权、只核部分身份即撤授权。编译失败、未知用例或其他崩溃不算检出。
原 036 模块另保留11组/4负控；该独立套件为隔离旧账本而 stub 新模块调用，实际 TEST
frontend+两模块整合由 `tests/voice_audio_frontend_identity` 独立验证，不合并冒充设备结果。

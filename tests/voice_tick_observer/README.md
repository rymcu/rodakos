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

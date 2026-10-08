# AFE 输出字节账本、取消与恢复回归

## 034 完整输出帧与有限自动恢复

固定 ESP-SR 2.2.2、ESP32-S3 的 1MIC/MR/16k/WebRTC 路径中，开启设备 AEC 时 feed
输入为每通道 256 样本，关闭 AEC 时为 160 样本；fetch 均需 512 单声道样本（1024B）。
开启 AEC 后，WebRTC 按 160 样本重分块，正常 feed 输出依次为 320/640/320/640/640B。
feed 返回的是写入输出 FIFO 的字节数，不是 ESP_OK，也不能把一次 feed 完成当成首帧就绪。
启动检查精确核对编译配置对应的 feed/fetch 尺寸、输出声道数与采样率。

SDK `sr_rb_read` 可先消费不足一帧的部分数据，再因后续数据超时报失败；AFE 此时返回
`ret_value=-1/data_size=0`，已经消费的数据不会重新出现。生产现在累计成功输出字节，
在调用 fetch 前预扣完整一帧，保留原 100ms 参数。单次异常 fetch 最多消费预扣字节，
剩余账本仍是实际库存的保守下界，可以自动恢复输出；PCM/AEC 连续性仍标记失效。

累计四帧不确定库存、带不确定库存的 feed 0，或异常 feed 返回 / 字节溢出触发重同步：
阻止新 feed lease，保留旧 lease 的消费排空，由唯一 fetch worker 等 lease 归还后调用
`reset_buffer` 与 `reset_vad`，清字节账本并推进输入 epoch。旧 epoch 的跨 reset
`ReadForOwner` 结果和应用层拼接尾部均丢弃。SDK 的 WebRTC 仍可能保留少于 160 样本的
旧残余，AEC 状态也保留；这是带明确 gap 的连续 DSP，不能声称重建了全新声学管线。
固定 SDK 的有效 handle 下 buffer reset 返回 1，但它会吞掉底层 reset 状态，不能据此
宣称所有 SDK 故障已覆盖。连续三次可观察 reset 失败将 recorder 置为非 Running，保留
错误快照供 Assistant 终止交互；一般暖机、等待和成功重同步始终保持 Running。

同代成功 raw read 若因重同步或 epoch 变化未进入 `AppendRaw`，记录 raw discontinuity；
已写入 raw 诊断、仅从 feed 拼接尾部丢弃的数据只影响 AFE 连续性。取消与旧代结果不会
给新诊断追加 gap。epoch 用例暂停下一次 raw read，排除输入耗尽造成的额外 I/O gap，
精确验证恢复前后只有一次被拒 raw read、960 个新 raw 样本和独立 AFE gap。

不足一帧时不调用 SDK，也不隐藏停滞：持续 100ms 无完整输出会报告 `AFE input stalled`
及 `warmup/running` 阶段、feed 开始/返回次数和 credit；同一连续等待只记一次，恢复后
下一次等待重新计时。首 feed 记录开始与耗时，首 fetch / 首输出记录相对创建时间。
退出摘要保留 `current_failures/cancelled_results`，另记录 feed 错误、stall、resync
和不确定库存。feed 0 是可见的输出丢弃，不归入取消或正常无数据。

`afe_output_flow_test.cc` 编译完整生产 TU，使用具有真实分块、partial 消费、FIFO、
WebRTC 残余以及 reset 行为的宿主边界模型。覆盖不足首帧时保留 PCM、持续停滞仍可见、
一次 partial 错误后恢复、累计错误重同步、异常 feed、信用上限、三次 reset 失败、
取消不足帧、resync 时 producer 背压，以及带不同 PCM 标记的旧 read / 局部尾部隔离。
它不执行专有 DSP 算法或真实 Xtensa 调度，不能把宿主丢失 160/640 样本的机制实验当作
033 真机实际丢失样本数。033 封存日志没有首 feed/fetch 次数或时刻，具体调度诱因仍须
新固件串口取证，不能仅由 WSS ready 前的警告推定 TLS 抢占是唯一原因。

主测试默认 `-DRODAK_FRONTEND_DEVICE_AEC=ON`。关闭 AEC 的独立构建使用 `OFF`，运行
`AFE compiled device AEC format accepts its exact SDK shape and rejects drift`，验证
feed160、fetch512 及错误格式拒绝。CMake 固定校验 AFE、audio processor 与 ring 所在
三份 SDK archive 的 SHA-256；升级库须重新审计字节单位与消费合同。

## 保留的 033 取消与连续性场景

生产 `AfeFetchTask` 在 fetch 返回后锁内复核生命周期。当前 conversation 同代错误继续
使 PCM/AEC 连续性失效并打印原警告；已经取消的返回结果单独累计。Stop 已关闭模式但
feed lease 尚在时，fetch 继续排空；只有 `afe_fetch_stopping_` 置位后才退出。原有有效
PCM 的 mode/generation 二次检查、100ms fetch 参数和外部删除/destroy 顺序保持不变。
退出摘要分别记录 `current_failures` 与 `cancelled_results`，不以消失的日志代替诊断。

五项用例编译完整生产 frontend TU，使用真实 PCM assembler 和 AEC diagnostic capture：

- 阻塞 fetch，Stop 已变更代次后返回 ESP_FAIL：取消单独计数，当前错误/连续性不增加。
- 取消时返回有效 PCM：不交付旧帧、不追加旧 AEC；重新 Start 后交付新代连续 PCM。
- 活跃时 PCM → 失败 → PCM：成功返回的样本保留，只有跨 gap 帧的 VAD 无效，AEC
  记录一次 discontinuity，后续正常帧恢复有效。
- 首帧就绪后的第 4 次 feed 只有第 2 次 fetch 进入才解除阻塞：取消仍须排空，destroy 不得发生在
  fake feed/fetch 调用期间。错误的提前退出可由精确断言捕获，安全逃生 gate 保证退出。
- 已接受的 256-sample 诊断输入耗尽后继续提供零样本；此时活跃 fetch 错误仍可见，
  有效 PCM 能恢复交付，不能把输入 EOF 误认为会话取消。

`afe_fetch_control.cc` 只控制 SDK 操作返回、背压和诊断日志观察；feed 仍由真实
CaptureTask 进入并持有生产 lease。结果内存由 script 持有至 worker 停止。测试通过
锁内 generation 与条件变量安排时序，不依赖睡眠后猜测已进入 Stop。默认 fake 行为仍
兼容先前身份/回收用例。五项不会证明模型声学或真实 I2S/AFE 调度性能。

`run_afe_negative.py` 编译以下完整 TU 与配套头，再运行对应单一探针：

| 对照 | 必须命中的行为断言 |
| --- | --- |
| `e6edadcd8d72ea8300c8afe3b0ccc1bb359eb80c` 完整旧实现 | cancelled ESP_FAIL 仍打印 rejected |
| 当前 TU 中取消后立即退出的变体 | 没有进入 feed 解除背压所需的第二次 fetch |
| 当前 TU 删除 current continuity 失效的变体 | gap 帧仍被错误标为有效 VAD |
| `78917fae1b9acb02010cef026facefc96a008c5c` 完整 033 TU | 不足首帧时已经消费并丢弃 160 样本 |
| 同一 033 TU 仅把 started 标志移到 feed 返回后 | 仍丢弃不足首帧的 160 样本 |
| 当前 TU 删除 raw read 的 epoch 校验 | reset 后重新 feed 320 个旧 read 样本 |
| 当前 TU 删除局部拼接 buffer 的 epoch 清理 | reset 后重新 feed 64 个旧尾部样本 |
| 当前 TU 删除同代被拒 raw read 的 gap 标记 | 960 个新 raw 样本被错误报告为无间断 |

报告在 `afe-negative-controls/results.json`，必须有专属观察标志、精确失败断言、
退出码 1 与一项测试失败；编译失败、超时、崩溃或任意非零退出不算检出。
此套件同时保留 031 任务回收及 033 MultiNet 命令表所有权的正负控。

032 中 TEST 第一次会话和普通 OFF 的 empty/rejected 出现在 WSS ready 之前，不能
归为已证明无害的 stop/rearm。SDK empty WARN 保留，具体真机启动调度诱因仍待取证。
100ms 参数不是整次调用或 Stop 的硬期限；任意 OOM、物理资源完整归还、声学/AEC/TTS、
长稳与内部栈余量仍没有新通过结论。

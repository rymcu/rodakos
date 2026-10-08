# 033 AFE 取消结果与排空回归

生产 `AfeFetchTask` 在 fetch 返回后锁内复核生命周期。当前 conversation 同代错误继续
使 PCM/AEC 连续性失效并打印原警告；已经取消的返回结果单独累计。Stop 已关闭模式但
feed lease 尚在时，fetch 继续排空；只有 `afe_fetch_stopping_` 置位后才退出。原有有效
PCM 的 mode/generation 二次检查、100ms fetch 参数和外部删除/destroy 顺序保持不变。
退出摘要分别记录 `current_failures` 与 `cancelled_results`，不以消失的日志代替诊断。

五项用例编译完整生产 frontend TU，使用真实 PCM assembler 和 AEC diagnostic capture：

- 阻塞 fetch，Stop 已变更代次后返回 ESP_FAIL：取消单独计数，当前错误/连续性不增加。
- 取消时返回有效 PCM：不交付旧帧、不追加旧 AEC；重新 Start 后交付新代连续 PCM。
- 活跃时 partial PCM → 失败 → PCM：所有样本保留，只有跨 gap 帧的 VAD 无效，AEC
  记录一次 discontinuity，后续正常帧恢复有效。
- 第 2 次 feed 只有第 2 次 fetch 进入才解除阻塞：取消仍须排空，destroy 不得发生在
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

报告在 `afe-negative-controls/results.json`，必须有专属观察标志、精确失败断言、
退出码 1 与一项测试失败；编译失败、超时、崩溃或任意非零退出不算检出。
此套件同时保留 031 任务回收及 033 MultiNet 命令表所有权的正负控。

032 中 TEST 第一次会话和普通 OFF 的 empty/rejected 出现在 WSS ready 之前，不能
归为已证明无害的 stop/rearm。SDK empty WARN 保留，启动期 feed/调度根因仍待取证。
100ms 参数不是整次调用或 Stop 的硬期限；任意 OOM、物理资源完整归还、声学/AEC/TTS、
长稳与内部栈余量仍没有新通过结论。

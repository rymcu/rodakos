# PrepareInteraction 的任务自身优先级接线测试

本目标编译完整生产 `realtime_voice_transport.cc`、真实 Cloud TU 和真实 TEST observer。
复用 `server_trust` 的 HTTP、Settings/NVS fixture；FreeRTOS mutex/event 使用同步 host
替身，WSS、线程创建和物理 heap/stack 接口是 fail-fast 哨兵，不建立网络模型。

9 个用例覆盖 Start/take 失败、取得锁后的两类提前取消、fresh 成功及重复调用、Cloud
未配置/TLS-open 失败、无 voice 配置及 Cloud 返回后的取消。链接器 wrap **真实**
`PrepareVoiceConfig` 并调用原实现，精确核验每次调用为 0 或 1，避免只数 HTTP 时漏掉
fresh 重复调用。priority getter 只接受 `nullptr`、禁止 observer 锁内查询，并记录当前
open mutex 持有状态：完整四点为 `false/true/true/false`。通过真实 observer 快照验证
阶段、次数与终点；OFF 完整 transport 对象另外检查 observer 符号/引用缺席。
该 OFF 对象禁用 sanitizer 注册信息，检查字符串前去掉 DWARF，避免测试目录名误判。

在 WSL 使用 CMake/Ninja 配置本目录并运行本目标的两个 CTest。可使用常规
`-fsanitize=address,undefined -fno-omit-frame-pointer` 与 leak 检查；不运行旧矩阵。

这里证明 host 替身下的实际接线/返回顺序，不证明目标 FreeRTOS PI、锁等待时长、TLS、
调度、物理资源或目标栈余量。OFF 对象检查也不替代最终固件 ELF/链接/布局审查。

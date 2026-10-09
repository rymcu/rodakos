# Camera 关闭阶段诊断

021 在 Camera → Photos 时最后输出 `CloseStream: STREAMOFF begin`，之后串口静默
127.583 秒。仅凭这条日志，不能确定 `ioctl` 尚未返回：实际使用的
`esp_cam_sensor` 2.3.0 extended DVP 驱动会直接删除 worker，后续普通日志也可能阻塞。
022 增加独立于日志的阶段记录，用于下一次故障取证；没有改变关闭顺序或修复任务生命周期。
028 在同一受校验 overlay 中加入合作式 worker 退出；本轮继续加入失败后可重试的 DVP
分阶段 owner 释放，并为受 IDF 范围约束的8192配置增加6144→4096的连续块降级尝试。
027 故障与调试干预证据保留。

## 记录合同

`rodak_camera_teardown_diagnostics` 是启动内的固定 536 字节内部 DRAM 对象，包含
6 个 32 位头字段和 32 条记录。每条为 `phase / core / int32 status / commit_seq`。
一次成功关闭最多经过 24 个标记：服务层 ioctl 与完成日志前后、sensor 停流、controller
stop/disable/del，以及 DVP worker 删除、GPIO、capture stop 和 GDMA 清理边界。
完整 ABI 与阶段表见 [记录模块测试](../tests/camera_teardown_diagnostics/README.md)。

写入只尝试一次 strong CAS，不分配、不等待、不调用日志。竞争或容量耗尽时保留已有记录，
设置粘性 drop 标志；不重试、不循环覆盖、不提供在线 reset。每条 payload 写完后 release
发布 `commit_seq`，认领后被停止的 writer 可留下 pending 槽。DVP 每个失败阶段只记录首次
尝试，后续 owner release 仅重试未完成阶段；上层多次 ioctl 仍会追加服务层记录。在线读者
必须使用 bounded snapshot；调试器读取则须先确认两核停止。记录不跨设备复位保留。

序号表示认领顺序，不是跨核绝对时间；core 是调用点采样，不是 IRQ 注册核。状态保留被调用
函数的原返回值，`ioctl=-1` 不包含 errno，也不是底层 `esp_err_t`。enter/void returned 为 0。
pending/drop 或开始之前的失败会造成缺标记，不能将缺失直接解释成没有执行到该点。
普通 begin 日志仍在 ioctl enter 标记之前，可能在第一个标记前阻塞。

## 构建与依赖

`camera_teardown_patch.cmake` 只替换构建中的两份源文件：`esp_video` 的 common stop 与
实际 extended DVP 后端。生成前校验组件、源码/头、项目 manifest、完整依赖图和相关
ESP-IDF 6.0.2 输入；依赖图只忽略平台生成的顶层 `manifest_hash`。managed 源文件保持原样。
升级须重新审查这些调用边界，不能仅更新哈希。详见
[overlay 合同](../patches/camera_teardown/2.3.0/README.md)。

ESP-IDF 为 PSRAM 原子操作启用 `CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND`，全局仍
保持该设置。IDF 自身对内部 RAM 使用原生 S32C1I。本项目只给诊断模块这一个翻译单元
追加 `-mno-disable-hardware-atomics`；它的原子访问目标仅为上述 DRAM 对象，不能复制
此选项到包含外部 RAM 原子状态的代码。编译期 lock-free 断言与最终 ELF 门禁同时保留。

`camera_teardown_diagnostics.cmake` 强制核验同次最终 ELF：初始化对象大小/布局、内部
DRAM、IRAM recorder、真实指令字节、一次 CAS、无调用/循环/回跳，以及 literal 的实际
RAM 地址。输出 `build/camera-teardown-linked.json`；对象文件或 host 通过不能替代它。
该门禁范围限于记录器，不证明驱动关闭、IRQ 生命周期、cache 或硬件可读性。

构建证据须保留四个实际编译入口、展开后的 response 参数、两个生成 C 文件及源码摘要，
并关联到同次 ELF/sdkconfig 和链接报告。历史 CI 制品可在其原范围内复核；当前交付通过
本地构建与包核验保存这些证据，不依赖或修复 GitHub Actions。

## 028 worker 生命周期修正

实际 extended DVP 仍使用受 IDF 范围约束的 8,192 B 配置（当前几何生成约 7,680 B），并在
连续块不足时尝试 6,144 B、再尝试 4,096 B 的帧对齐 ring；同时保留 3,072 B worker 栈、
优先级 23 和长度 3 的队列。旧 8,192 B 配置当前几何生成 7,680 B，
在长稳碎片化窗口中曾出现总 DMA 空闲 20–27 KiB 而最大连续块只有 5–7 KiB 的分配失败。
构造仅将 worker stack 改为
`xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`，不回退内部栈；TCB、
controller、ring、descriptor 和 queue 保持原内部内存要求。目标 ELF DWARF 的 controller
从 144 B 增至 148 B；3,072 B 栈迁移与 4 B 结构增量不是实机 free/largest 净收益。
最终 ELF 中 IRAM text 增加 72 B、末尾对齐增加 184 B，合计使内部 SRAM 边界后移 256 B；
DRAM dummy 增加 256 B、data/bss 地址及 heap start 同步增加 256 B，data/bss 大小和每任务对齐 TLS 大小
均未变。不能仅凭 BSS 大小不变声称静态内部占用不变，也不能把 IRAM 与映射的 DRAM
区域当成两份独立占用相加。028 的记录表地址为 `0x3fca85e8`，必须按对应 ELF 取证。

owner 在原 spinlock 内发布 shutdown，再非阻塞地向队列前端写 STOP；队列已满时已有事件
能够唤醒 worker。worker 在接收前后复查请求，等待已进入的回调、日志或处理分支结束，
然后在同一锁内发布 quiesced。最后解锁后不再访问 controller、queue、ring、回调或日志，
只进入 suspend。owner 用同一锁观察到确认后，调用 `vTaskDeleteWithCaps(saved_handle)`，
再执行原 GPIO/capture/GDMA/内存清理。不能只读一个未同步的标志后提前释放。

worker 回调尝试删除自身时，在获取锁和输出日志之前拒绝，避免等待自己的退出。
不使用 WithCaps 自删，因为当前 IDF 的该路径会再创建内部清理任务，低内存下可能失败。
构造失败保持配套清理，managed 源文件不修改；输入源码、IDF WithCaps 实现及生成 C 都受
provenance 约束。

等待 quiesced 没有“超时即成功”分支。若第三方回调或日志本身一直不返回，owner 仍可能
等待；本修正不承诺任意外部死锁可恢复。host 的受控日志锁、队列空/满、最后解锁、任务
handle 发布前运行及处理中关闭回归，与实际 GPIO ISR drain、cache-off/NVS/OTA 并发、
无线调度和资源回收仍是不同证据。最终记录器 ELF 门禁及 ioctl/日志区别回归继续保留。

本地 Windows Debug 已通过 Camera teardown overlay 33 个 CTest（含分阶段失败重试和
self-delete 拒绝），生成器 Python 11/11；Camera deinit source contract 11/11；worker
13 个正向案例仍可由既有 host 产物复核。本轮 ESP-IDF 6.0.2 隔离构建、Camera teardown
ELF audit 和 JPEG allocator audit 均通过；构建只保留既有 Recovery 分区容量警告。这些
离线结果不等于硬件首帧、吞吐或八小时长稳 GO。
旧 027 的真实生成 worker 在持有日志锁时直接进入删除，明确触发指定断言；不以超时或
sanitizer 崩溃冒充红例。028 的 ESP-IDF 6.0.2 编译及 recorder/JPEG 最终 ELF 门禁通过；
实机结果须以相应已刷具名包的独立记录为准。

## 取证与当前边界

先冻结已刷写包、源码、ELF、map 和校验 JSON 的 SHA-256，再从该 ELF 取得记录表地址。
正常复现期间不连接调试器；发生静默后记录串口/网络时间窗，再进行有界双核暂停和读取。
ESP32-S3 OpenOCD halt 会改变 watchdog 状态，普通 resume 不保证恢复它；调试干预后的
窗口不能用于证明 watchdog 或长稳健康。恢复与后续观测必须另开窗口。

Windows MI02 缺少 DeviceInterfaceGUID 的接入问题已通过管理员安装官方驱动修复，
MI00/COM3 保留原 usbser。OpenOCD 需精确匹配大写 serial `44:1B:F6:C3:B4:30`；
双核与固定 DRAM 读取能力检查已通过。历史 `/kp` 校验仍为 exit 1，驱动安装成功不代表
WHQL 或 kernel-policy 校验通过。

022 保 NVS 刷写后，两轮 Camera 分别运行 28.300 秒 / 380 帧和 88.744 秒 / 1,171 帧，
均正常退出。首轮关闭后才进行 JTAG，读到 24 committed / 0 pending / 0 drop；读取后
的新 halt 却落在 cache-error panic 与 semihosting panic 断点，随后串口静默、屏幕断开。
最终两核 running 与离线 decoder 成功均不证明健康恢复，首次 panic 的具体触发原因仍未确定。
后续独立复位窗口未连接 JTAG，Camera 正常退出后回 Home，串口继续输出 147.311 秒。
两轮正常退出不能覆盖 021 的 127.583 秒 STREAMOFF 停滞，也未取得该原始故障的阶段记录。

同版 OpenOCD 的一次 SMP resume 会恢复其他已停核心；poll 还可能处理 semihosting 并
自动恢复。后续 observer 应避免额外逐核 resume，并将采集后的任何新 halt 视为恢复未成立；
仅删掉一次 resume 不能承诺无扰动。原 observer/raw 保留不变，详见
[022 取证记录](ota-release-readiness.md#2026-10-07-camera-teardown-diagnostics-022)。
023 的[静态首帧定向通过](ota-release-readiness.md#2026-10-07-static-screen-first-frame-validation-023)
没有修改 Camera 释放生命周期。后续 027 的故障阶段记录见下，原始窗口仍分别保留。

### 027：ioctl 返回后的日志边界

源码 `3ff55ab7ec3d46cd7a1e2c41fca5d700505c0a06`、包 `20261008-004814` 的正常 Camera
窗口没有远端画面。在 native preview 后、peer open 前 DMA free/largest 已降至
1,907/1,792 B，open 后为 943/832 B；900 设备毫秒后 AES 分配失败、TLS 写 `-0x0084`，
MQTT 断开。最后一条 STREAMOFF begin 后至请求关闭原始串口采集共 492.499 秒无新字节。
此时桌面 Stop 为 unknown；内部首帧 87 ms 不表示远端视频成功。

关闭上述未干预窗口后，独立 JTAG 从该包 ELF 的 `0x3fca84e8` 读取 134 words：
23 committed、0 pending、0 drop，所有记录 core/status 为 0。最后阶段为
`... 24, 12, 2, 3`，其中 2 明确证明 ioctl 返回 0，3 为完成日志前，4 缺失。
冻结 ELF 机器码确认 3 与 4 之间调用 `esp_log_timestamp/esp_log`，因此本次不能继续判为
ioctl 未返回；这仍未直接证明具体锁持有者。旧 worker 在回调或日志内被直接删除的源码风险
与这一边界相符，须通过受控回归和后续实机修复分别验证。

暂停时双核 PC 均为 `esp_cpu_wait_for_intr`，采集后仅一次 SMP resume 仍进入
semihosting/cache-error panic 路径。原故障与调试干预后的 panic 分开记录，OpenOCD exit 0
不代表恢复。随后直接 RTS 尝试留下 90 秒空日志、MQTT 仍离线；官方 esptool USB reset
则恢复同一已安装 027 的 main/Home/OTA/MQTT，没有重新刷写或擦除。首启脚本 exit 5 只因
缺少 Recovery 标记，不适用于已确认镜像直接 main 启动；新快照确认原 ID、bound、token4。

证据位于相邻 Rodak 仓库 `.codex-temp/candidate-capacity-027/`：`normal-camera/`、
`camera-post-debug/`、`usb-reset-recovery/` 和 `independent-review/fault-dram/analysis.json`
（SHA-256 `e685929a360c7629132afec2306c85a7f93137515782e070943c0968401c106c`）。
完整窗口与制品身份见
[跨仓验证](https://github.com/rymcu/rodak/blob/master/docs/video-candidate-resource-verification.md)。

软件验证分别覆盖记录发布协议、真实 C 驱动函数的原语义与漂移拒绝，以及生产
CameraService 在 ioctl 内和返回后日志处受控阻塞的区别。它们不关闭 Camera 退出、
媒体并发、资源归还或八小时 soak 门禁；发布仍为 **NO_GO**。

# Camera 关闭阶段诊断

021 在 Camera → Photos 时最后输出 `CloseStream: STREAMOFF begin`，之后串口静默
127.583 秒。仅凭这条日志，不能确定 `ioctl` 尚未返回：实际使用的
`esp_cam_sensor` 2.3.0 extended DVP 驱动会直接删除 worker，后续普通日志也可能阻塞。
022 增加独立于日志的阶段记录，用于下一次故障取证；没有改变关闭顺序或修复任务生命周期。
028 在同一受校验 overlay 中加入合作式 worker 退出；后续加入失败后可重试的 DVP
分阶段 owner 释放。046 保留受 IDF 范围约束的 8192 配置上限，将非 JPEG ring 改为
6144 首选、4096 回退；JPEG 仍先使用配置值。
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

实际 extended DVP 保留受 IDF 范围约束的 8,192 B 配置上限。046 之前先尝试配置值，
连续块不足时再尝试 6,144 B 和 4,096 B；046 起，非 JPEG 模式直接首选 6,144 B，失败后
回退 4,096 B，JPEG 模式仍按 8,192→6,144→4,096 B。320×240 RGB565 下三档实际 ring /
half / 每帧接收事件分别为 7,680/3,840/40、6,144/3,072/50、4,096/2,048/75 B；首选档
相对旧档增加 25% 接收事件，但非 JPEG 每半区仍只需一个 DMA descriptor。同时保留
3,072 B worker 栈、优先级 23 和长度 3 的队列。旧长稳碎片化窗口曾出现总 DMA 空闲
20–27 KiB 而最大连续块只有 5–7 KiB 的分配失败。
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

## 044：最新修复包实机窗口

源码 `a9c68bd454d697df6bc2120bd884fb44daebcf29` 已打包为
`20261009-174828` / `camera-dvp-release-044`。ESP-IDF 6.0.2 构建、签名包校验、
设备 VerifyOnly 和保留 NVS 的增量刷新均通过；设备 ID、绑定和 token version 保持。

三个独立 Camera 启动均提交了软件预览首帧。每次返回 Home 时都按顺序出现 STREAMOFF、
fd close、device release、preview stop、preview destroy、audio release 六个关闭阶段，
随后至少 65 秒的 MQTT/Main/Voice 健康样本保持新鲜且 Voice 恢复 listening。

该结果仍为 **NO_GO**。三次停止均在 STREAMOFF 期间输出 raw partial-frame `E:RX`；严格
应用样本的最大连续内部块低于 4.5 KiB。Camera 启动前 DMA largest 仍为 16 KiB，因此
没有实际触发 6144/4096 ring fallback，也没有取得降级吞吐证据。软件首帧和关闭日志不能
替代物理画质、任意 OOM、完整资源归还或八小时新包长稳。

## 045：主动停流半帧隔离

源码 `85538b0` 将 controller stop 置于 sensor STREAMOFF 之前，并在同一 spinlock 下发布
`stream_stop_requested` 与 DVP FSM。worker 对已排队或正在处理的停流事件跳过回调、错误日志、
采集重启和 VSYNC 重开；正常流中的真实半帧仍保留 `E:RX`。源码负控覆盖删除 stop 检查、
恢复 sensor-first 顺序和非停流半帧错误语义。Camera capture 7/7、teardown 33/33、worker
17/17 加 7 组负变异均在 Debug 与 ASan/UBSan 下通过；ESP-IDF 6.0.2 构建及最终 ELF/JPEG
审计通过。

开发签名普通包 `20261009-193618` / `camera-stream-stop-045` 通过签名验包、COM3 VerifyOnly
及保留 NVS 的 `otadata + ota_0` 刷写。Recovery → Main → OTA confirmation → Home 通过，
原设备 ID、`bound`、`tokenVersion=4` 和 MQTT 在线状态保持。

三个独立 Camera → Home 窗口均取得软件首帧、六个关闭阶段和 65.000–65.094 秒新鲜
MQTT/Main/Voice 健康样本，Voice 均为 listening，三份原始串口日志的 `E:RX` 总数为 0。
该局部故障已关闭，但结果仍为 **NO_GO**：三个应用窗口的最大连续内部块均为 4,096 B，
健康期 DMA largest 均为 7,680 B，尚未证明 6,144/4,096 ring fallback、资源余量、物理画质、
任意 OOM 或长期稳定性，因此没有启动新的八小时资格长稳。

## 046：非 JPEG DMA headroom 候选

045 三次启动前的 `internal_dma_largest` 均为 15–16 KiB，因此旧顺序总会选择 7,680 B
实际 ring，无法覆盖 6,144/4,096 分支。046 将非 JPEG 选择顺序改为 6,144→4,096，保留
`CONFIG_CAM_CTRL_DVP_DMA_BUFFER_SIZE=8192` 作为合法上限；JPEG 路径保持配置值优先。每次
成功选择都会记录 configured、selected、actual ring、half 和每半 descriptor 数，供实机
日志直接核对。

真实生成函数的 host 回归覆盖 6,144 首选、JPEG 保留 8,192、ring 分配失败回退、descriptor
分配失败释放 ring 后回退，以及全部失败无泄漏。Camera teardown 38/38、worker 17/17 加
7 组源码负控均在 Debug 与 ASan/UBSan 下通过；Camera capture 7/7、device lifecycle 4/4、
DVP deinit 13/13、DVP RCC 6/6 通过。ESP-IDF 6.0.2 构建、Camera teardown 最终 ELF 审计和
JPEG allocator 审计通过。主镜像为 7,158,464 B，SHA-256
`fe29de1ef0410876bccdb34dfcc4584cf791f7a4facf3758bea407dcdfcccd9d`。

源码 `0e29b14` 已打包为开发签名普通包 `20261009-202156` / `camera-dma-headroom-046`。
签名验包、COM3 VerifyOnly、保留 NVS 的 `otadata + ota_0` 刷写及 Recovery → Main → OTA
confirmation → Home 均通过；原设备 ID、`44:1b:f6:c3:b4:30`、`bound`、tokenVersion 4 和
MQTT 在线状态保持。

一个独立 Camera → Home 窗口明确记录 `selected=6144 actual=6144 half=3072 desc_half=1`，
取得软件首帧、六阶段关闭和 65.093 秒新鲜 MQTT/Main/Voice 健康样本；随后一个单串口
5 次循环每次都选择 6,144 B、取得首帧和六阶段关闭。六次合计 `E:RX`、overflow、DQBUF
和 error 均为 0，5 次循环的内部 heap median drop 为 0。

发布状态仍为 **NO_GO**。单次应用窗口最低连续内部块为 4,352 B，5 次循环为 5,120 B；
健康期 DMA largest 仍只有 6,144 B。当前结果证明 6,144 B 实际路径可重复运行，但没有证明
4,096 B fallback、充足连续内存余量、物理画质、任意 OOM、完整资源释放或长期稳定性，
因此没有启动新的八小时资格长稳。证据位于 `.codex-temp/camera-dma-046/`，首次启动日志为
`build/logs/first-boot-20261009-202314.log`。

## 047：受控 4,096 B fallback 实机覆盖

源码 `4bf341f` 增加默认关闭的 `RODAKOS_CAMERA_DMA_FORCE_4096`，只给 Camera sensor 组件
注入编译定义。开启时，非 JPEG 分配器跳过 6,144 B 候选并输出明确的 release fault marker；
JPEG 和普通 OFF 构建不改变。打包、验包和刷写均要求显式 fault allow 开关，避免测试镜像
进入普通发布流。host 回归新增独立 fault executable，验证仅尝试 4,096 B ring、half 为
2,048 B、每半一个 descriptor 且 marker 存在；Camera teardown 39/39 在 Debug 与
ASan/UBSan 下通过，generator 13/13，worker 17/17 与 7 组源码负控通过。

fault 包 `20261009-205201` / `camera-dma-fallback-047` 通过签名验包、COM3 VerifyOnly 与保留
NVS 的 `otadata + ota_0` 刷写。单串口窗口记录
`RODAKOS_RELEASE_FAULT_INJECTION_ACTIVE` 和
`selected=4096 actual=4096 half=2048 desc_half=1`，随后取得软件首帧、六阶段关闭和
65.000 秒 MQTT/Main/Voice 健康观察。`E:RX`、overflow、DQBUF、ESP error 均为 0，内部
heap median drop 为 0。

这只关闭“4,096 B 分支从未在实机执行”的覆盖缺口。应用最大连续内部块最低 6,400 B，
健康期 DMA largest 最低 6,656 B，Voice supervisor 最低剩余栈 2,388 B，内部历史最低
2,123 B；资源采集仍为 **NO_GO**。物理成像、充足连续余量、任意 OOM、完整资源释放和长稳
均未证明。测试后已恢复普通 046，确认 Home、原 ID、`bound`、tokenVersion 4、MQTT 在线，
并把工作区开关恢复 OFF、重建普通固件；fault 包不得用于生产。证据位于
`.codex-temp/camera-dma-047/`。

## 059/060：GC0308 test pattern 定位与普通恢复

`5972684` 增加默认关闭的 `RODAKOS_CAMERA_TEST_PATTERN`。059 诊断包在 Camera 打开后通过
`V4L2_CID_TEST_PATTERN` 请求 GC0308 彩条，并输出受控 fault marker。实机 Remote Camera
取得 320×240 彩条 JPEG，RGB 标准差约为 102.38/111.83/106.62；串口记录 6,144 B ring、
125 ms 首帧、258 帧和完整 STREAMOFF/fd/device release，停止后 internal/DMA largest 恢复
6,144 B。该结果证明 SCCB test-pattern 控制和 DVP/RGB565/JPEG/WebRTC 链路能传输动态像素，
普通单色暗帧不应继续归因于 JPEG 或 WebRTC 数据通路。

诊断后已恢复 `RODAKOS_CAMERA_TEST_PATTERN=OFF`，普通 060 包
`20261010-003215` / `camera-test-pattern-off-060` 通过验签、VerifyOnly、保留 NVS 刷写及
Recovery → Main → OTA confirmation → Home。普通二进制不含 fault marker；一次 Remote
Camera 在 91 ms 取得首帧、完整释放且无 Camera/DVP 错误，但 JPEG 仍与 058 的单色暗帧
逐字节一致。后续诊断应比较普通模式 GC0308 初始化后的关键寄存器和 test-pattern 切换前后
状态，并检查曝光/增益、XCLK/供电及镜头/遮挡。该证据不关闭物理画质、失败 STREAMON、
任意 OOM、完整并发或八小时资格门禁，状态仍为 **NO_GO**。

## 062/063：GC0308 寄存器收敛与普通恢复

提交 `c73c08f` 增加默认关闭的 `RODAKOS_CAMERA_SENSOR_DIAGNOSTICS`。诊断包
`20261010-011055` / `camera-register-settled-062` 在配置、STREAMON、首帧和第 60 帧
分别读取 GC0308 page 0/1，所有 SCCB 读取 `failures=0`，每次读取后恢复 page 0。实机首帧
94 ms、运行 328 帧，关闭阶段完成 `STREAMOFF`、fd close 和 Board Manager device release。

关键快照如下：

| 阶段 | page 0 暴光候选 | page 1 动态寄存器 |
| --- | --- | --- |
| configured | `03=00 04=96` | `62=81 63=21 64=69 65=69` |
| streaming | `03=00 04=96` | `62=81 63=21 64=69 65=69` |
| first-frame | `03=00 04=96` | `62=69 63=1f 64=56 65=5f` |
| settled（第 60 帧） | `03=01 04=e0` | `62=1c 63=1c 64=1c 65=1c` |

因此 AEC/AGC 已经在运行并改变了曝光相关寄存器；但 Remote Camera JPEG 仍与 058/060/061
完全同哈希 `7b68a3de4667b8288ebd434177f0344b58e198e53c67bef7fc51dfb5851b5f46`。这将问题
进一步收窄到模拟前端、镜头/遮挡、供电或普通 RGB 输出配置，不能据此声称物理画质通过。

诊断结束后已恢复普通 063：包 `20261010-064533` / `camera-register-off-063`，
`RODAKOS_CAMERA_SENSOR_DIAGNOSTICS=OFF`、`RODAKOS_CAMERA_TEST_PATTERN=OFF`，主镜像
7,161,264 B，SHA-256 `846b585769b96b6c6e77cc996d5442fabd19435bb30ab93d2f278d0b58b52ec3`。
本地验签、COM3 VerifyOnly、保留 NVS 的 `otadata + ota_0` 刷写、Recovery → Main → OTA
confirmation → Home、WiFi/MQTT 恢复均通过，未使用 `-Erase`；普通设备无 fault marker。
062 原始串口在 RodakOS `.codex-temp/camera-register-062/serial-062.log`，Remote Camera
证据在 Rodak `.codex-temp/camera-physical-058/camera-stream-*-062-settled.*`。物理画质、
任意 OOM、异常并发和八小时资格门禁继续 **NO_GO**，未启动新的长稳。

## 062 后的软件输入审计

为避免把已核对的 managed source 当成新的修复方向，补做了构建输入审计：

- `managed_components/espressif__esp_cam_sensor/sensors/gc0308/gc0308.c` SHA-256 为
  `36f221ef43559c9fc222a1dc23b91a5eb64fb899d521a8fbb11a08b305c2052a`，与
  `patches/dvp_deinit/2.3.0/provenance.json` 中受审查版本完全一致。
- BigSmart Board Manager 配置固定为 GC0308 DVP、20 MHz XCLK（GPIO5）、VSYNC GPIO44、
  DE GPIO46、PCLK GPIO7，8-bit data mapping 为 `16/18/8/17/15/6/4/9`；sensor 没有独立
  reset/pwdn GPIO。
- PCA9557 的 DVP_EN 为低有效，默认输出 `[1, 1, 0]` 保持摄像头供电；063 构建的
  `RODAKOS_CAMERA_DMA_FORCE_4096`、`RODAKOS_CAMERA_SENSOR_DIAGNOSTICS` 和
  `RODAKOS_CAMERA_TEST_PATTERN` 均为 OFF。

因此目前没有证据支持继续改 managed GC0308 寄存器表或 DVP 引脚映射。下一次硬件窗口应记录
摄像头模组镜头/遮挡状态、DVP_EN 电平、GPIO5 XCLK、PCLK/VSYNC 活动和摄像头供电；若电气
信号正常而普通帧仍为同一暗值，再考虑更换模组或针对模拟前端做板级维修。软件首帧、SCCB
读取和 test pattern 仍不能替代这些物理证据。

## 2026-10-10 Camera OOM 首次失败后重试 073d 与普通恢复 074

073d 在保持 8,192 B Camera 启动余量的前提下，加入一次性首次非 JPEG DVP ring 分配失败注入；失败后下一次启动跳过 6,144 B，优先使用按帧对齐的 3,840 B recovery ring（半环 1,920 B），常规路径仍优先 6,144 B、再回退 4,096 B。Host teardown Debug 41/41、camera_deinit_lifecycle 13/13、camera_serial_tool 13/13 和生成器回归通过，ESP-IDF 6.0.2 fault 构建/最终 ELF 审计通过。

故障包 `20261010-120147` / `camera-dma-fail-retry-073d` 通过签名验包、COM3 增量刷写（保留 NVS，未使用 `-Erase`）和启动确认。首轮恰好一个 fault marker、一个 `Not enough space`、无首帧并完整释放；重试选择 `configured=8192 selected=3840 actual=3840 half=1920 desc_half=1`，取得首帧（串口 uptime 84,558 ms）、六阶段关闭完整，并完成 65.047 秒 MQTT/Main/Voice 健康观察。健康期 internal/DMA largest 均为 8,192 B，Voice supervisor 最低剩余栈 4,432 B，internal 历史最低 3,395 B，无错误、panic、abort、E:RX 或 Camera OOM。串口 SHA-256：`815eaa1c51a6f3884ec73ca3764fd820e169b454ad1c668c820b3e93ad111493`；证据位于 `.codex-temp/camera-dma-fail-retry-073c/`。

故障验证结束后已恢复普通 OFF 包 `20261010-121519` / `camera-dma-recovery-normal-074`，复用设备当前 immutable Recovery，保留 NVS、绑定和 OTA 状态；Recovery 校验、Recovery → Main、OTA confirmation、Home、WiFi/MQTT 均通过，主镜像 SHA-256：`73964f7763169bc4f9e476a91602e74d272a7afef16a64a8845f135686c9ae43`。普通 Camera 首帧和六阶段关闭通过，但第 1 轮 65 秒健康观察的最大连续 internal/DMA 仅 6,656 B，门禁判定 `NO_GO`，因此未把普通循环写成通过，也未启动长稳。设备当前保持普通 OFF 固件。发布状态继续 **NO_GO**：物理画质、任意 OOM 矩阵、异常并发、生产 readback/power-cut 和八小时资格仍未完成。

## 2026-10-10 Camera 同请求自动 OOM 重试 075 与普通恢复 076

074 的普通回归表明，将启动前 reserve 释放阈值全局提高到 8,192 B 会使首轮过早释放 Home return reserve，关闭后失去 reserve 释放带来的连续块合并，健康期 internal/DMA largest 降到 6,656 B。076 将普通启动阈值恢复为 4,096 B；若 Camera 启动实际返回 `Camera OOM` 或 `Not enough space` 且 reserve 仍存在，则在同一次 Camera 请求中释放 reserve 并自动重试一次。

故障包 `20261010-130731` / `camera-dma-auto-retry-075` 在同一次 Camera 请求内记录恰好一个首次 ring fault、一个 `Not enough space` 和一个自动重试 marker；重试选择 `selected=3840 actual=3840 half=1920 desc_half=1`，取得首帧、六阶段关闭并完成 65.094 秒健康观察。健康期 internal/DMA largest 为 8,192 B，internal 历史最低 3,283 B，Voice supervisor 最低剩余栈 4,432 B，无 error、panic、abort 或 `E:RX`。串口 SHA-256 为 `f0feb6818e9c2620cfb90089cb9e2bd1c036c82cf4b2ab1bd230941d598c068c`。

验证后已恢复普通 OFF 包 `20261010-131648` / `camera-dma-auto-retry-normal-076`，主镜像 SHA-256 `f0c39cdc47c7052190abc5b4193bead896bea7c13503e64f32ee6726d4940dd1`。包复用设备匹配的 immutable Recovery，保留 NVS、绑定与 OTA 状态，Recovery → Main → OTA confirmation → Home、WiFi/MQTT 通过。两轮普通 Camera → Home 均选择 6,144 B ring、取得首帧和六阶段关闭，并分别完成 65.078/65.031 秒健康观察；第二轮在 `largest=3968 required=4096` 时释放 reserve。全窗最低健康期 internal/DMA largest 为 8,192 B，internal 历史最低 3,307 B，Voice supervisor 最低剩余栈 4,432 B，无 fault marker、Camera OOM、error、panic、abort 或 `E:RX`。串口 SHA-256 为 `cca7f77ea32090090196488238ffd107d24be2c6cca8ad8c22d38570328c0cc2`。

075/076 关闭“用户必须退出再进 Camera 才能从首次 ring OOM 恢复”和 074 的普通路径余量回归，但不替代任意 OOM 矩阵、物理颜色/清晰度/电源轨、混合媒体/网络/音频并发、生产 readback/power-cut 或八小时资格。发布状态继续 **NO_GO**，当前不启动长稳。

## 2026-10-10 Camera + Photos 内部堆压力窗口

普通 OFF 076 上的 Camera + Photos/SD 严格重叠窗口成功扫描并初始化 41 张照片，Camera 保持
软件预览可见并完成 161 帧和六阶段关闭，但在 Photos 返回 Home 附近记录 2 次
`CameraService: Camera OOM`、6 次 `esp-aes: Failed to allocate memory`、6 次
`PEER_DEF: Write fail -84`，Voice `internal_min=127 B`。因此该窗口不是通过证据；它只说明
Camera 生命周期最终能关闭，不能推导任意 OOM 或媒体并发健康。原始串口 SHA-256 为
`48b61e1895103193797068af09f846c52d828829c127f0d90e1222013bddd0d3`，证据在 Rodak
`.codex-temp/resource-concurrency-029/current076-camera-photos-overlap/`。

Photos 原先为图库全部条目立即创建 LVGL tile，现已改成每页最多 6 个 tile，并在翻页时清理
旧控件、缩略图和 timer；原始照片索引保留。Host Debug 与 ASan/UBSan Photos 回归均 25/25
通过。修复后的 Camera + Photos 实机复测、视频期连续堆余量、任意 OOM 矩阵和八小时资格
门禁仍未完成，发布继续 **NO_GO**。

## 2026-10-10 Camera + Photos 后续 077–079

077 只加入 Photos 6 项分页，实机仍出现 3 次 Camera OOM、6 次 AES 分配失败和 6 次
`Write fail -84`，说明 LVGL tile 数量不是充分根因。078 进一步收敛 JPEG 峰值：worker 不再为
sequence 预复制完整帧；编码直接从锁内最新 strided RGB565 转入单个 RGB888 PSRAM 缓冲；输出
改为 96 KiB heap-caps scratch，再复制为实际 JPEG 长度。Camera capture 46/46 和 8 个附加
probe、Photos 25/25 均在 Debug 与 ASan/UBSan 通过。

078 同窗口中 Camera OOM 降为 0，Photos 初始化、Camera 持续可见、182 帧及六阶段关闭均完成；
但仍有 2 次 AES 分配失败、2 次 `Write fail -84`，Voice `internal_min=83 B`，停止后
internal/DMA largest 仅 6,144 B。串口 SHA-256 为
`a02c0865cb1d65175445a905bb7ff3d3505cbc414ebd927b85ebaf23eb2fbdfa`。

079 的“Photos 期间暂停 JPEG、Home 后延迟恢复”导致 WebRTC 会话关闭和 Camera 提前停止，且仍
有 1 次 AES 分配失败，已从源码撤销。设备最终恢复为与当前源码一致的 078 普通 OFF 镜像
`c2d8acfd1b556b13dd2527bd147a9ba21ad3b5fe99de7082cefd0ca6b3142c9e`，保留 NVS、绑定和
immutable Recovery。当前只关闭该窗口的 Camera OOM，AES/peer write、8,192 B 恢复和长稳仍
为 **NO_GO**。

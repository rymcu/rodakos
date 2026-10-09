# Camera STREAMOFF 阶段诊断

本 overlay 保留 Camera 停流阶段诊断，并在028加入 DVP worker 合作退出、可重试的分阶段 owner 释放和 PSRAM 栈；不修改超时。
`prepare_camera_teardown_patch.py` 校验两个 2.3.0 组件、完整依赖图、项目 manifest、
诊断 C ABI 头及 ESP-IDF 6.0.2 的相关源文件。源文件哈希按 LF 归一化。
依赖锁使用 IDF / host CI 已提供的 `ruamel.yaml` safe loader；与 `firmware_ci.py` 相同，
只排除跨平台重新解析会变化的顶层 `manifest_hash`，其余全部字段经排序、紧凑分隔的
canonical JSON（`ensure_ascii=True`）计算 SHA-256。包哈希、版本、来源、依赖边、
direct dependencies、target、锁格式版本及未知新增字段均不可漂移；嵌套同名字段不豁免。
生成文件位于 `build/rodak_patches/camera_teardown/`；CMake 要求每个组件恰好替换一个
源文件，managed components 保持原样。升级依赖必须重新审阅 provenance 和替换边界。

`common_video_stop` 在 sensor `S_STREAM`、controller stop、disable、del 前后各记一次。
`dvp_cam_ctlr_del` 在 task delete、GPIO disable、capture stop、GPIO ISR remove 前后记录；
其 DMA 清理在 GDMA disconnect、delete 前后记录。每次调用只求值一次，返回标记保存
原始错误码。阶段状态会保留已经成功的 owner 释放，失败后再次调用只重试未完成阶段，
不会重复释放已经断开的 GDMA、已删除的 task 或已移除的 GPIO。每个阶段只记录首次尝试，
避免一次失败重试耗尽固定诊断槽位。除下述 worker 删除合同外，原有早退、错误日志和硬件
释放顺序保持。

正常路径的驱动阶段顺序为：

```text
5 6 7 8 9 10 11 13 14 15 16 17 18 19 20 21 22 23 24 12
```

加服务层的 4 个标记共 24 个，低于一次开机固定 32 槽容量。诊断新增代码不创建日志、
堆对象、任务、TLS 或锁。两个 DVP 内部启动失败 DMA 清理调用传 `false`，只保留原清理；
controller del 的 staged owner release 直接执行 GDMA disconnect/delete 并传递错误。video
启动失败的内部 DMA 清理仍不写阶段记录。槽位不 reset、不环绕；每个 DVP 阶段只占用一对
首次尝试标记，但每次上层 ioctl 仍会追加服务层标记。必须结合诊断模块的 committed
sequence、pending 与 drop 标志解释，不能假设长时间多次停流后仍有完整记录。

以下间隙未单独标记：可选 `intf->stop` 在 sensor returned 与 controller stop enter 之间；
`cam_hal_deinit` 在 capture returned 与 GPIO remove enter 之间；末尾 heap/queue 释放在
GDMA returned 与 common del returned 之间。停留在某个 returned 标记后，不能归责于已返回
的操作。当前 ESP32-S3 配置没有启用 video byte-swap 的可选 stop。

`tests/camera_teardown_patch` 从受 pin 保护的生成源提取关闭函数及合作等待 helper 作为 C 编译，验证
调用次序、错误行为、24 标记预算、记录丢弃不影响驱动以及不返回时缺失 returned 标记。
生成器测试逐项拒绝输入漂移，检查 managed 不变、幂等、CRLF、输出目录、替换次数和实际
CMake source 替换；这些 host 证据不等于硬件停流恢复或实时性证明。最终固件仍需核验
compile commands、map/ELF 与实际设备阶段快照。

## 028 worker 合作退出与栈

真实 extended-DVP controller 增加 `shutdown_requested / worker_quiesced`，均由原内部
spinlock 保护。del 在任何锁、标记或日志之前拒绝空 handle、空 task 和同 task 删除；当前
Camera 调用来自 preview worker，但底层 callback 也可能同步调用 driver API，不能依赖
它永不 self-delete。

owner 设置退出请求后，向原3项 event queue 队首非阻塞发送固定 STOP 事件；队列满时已有
项足以唤醒 worker。worker 在 receive 前后复查请求，已经进入的 callback 或日志允许完成。
最后一次 controller 访问是在原锁下发布 quiesced 并释放该锁；之后只自 suspend，不再碰
controller、queue、callback 或日志。owner 拿同一锁确认后才调用
`vTaskDeleteWithCaps(handle)`，再进入原 GPIO/GDMA/ring/queue/controller 释放路径。
没有新增 cleanup task、任意硬超时或假 Stop 成功。

创建改用 `xTaskCreateWithCaps`，3072 B 栈仅申请 PSRAM|8BIT，TCB 和 ISR 可访问对象仍内部；
不 fallback 到 internal。默认 ring 配置仍为受 IDF 范围约束的8192（320×240 RGB565
当前几何生成约7680 B）；分配 overlay 在连续块不足时依次尝试6144和4096字节的帧对齐
ring，任务优先级23、queue 长度3保持。长稳失败时总DMA空闲仍有20–27 KiB但最大连续块
只有5–7 KiB；降级路径只是软件候选，吞吐、首帧和长稳仍需实机验证。WithCaps 自删除会另建 internal cleanup task，因此使用已有 owner 删除；普通
`vTaskDelete` 也不能替代它。相关 IDF WithCaps 实现、声明、Kconfig 和 heap 契约已加入 pins。

phase13 现在包含合作等待和 WithCaps 删除，phase14 仍在成功删除后；记录 ABI、固定536 B、
32槽、一次正常关闭24标记保持。新增 worker 函数本身不写诊断记录，不增加槽位压力。

当前 EXT_MEM stack 配置允许 PSRAM，但 cache disabled 时不可访问外部 stack。IRAM_ATTR
不能证明此条件；真实 flash/NVS/媒体并发仍需独立验证。此次保留既有 IRQ 清理顺序，没有
把 GPIO handler remove 视为跨核 in-flight ISR join，也不扩大构造时 GPIO 启用到 queue
创建之间的旧生命周期保证。合作退出避免主动删除仍持应用锁的 worker，不承诺所有外部阻塞
可恢复。

新[真实 worker host 目标](../../../tests/camera_worker_lifecycle/README.md)覆盖持日志锁的
确定性旧红/修复绿、队列竞态、最后锁释放、构造失败、self-delete 拒绝与 WithCaps 配对。

# Camera 关闭阶段诊断

021 在 Camera → Photos 时最后输出 `CloseStream: STREAMOFF begin`，之后串口静默
127.583 秒。仅凭这条日志，不能确定 `ioctl` 尚未返回：实际使用的
`esp_cam_sensor` 2.3.0 extended DVP 驱动会直接删除 worker，后续普通日志也可能阻塞。
022 增加独立于日志的阶段记录，用于下一次故障取证；没有改变关闭顺序或修复任务生命周期。

## 记录合同

`rodak_camera_teardown_diagnostics` 是启动内的固定 536 字节内部 DRAM 对象，包含
6 个 32 位头字段和 32 条记录。每条为 `phase / core / int32 status / commit_seq`。
一次成功关闭最多经过 24 个标记：服务层 ioctl 与完成日志前后、sensor 停流、controller
stop/disable/del，以及 DVP worker 删除、GPIO、capture stop 和 GDMA 清理边界。
完整 ABI 与阶段表见 [记录模块测试](../tests/camera_teardown_diagnostics/README.md)。

写入只尝试一次 strong CAS，不分配、不等待、不调用日志。竞争或容量耗尽时保留已有记录，
设置粘性 drop 标志；不重试、不循环覆盖、不提供在线 reset。每条 payload 写完后 release
发布 `commit_seq`，认领后被停止的 writer 可留下 pending 槽。在线读者必须使用 bounded
snapshot；调试器读取则须先确认两核停止。记录不跨设备复位保留。

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

## 取证与当前边界

先冻结已刷写包、源码、ELF、map 和校验 JSON 的 SHA-256，再从该 ELF 取得记录表地址。
正常复现期间不连接调试器；发生静默后记录串口/网络时间窗，再进行有界双核暂停和读取。
ESP32-S3 OpenOCD halt 会改变 watchdog 状态，普通 resume 不保证恢复它；调试干预后的
窗口不能用于证明 watchdog 或长稳健康。恢复与后续观测必须另开窗口。

当前 Windows MI02 虽显示 WINUSB/PnP 正常，却未注册 DeviceInterfaceGUID，OpenOCD
在 init 阶段返回 `LIBUSB_ERROR_NOT_FOUND`，尚未取得设备 DRAM/PC。只读诊断和官方
MI02 驱动候选修复已准备，安装需要管理员权限；没有据此推断设备 eFuse、panic 或死锁。
设备仍运行 021，022 的真实故障分段尚未取得。

软件验证分别覆盖记录发布协议、真实 C 驱动函数的原语义与漂移拒绝，以及生产
CameraService 在 ioctl 内和返回后日志处受控阻塞的区别。它们不关闭 Camera 退出、
媒体并发、资源归还或八小时 soak 门禁；发布仍为 **NO_GO**。

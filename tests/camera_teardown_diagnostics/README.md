# Camera 关闭阶段诊断

本目标编译真实的 `main/phone_os/camera-teardown-diagnostics.cc`。记录模块不依赖
FreeRTOS、日志或设备驱动；ESP 构建将数据放入内部 DRAM，将 recorder 放入 IRAM。
调用方传入阶段、当时采样的 core 和原始有符号返回值。模块不分配、不输出、不等待锁，
不修改 Camera 的释放顺序、超时或任务行为。

## 存储和发布边界

导出 C 符号 `rodak_camera_teardown_diagnostics` 为 536 字节，4 字节对齐：

| 字段 | 字节偏移 / 大小 |
| --- | --- |
| `magic` (`0x43544447`) | 0 / 4 |
| `version` (`1`) | 4 / 4 |
| `capacity` (`32`) | 8 / 4 |
| `next_index` | 12 / 4 |
| `contention_drop_seen` | 16 / 4 |
| `full_drop_seen` | 20 / 4 |
| `records[32]` | 24 / 512 |

每条为 `phase / core / int32 status / commit_seq`，偏移分别为 0 / 4 / 8 / 12，
总计 16 字节。`rodakos_camera_teardown_abi` 输出机器可读 JSON；生产文件的静态断言
在目标编译器上检查相同 `sizeof / offsetof` 和 32 位原子的 lock-free 性质。

每次 record 仅对 `next_index` 做一次 strong CAS。竞争失败返回 0，并置粘性
`contention_drop_seen`；容量耗尽返回 0，并置 `full_drop_seen`。成功后写入 payload，
最后 release-store `commit_seq = index + 1`，函数返回该序号。序号表示认领顺序，
不表示跨核严格时间或完成顺序。所有控制字段均用 32 位原子访问，payload 发布后不再改变。

本次启动内只追加，不环绕、不复用、不提供 reset。写者被删除或停在发布之前会留下
`commit_seq == 0` 的 pending 槽；其他写者仍可继续。数据不跨复位保留，不使用 RTC。
32 槽供一次完整关闭的最多 24 个关键点使用，后续关闭可能耗尽容量；不能在同一次启动中
依赖它完成无界循环诊断。

`rodak_camera_teardown_snapshot` 只扫描一次，acquire-load 到正确的 `commit_seq` 后
才读取 payload。pending / 开始边界以后的槽全部返回零，不等待写者；返回值按位组合：

- `PENDING`：本次扫描见到尚未发布的槽。
- `CHANGED`：开始/结束认领数量或丢弃标志不同。
- `DROPPED`：至少一个丢弃标志已置位；不提供精确丢失数量。
- `INVALID`：空目标指针、超容量计数或错误 commit 序号。

每条已提交记录完整一致；快照不是一个原子的全局时刻。pending 可在本次扫描后完成，
drop 也可在最终检查后发生。调试器应同时停止两核再读取全局符号，并保留 pending/drop；
不能把缺少某个阶段直接解释为执行未到达该点，也不能将复位后的空表视作失败现场。
在线 C/C++ 读者应使用 snapshot，不能直接普通读共享字段。

## 阶段编码

| 编码 | 阶段 |
| --- | --- |
| 1 / 2 | ioctl enter / returned |
| 3 / 4 | ioctl 后普通日志 before / after |
| 5 / 6 | sensor S_STREAM enter / returned |
| 7 / 8 | controller stop enter / returned |
| 9 / 10 | controller disable enter / returned |
| 11 / 12 | controller del enter / returned |
| 13 / 14 | DVP worker raw delete enter / returned |
| 15 / 16 | DVP GPIO disable enter / returned |
| 17 / 18 | DVP capture stop enter / returned |
| 19 / 20 | DVP GPIO handler remove enter / returned |
| 21 / 22 | DVP GDMA disconnect enter / returned |
| 23 / 24 | DVP GDMA channel delete enter / returned |

enter 和无返回值调用的 returned 使用 status 0；有返回值调用保留原始值。
这些常量不自动增加调用点；CameraService / 源码校验 overlay 的接线另行验证。
表内不记录 GPIO/GDMA IRQ 的注册核、每帧活动、硬件寄存器、输出锁 owner 或时间戳。
del 中未细分的 HAL deinit、buffer/queue free 等仍属于邻接阶段之间的区间。
IRAM recorder 不代表调用方的 PSRAM 栈可在 cache-disabled 条件下使用。

## Host 检查

12 项模块/ABI CTest 每项运行在新进程，避免向生产模块增加 reset API：

- 零状态和空指针、真实 C 编译/链接 ABI、24 阶段顺序及 `INT32_MIN/MAX` 状态。
- 32 槽耗尽后不覆盖、未发布部分 payload 不泄漏、损坏计数/commit 的有界读取。
- 8 个并发写者和实时 snapshot 的完整字段、唯一序号、粘性丢弃标志。
- 确定性交错：竞争者抢先完成使 CAS 失败；写者认领后暂停时另一写者继续；扫描中新增记录。
- ABI JSON 的大小、偏移和常量检查。

常规与多线程测试使用未替换原子操作的真实模块。另一个 host-only object 通过编译器
`-include atomic_interleaving.h` 将两种原子 intrinsic 转交调度 hook，以固定上述三个
交错点；hook 最终仍执行真实原子操作。该 object 不进入固件，生产源码无测试分支。
这些检查验证记录协议，不证明真实 Camera/驱动释放流程或串口静默原因。

另一个 CTest 运行 `test_linked_check.py` 的 18 项 Python 检查，覆盖最终 ELF 门禁的
正例，以及错误地址/布局、PT_LOAD 不一致、重复/弱符号、调用/回跳/未知指令、错误 CAS
数量、反汇编字节不一致、外部 RAM literal、真实 objdump literal 注释及审计期间输入变化
等负例。当前共 13 项 CTest；Python 使用结构化 ELF/反汇编 fixture，不冒充实际目标编译。

```bash
cmake -S tests/camera_teardown_diagnostics \
  -B "$HOME/.cache/rodakos-camera-teardown-debug" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$HOME/.cache/rodakos-camera-teardown-debug" -j 2
ctest --test-dir "$HOME/.cache/rodakos-camera-teardown-debug" --output-on-failure

cmake -S tests/camera_teardown_diagnostics \
  -B "$HOME/.cache/rodakos-camera-teardown-asan" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  '-DCMAKE_EXE_LINKER_FLAGS=-no-pie'
cmake --build "$HOME/.cache/rodakos-camera-teardown-asan" -j 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir "$HOME/.cache/rodakos-camera-teardown-asan" --output-on-failure
```

ThreadSanitizer 使用独立目录，将上述 C/CXX flags 的 `address,undefined` 改为 `thread`。
当前 Debian/WSL 上直接运行曾在 sanitizer 初始化报 `unexpected memory mapping`，
部分进程甚至尚未进入测试即退出；仅对测试进程使用 `setarch x86_64 -R` 后 12 项均通过：

```bash
TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R \
  ctest --test-dir "$HOME/.cache/rodakos-camera-teardown-tsan" --output-on-failure
```

固件验证还须检查同次最终 ELF 中符号位于内部 DRAM、大小 536 字节，recorder 无
libatomic/heap/stdio 调用或重试循环。独立对象或 host ABI 检查不能替代该链接门禁，
也不证明板上 cache、调度、JTAG 读取或实际多核暂停成功。

IDF 6.0.2 的 `CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND` 为 S3 + PSRAM 全局添加
`-mdisable-hardware-atomics`。其 `esp_libc/priv_include/esp_stdatomic.h` 对非外部 RAM
地址使用 native S32C1I helper，`esp_libc/CMakeLists.txt` 只为该 helper 文件添加
`-mno-disable-hardware-atomics`。本模块的 CMake 同样仅为诊断 TU 增加后一个选项：
所有原子地址均为固定 `DRAM_ATTR` 对象，其他代码和 PSRAM 原子操作保持 IDF 策略。
静态 lock-free 断言仍保留；缺少单 TU 选项会在真实构建中失败。

`tools/check_camera_teardown_diagnostics.py` 对最终 ELF 做只读门禁；检查初始化数据与
PT_LOAD、536 字节内部 DRAM 对象、IRAM recorder、唯一原生 CAS、内部 RAM literal、
完整可达控制流，拒绝调用、间接跳转、回跳、hardware loop 和未知指令。函数以外的驱动、
IRQ 和其他库不在该检查范围内。`cmake/camera_teardown_diagnostics.cmake` 将源码、头、
检查器及其共享 ELF 解析器列入链接依赖，链接后生成 `build/camera-teardown-linked.json`。

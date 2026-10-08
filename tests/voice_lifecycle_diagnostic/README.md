# 032 USB 语音生命周期诊断宿主验证

此目标编译完整生产 `voice_lifecycle_diagnostic.cc`。service、任务名查询、资源统计和日志
输出是可控依赖；单槽状态机、命令解析、三阶段观测、恢复与结果判定均使用生产实现。

```sh
cmake -S tests/voice_lifecycle_diagnostic -B /path/to/host-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build /path/to/host-debug -j4
ctest --test-dir /path/to/host-debug --output-on-failure
```

14 项行为用例覆盖严格 ID、重复／旧 ID、受理与执行两次 busy 检查、拒绝时无任何 voice
读取／Deinit／Start、输出阻塞期间的 accepting 槽、并行受理、执行唯一领取、busy 回调和
输出回调重入、USB 变更阻断、三任务 Listening 与两种 idle 的分类、停止残留仍只恢复一次、
Start 返回 true 但实际恢复条件不满足，以及 Deinit 后不调用会初始化 Wake 的观测接口。

另一项 CTest 调用真实 JSON 输出函数，核对阶段／同一 uint32 ID／任务布尔值；并以 `nm`
确认未定义 `RODAKOS_RELEASE_TESTS` 的生产 TU 没有任何诊断符号。固件接线由 main 的
同名宏隔离，CMake 仅在该选项为 ON 时附加源文件。此 host 结果不代替完整 IDF 构建或
硬件三个任务真实退出；031 原服务回归仍是各自的独立软件证据。

ASan／UBSan／leak 用独立构建目录，附加：

```text
-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie"
-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined -no-pie"
```

执行时设置 `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`、`UBSAN_OPTIONS=halt_on_error=1`。

## 固件命令合同

默认 `RODAKOS_RELEASE_TESTS=OFF`；生产固件没有本入口、dispatcher 或 main Pump。
测试构建必须显式 `-DRODAKOS_RELEASE_TESTS=ON`，并将该标志连同源码／制品身份写入
测试包记录。复用此标志也会启用原有 `fail_alloc` 入口，不能称为普通生产包。

```text
RODAK_RELEASE_TEST_V1 voice_cycle 1
```

ID 为 `1..4294967295` 的规范十进制，无符号、前导零、额外空白或尾随字符。每 boot
只接受高于最后 accepted ID 的请求，不回绕。busy／语法拒绝不消耗 ID；已 accepted 后
即使执行前因媒体忙而拒绝，该 ID 仍消耗。重复请求没有幂等重放，也没有自动重试。

接收任务将请求放入一个固定槽，永久 internal 栈 main 任务领取执行，不增加 worker。
`accepting / pending / executing` 都算忙；只有 accepted 输出完成后才允许 main 领取。
同一请求所有日志使用 `RODAK_VOICE_CYCLE` 前缀和同一 `id`。正常顺序：

1. `accepted`：`result=queued`，只表示入队。
2. `before`：任务名存在性、assistant phase/stopping、uptime 和 heap 快照。
3. `after`：Wake Deinit 已返回；三个任务名都不存在时 `stopped=true`。
4. `recovered`：已进行一次 Wake Start；同时检查实际任务、监听状态和 enabled 偏好。
5. `complete`：`three_task_pass / idle_cycle_pass / disabled_idle_cycle_pass / failed`。

busy 的受理拒绝为 `phase=rejected/result=rejected`；已入队后发现 busy 则直接输出
`phase=complete/result=rejected`，不执行 Deinit、Start 或读取 Wake 状态。无队列成功
响应的请求不能期待后续完成。`id=0` 只表示无有效 ID 的语法拒绝。

`three_task_pass` 要求 before 为 enabled=true、assistant Listening 且非 stopping、
assistant_io／voice_frontend／voice_wake 三者存在。enabled 空闲循环要求 before 无
assistant 且 capture／supervisor 存在，独立报 `idle_cycle_pass`；disabled 空闲循环要求
before 仅 supervisor 存在，报 `disabled_idle_cycle_pass`。不完整／其他阶段的 before
即使退出并恢复成功，也不能算这三类通过。

恢复不自动打开新的 assistant 会话。enabled=true 时要求 supervisor／capture 存在、
assistant 不存在、listening=true；enabled=false 时只应有 supervisor，listening=false。
还必须满足 Start 返回 true、enabled 前后相同。停止残留或恢复失败只报告失败，不重复
调用 Start、SetEnabled、重新配对或复位。诊断不改 NVS enabled 偏好；既有身份 Init／到期
协调仍可能进行原有身份记录写入，不能把这个合同扩大为“全部 NVS 零写入”。

busy 覆盖 OTA、local Camera preview、Camera／Display stream、独立录音、音频播放器
loading／playing／paused（包括 Music）和非 voice-assistant 音频焦点。正在进行的 voice
会话是目标，不能作为 busy 直接拒绝。Music library scan 不单独列为 busy。USB 操作期间
拒绝 voice/audio/AEC 变更、配网、应用启动和 fail_alloc；只保留 AEC status/read 查询。
该门禁是两次资源状态快照和串口写入串行化，不是 UI／MQTT 全局准入锁；硬件采集期间
应保持其他操控安静，不能宣称媒体并发仲裁已经验证。

`after` 的任务观测只使用 `xTaskGetHandle(name) != nullptr`；从不保存并读取已删除
handle 的状态，也不在该阶段调用 Wake GetState／IsEnabled。Deinit 与 Start 都在控制器
mutex 和 UI／serial／服务锁外调用。同步 Join 没有硬完成期限；缺少 complete 不能视为
成功或触发自动重发。heap free／largest／minimum 仅是同 boot 辅助观测，不证明净内存
节省或所有物理资源归还。真实 SDK 调度、回调、音频／TLS、硬件 OOM 和长稳仍需单独验证。

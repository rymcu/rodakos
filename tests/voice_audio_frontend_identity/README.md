# VoiceAudioFrontend 身份恢复与任务回收宿主回归

此目标直接编译生产 `main/phone_os/voice_audio_frontend.cc`，使用生产
`VoiceAudioFrontend::ConfigureWakeWord`、`Init`、`StartListening` 和
`ProcessWakeSamples`，以及完整 CaptureTask。MultiNet 的闭源声学计算、AFE、调度器与
`AudioCodecInput` 使用可控宿主适配；MultiNet 命令表直接编译固定 ESP-SR 2.2.2 的
完整 `esp_mn_speech_commands.c` 和真实接口头。任务回收复用 `tests/task_retirement` 的生产
retirement TU 和固定 ESP-IDF 6.0.2 的完整 WithCaps 创建/删除函数。
测试验证失败的命令清理、注册和 update 入口都会释放旧 MultiNet graph，
使 Start 与检测保持封锁；成功 Configure 后由生产 Init 路径重建 graph 才能再次监听。

对话录音用例实际调用生产 `Start`/`StartAfe`，随后注入唤醒配置失败，确认 AFE data、
模型目录、对话模式和运行状态保持不变，避免唤醒 graph 恢复破坏正在进行的录音。
`LastErrorSnapshot` 用例并发读取生产锁内错误副本。模型/声学、麦克风和 FreeRTOS
均为 host fake，不证明真实 MultiNet 识别、声学质量、NVS 或硬件行为。

031 回收用例实际运行 `voice_frontend`、`afe_fetch` 与 internal 栈的 `wake_notify`
宿主线程，喂入至少一个 MR chunk。测试在 AFE feed 记录生产局部 vector 的实际地址，
仅包装链接器的 C++ delete 符号以观察释放，仍调用原始 delete；在 Capture 外部任务
删除前断言 vector 已释放。并发 Deinit 必须等到外部回收完成，普通 Deinit 之后仍可
Init；还覆盖 create 内提前调度、失败创建保留旧票据、回收池耗尽时清理已创建通知任务，
以及通知 callback 重入 Deinit。普通 mutex 与 recursive mutex 分别模拟，不以递归锁
掩盖生命周期锁死锁。

旧 fake 的 self-delete 直接 return、固定 ReadForOwner=false、通知任务不运行等行为
已移除。负控冻结并编译 `34c9e6453344b8eb7896ab5476ef222504c12a94` 的完整旧 frontend
TU 与同提交配套头文件；显式 include override 保证类布局也使用旧版，legacy 编译宏仅
隔离访问新回收字段的绿色测试，保留公共 Start/Deinit、真实 feed 与释放观察探针：
必须先实际 AFE feed，再由真实 IDF self-delete 因清理任务创建拒绝而 SIGABRT，且不得
出现 vector 释放标志。第二个负控只跳过局部 vector 析构，必须精确触发回收前释放
断言；编译失败、超时、任意非零退出均不作为成功负控。

```powershell
wsl -d Debian -- bash -lc '
  cmake -S /mnt/d/workspace/rodakos/tests/voice_audio_frontend_identity \
        -B ~/.cache/rodakos-voice-audio-frontend-identity -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DRODAKOS_IDF_PATH=/mnt/c/esp/v6.0.2/esp-idf &&
  cmake --build ~/.cache/rodakos-voice-audio-frontend-identity &&
  ctest --test-dir ~/.cache/rodakos-voice-audio-frontend-identity --output-on-failure
'
```

CTest 名称为 `rodakos_voice_audio_frontend_identity` 与
`voice_frontend_retirement_negative_controls`、`voice_frontend_multinet_negative_controls`、
`voice_frontend_afe_negative_controls`、`voice_frontend_observation_negative_controls`。可执行文件为
`rodakos_voice_audio_frontend_identity_tests`，接受一个完整测试名称作为过滤参数。
负控报告存于构建目录的 `negative-controls/results.json`。ASan/UBSan 使用独立构建目录，
编译与链接增加 `-fsanitize=address,undefined`，设置
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`；可传
`-DRODAK_FRONTEND_NEGATIVE_CHILD=ON` 只运行绿色宿主用例。

这些用例不验证目标芯片调度延迟、物理输入关闭、声学结果或所有 heap 全面回收。
AFE fetch 仍使用原有外部删除顺序；wake_notify 保留 internal 栈与原有 callback 策略。
033 的 fetch 取消分类、feed 排空与真实 PCM 连续性，以及 034 的完整输出字节账本、
停滞诊断和有限自动重同步见 [AFE 生命周期回归](afe-lifecycle.md)。主目标当前共 49 项；
`RODAK_FRONTEND_DEVICE_AEC=OFF` 独立构建用命名格式用例验证关闭 AEC 的 160 样本输入合同。

## 035 AFE 阶段与输出等待观测

034 的 `feeds=124 / returns=123` 只证明一次 feed 已准入、返回尚未记账，
不能区分调用前抢占、SDK 调用内部或返回后的业务锁等待，也不能指定 TLS/DSP 根因。
035 在普通与 `RODAKOS_RELEASE_TESTS` 固件保留同一轻量观测实现；100ms 阈值、
完整输出 credit、取消排空、保守库存、epoch/raw gap、reset 与 terminal 行为不变。

`voice_afe_observation.h` 的 56B 固定元组由 Capture 单一写者通过独立 `portMUX` 发布。
时间先在临界区外读取，临界区只有有界字段更新/56B 快照复制，没有 timer、业务锁、
SDK、日志或动态分配。SDK 返回后先发布 `returned_wait_publish`，再获取 frontend mutex；
因此等待该业务锁时，返回边界仍可独立观察。生命周期不写/reset 该元组，晚到的旧 raw read
保留旧 generation/epoch；消费者将不匹配元组标为 `scope=stale`，不把它当作当前阶段。

| 阶段 | 仅能说明的边界 |
| --- | --- |
| `input_open` / `raw_read` | 输入打开或 `ReadForOwner` 调用边界；仍含输入锁、调度与 codec，不是纯 I2S 时间 |
| `read_returned` / `read_failed` | raw 返回及读取墙钟耗时；成功返回后还会做选麦处理 |
| `feed_admission` / `prepare_feed` | 准入锁等待或本地 MR 拼接准备 |
| `feed_admitted` / `api_boundary` | 已准入或已发布 SDK 调用边界；标记到真正调用之间仍可能被抢占 |
| `returned_wait_publish` | SDK 已返回，尚未完成本次返回记账；`previous_us` 是 API 边界墙钟耗时 |
| `credit_published` | 返回校验/记账已完成；0、非法返回或 overflow 时不代表 credit 增加；`previous_us` 是 return-to-publish 全段墙钟 |
| `between_reads` | 本轮处理结束，尚未发布下一输入边界 |

`seq` 在 raw/准备阶段是 read 序号，在 feed/API/记账阶段是 feed 序号；`detail`
按阶段为样本/尾部数量或 SDK 返回字节。序号只用于关联，不用于业务授权或时间差计算。
read、API、return-to-publish 的最大墙钟耗时和对应序号属于该 **producer generation/epoch**，
后来的短操作不会覆盖最大值。它们可能早于当前输出 gap，不证明该 gap 的唯一原因。
所有原始时点为锁保护的 `int64_t`；压缩到 `uint32_t` 的 `*_us` 在 `UINT32_MAX`
饱和，表示“至少 4,294,967,295µs”，不是精确值或微秒计数回绕。

每次原有 W 保留独立 `count`，并携带以首个 W 序号命名的稳定 `gap_id`。失败 fetch 后
再次达到 100ms 仍会增加 W/count；同一个 gap 延续到同 generation/epoch 的首个有效完整
输出，或以 `cancelled` / `resynced` 关闭。闭合记录列出 first/last stall 与 warnings，
不会让新一代输出关闭旧 gap。等待起点是首次不足 credit 的消费者观察，elapsed 是观察
区间，不是测得的单次 raw/SDK 阻塞时长。

消费者记录相邻观察和业务锁调用的墙钟间隔，保留首 W 之前、首 W 之后及 fetch 返回后
再次取锁的最大值。间隔包含 SDK、日志、delay 与调度，不能直接称为调度延迟或 mutex
竞争时长。stall 与 gap closed 均在业务锁内复制 producer 元组，再在 diag mux 外采时，
随后释放业务锁输出冻结值；stageAge 不复用更早的 fetch-return 时刻。原有 AFE feed/reset/
first-fetch/first-output 相关日志也移到业务锁外。日志阻塞期间实际阶段可能继续推进，
打印内容仍是带原 generation/epoch/count 的观测快照。

`afe_observation_test.cc` 的 14 项新用例运行完整生产 TU。仅该测试 TU 使用
`-fno-access-control` 读取私有 mutex、计数和诊断快照；生产 TU 正常编译，没有
`#define private public` 或生产测试回调。这是内部边界合同测试，不是纯公共 API 黑盒测试。
host-only semaphore/critical-entry/log 门闩和可推进的单调时钟控制交错，覆盖 raw、
API 前、API 内、SDK 返回后记账锁、消费者延迟、跨代 stale、resync、同 gap 多次 W、
快照采时顺序，以及堵日志时 producer 实际进入第二次 SDK 调用。元组并发测试要求
读者实际观察两类不同阶段，另外核对 scope/sequence 回绕与饱和。

`run_observation_negative.py` 编译完整变体 TU：把返回标记移到业务锁之后，必须由
`returned_visible` 断言拒绝；把 stall 日志移回业务锁内，必须由 `producer_progressed`
断言拒绝。两者先确认实际门闩边界，日志负控先解除门闩再报告断言失败；编译失败、
超时或任意异常退出都不算检出。旧 2 项 retirement、2 项 MultiNet、8 项 AFE 负控
仍保留，旧头变体只关闭不兼容的新观测测试文件。

固定 034 OFF 编译参数的隔离目标检查记录：实例 576→640B（+64B，包括 8B mux），
Capture 固定帧 176→240B，Fetch 112→368B；gap/producer 日志 helper 分别 128/112B。
这些是对象与固定帧成本，不是完整调用栈或实测 HWM，也不能从既有水位推算新余量。
正常路径每个 conversation read 有 6 次 Publish、每次 feed 另有 4 次；诊断确有采时和
短临界区成本，不能称“仅告警时有开销”。正式包仍须独立核对最终 ELF 与设备窗口。
host fake 不执行闭源 DSP、FreeRTOS 真调度、物理 I2S 或声学处理，不给出 034 真机根因。

## 033 MultiNet 命令表所有权

当前嵌入模型仅有 `mn5q8_cn`。固定 ESP-SR 2.2.2 的 ESP32-S3 `libmultinet.a` 中，
`multinet5_quantized8.c.obj` 接口表的 create/destroy 槽分别指向 `model_init` / `model_destroy`。
create 正常返回之前内部调用 `esp_mn_commands_alloc`；destroy 内部调用
`esp_mn_commands_free`，真实命令表的 free 无条件先 clear，再将全局 root 和模型关联置空。
因此前端只持有模型，不能额外 alloc/free 命令表，也不能通过交换 free/destroy 顺序修复。
生产入口在创建前拒绝未经核验的其他模型名；SDK 或模型升级需重新核验所有权。

`mn_registry.c` 直接包含 managed component 的完整源，不复制清理实现；
`mn_runtime.cc` 仅模拟不能在宿主执行的模型 create/destroy 合同，以及声学结果。
真实 SDK 的命令检查、链表存储、模型关联和 update 路径参与用例。
链接器 wrapper 仅观察跨 TU 的 alloc/free 调用并注入 frontend clear 失败；SDK 内部
因重复 alloc 而触发的隐含 free 不计入边界 frees，另由 `reallocations` 观察。
测试不依赖是否打印日志判断正确性。

正控覆盖：Init → Configure → Deinit → Init；重复 Deinit；已取得模型后的首次 add、update、
非法 chunk 失败；运行期 clear/add/update 失败；未取得模型句柄时不做调用者清理；拒绝其他
模型。每条释放路径断言真实 registry 的存在、模型关联与边界释放计数。
`SetModelCreateResult(false)` 只验证调用者收到 nullptr 的行为，不模拟 SDK 内部任意 OOM。

`run_mn_negative.py` 冻结并编译 `e6edadcd8d72ea8300c8afe3b0ccc1bb359eb80c` 的完整 frontend
TU 和配套头，分别运行两个公共 Init/Deinit 探针：必须观察到 `allocations=2 / reallocations=1`
及 `frees=2 / empty_frees=1`，并由对应断言以退出码 1 拒绝。编译失败、超时、任意异常退出、
仅出现日志均不算检出。报告在 `mn-negative-controls/results.json`。

CMake 校验以下 SDK 输入 SHA-256，变化时拒绝继续套用旧合同：

| 输入 | SHA-256 |
| --- | --- |
| `src/esp_mn_speech_commands.c` | `57c14f35dc3107db84d038f9e16d560831106698280895d7bae73a7a3cfd7885` |
| `src/include/esp_mn_speech_commands.h` | `26c86a0195cc24f3e0e8bc26ac9fc346a06cb01828d8d22c8066c60dd4f7b342` |
| `include/esp32s3/esp_mn_iface.h` | `74f031979ac525cfee66e088cc9c856052727d562489b64e3bee6be3b5e05349` |
| `lib/esp32s3/libmultinet.a` | `92431596a614dcc2671a8dd36231f4f4fef5f793e232601ee178134759aa910b` |

边界：真实 SDK 的 alloc 在 root 分配失败时仍返回 ESP_OK，create 也未检查该返回值；
其部分初始化失败分支还在 alloc 前调用内部 destroy，并非所有内存不足路径都能安全返回。
本修复消除应用层成功 create / 已取得模型后的重复所有权，不宣称修复 SDK OOM、其他模型、
硬件声学或设备资源余量。硬件 Deinit 周期仍需独立验证。

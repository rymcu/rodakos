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
`voice_frontend_afe_negative_controls`。可执行文件为
`rodakos_voice_audio_frontend_identity_tests`，接受一个完整测试名称作为过滤参数。
负控报告存于构建目录的 `negative-controls/results.json`。ASan/UBSan 使用独立构建目录，
编译与链接增加 `-fsanitize=address,undefined`，设置
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`；可传
`-DRODAK_FRONTEND_NEGATIVE_CHILD=ON` 只运行绿色宿主用例。

这些用例不验证目标芯片调度延迟、物理输入关闭、声学结果或所有 heap 全面回收。
AFE fetch 仍使用原有外部删除顺序；wake_notify 保留 internal 栈与原有 callback 策略。
033 的 fetch 取消分类、feed 排空与真实 PCM 连续性，以及 034 的完整输出字节账本、
停滞诊断和有限自动重同步见 [AFE 生命周期回归](afe-lifecycle.md)。主目标当前共 35 项；
`RODAK_FRONTEND_DEVICE_AEC=OFF` 独立构建用命名格式用例验证关闭 AEC 的 160 样本输入合同。

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

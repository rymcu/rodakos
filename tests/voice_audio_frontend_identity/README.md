# VoiceAudioFrontend 身份恢复与任务回收宿主回归

此目标直接编译生产 `main/phone_os/voice_audio_frontend.cc`，使用生产
`VoiceAudioFrontend::ConfigureWakeWord`、`Init`、`StartListening` 和
`ProcessWakeSamples`，以及完整 CaptureTask。MultiNet、AFE、调度器与
`AudioCodecInput` 使用可控宿主 fake；任务回收复用 `tests/task_retirement` 的生产
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
`voice_frontend_retirement_negative_controls`。可执行文件为
`rodakos_voice_audio_frontend_identity_tests`，接受一个完整测试名称作为过滤参数。
负控报告存于构建目录的 `negative-controls/results.json`。ASan/UBSan 使用独立构建目录，
编译与链接增加 `-fsanitize=address,undefined`，设置
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`；可传
`-DRODAK_FRONTEND_NEGATIVE_CHILD=ON` 只运行绿色宿主用例。

这些用例不验证目标芯片调度延迟、物理输入关闭、声学结果或所有 heap 全面回收。
AFE fetch 仍使用原有外部删除顺序；wake_notify 保留 internal 栈与原有 callback 策略。

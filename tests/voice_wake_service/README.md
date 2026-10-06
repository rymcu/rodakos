# VoiceWakeService 宿主回归

此目标编译生产 `VoiceWakeService`、`voice_wake_settings`、`voice_identity`、
`voice_identity_record`、`time_service` 与 managed cJSON。唤醒 runtime、语音会话、
FreeRTOS、SNTP 和 Settings 存储由宿主 fake 提供；身份事务、序列化、期限判断、
补偿及状态快照均执行生产实现。

```powershell
wsl -d Debian -- bash -lc '
  cmake -S /mnt/d/workspace/rodakos/tests/voice_wake_service \
        -B ~/.cache/rodakos-voice-wake-service -G Ninja -DCMAKE_BUILD_TYPE=Debug &&
  cmake --build ~/.cache/rodakos-voice-wake-service &&
  ctest --test-dir ~/.cache/rodakos-voice-wake-service --output-on-failure
'
```

CTest 名称为 `rodakos_voice_wake_service`，超时 40 秒。可执行文件为
`rodakos_voice_wake_service_tests`。ASan/UBSan 使用独立构建目录，并增加：

```text
-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
```

运行 CTest 时设置 `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`。

25 项用例覆盖：

- 禁用且未配置时的 Init/GetState/Start 不加载唤醒 runtime、不启动监听；显式应用后才确认身份。
- 完整身份记录的持久应用、相同 revision 幂等、冲突与旧 revision 拒绝、过期后保留水位。
- 可信 Unix 时间与单调 elapsed 期限，时间前跳、回拨、失步、重启后等待可信时间，以及禁用时过期。
- 运行时配置失败、已知未写入和不确定写入、迁移失败锁定、监听恢复失败、存储补偿失败。
- 配置途中到期、旧临时身份在失败补偿期间到期，以及过期恢复失败时停止监听并撤销确认。
- 并发 Apply/GetState 的完整事务序列化，Stop/禁用/身份变更使旧 wake callback 失效；
  barrier 覆盖读取 assistant 状态期间失效的 callback，不允许其打断后续 TTS。
- 生产 Unix 适配器单次 `gettimeofday` 快照与既有 2020 年有效时间下限。
- runtime 错误并发变化时，service 通过拥有字符串内容的快照读取完整错误。

故障存储可以区分写调用失败但旧值未变、写后 commit 失败和补偿写失败；这些是软件故障注入，
不证明真实 NVS 掉电原子性或闪存可靠性。此目标也不证明 MultiNet 图构建、麦克风声学识别、
真实 TTS 打断或固件硬件验收。实际 frontend 的宿主检查见
[`tests/voice_audio_frontend_identity`](../voice_audio_frontend_identity/README.md)，
真实 MQTT 与 Wake service 的连接检查见
[`tests/voice_identity_integration`](../voice_identity_integration/README.md)。

错误快照修复仅覆盖 `VoiceWakeRuntime` / `VoiceWakeService` 调用链；保留的
`VoiceRecorderService::last_error()` 及其消费者仍使用旧指针接口，不能据此宣称全域并发安全。

集成目标可复用本目录的 fake headers，但应使用自己的 RTOS runtime，不同时链接本目录的
`host_runtime.cc`；该文件为独立目标实现 task 生命周期及 Linux `--wrap=gettimeofday`。

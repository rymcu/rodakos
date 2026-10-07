# VoiceWakeService 宿主回归

此目标编译生产 `VoiceWakeService`、`voice_wake_settings`、`voice_identity`、
`voice_identity_record`、`time_service` 与 managed cJSON。唤醒 runtime、语音会话、
SNTP 和 Settings 存储由宿主 fake 提供；身份事务、序列化、期限判断、
补偿及状态快照均执行生产实现。supervisor 复用 `tests/task_retirement`，编译真实
ESP-IDF 6.0.2 WithCaps 创建／外部回收源码和完整生产 `task-retirement.cc`；宿主内核模型
记录真实栈／TCB 配对分配、跨核收敛和外部删除，不把自删伪造成正常 C++ 返回。

```powershell
wsl -d Debian -- bash -lc '
  cmake -S /mnt/d/workspace/rodakos/tests/voice_wake_service \
        -B ~/.cache/rodakos-voice-wake-service -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DRODAKOS_IDF_PATH=/mnt/c/esp/v6.0.2/esp-idf &&
  cmake --build ~/.cache/rodakos-voice-wake-service &&
  ctest --test-dir ~/.cache/rodakos-voice-wake-service --output-on-failure
'
```

CTest 名称为 `rodakos_voice_wake_service`，超时 120 秒。可执行文件为
`rodakos_voice_wake_service_tests`。ASan/UBSan 使用独立构建目录，并增加：

```text
-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie"
-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie"
-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined -no-pie"
```

运行 CTest 时设置 `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`。

35 项用例覆盖：

- 禁用且未配置时的 Init/GetState/Start 不加载唤醒 runtime、不启动监听；显式应用后才确认身份。
- 完整身份记录的持久应用、相同 revision 幂等、冲突与旧 revision 拒绝、过期后保留水位。
- 可信 Unix 时间与单调 elapsed 期限，时间前跳、回拨、失步、重启后等待可信时间，以及禁用时过期。
- 运行时配置失败、已知未写入和不确定写入、迁移失败锁定、监听恢复失败、存储补偿失败。
- 配置途中到期、旧临时身份在失败补偿期间到期，以及过期恢复失败时停止监听并撤销确认。
- 并发 Apply/GetState 的完整事务序列化，Stop/禁用/身份变更使旧 wake callback 失效；
  barrier 覆盖读取 assistant 状态期间失效的 callback，不允许其打断后续 TTS。
- 生产 Unix 适配器单次 `gettimeofday` 快照与既有 2020 年有效时间下限。
- runtime 错误并发变化时，service 通过拥有字符串内容的快照读取完整错误。
- supervisor Stop／Deinit／析构等待完整回收，退出不创建 cleanup task、不新增回收分配；
  create-before-publish、创建与栈分配失败取消、普通 Deinit 后重新 Start。
- 禁用监听仍保留 supervisor 处理身份到期；disable 途中 Stop／Deinit 合并为同一停止操作。
- 双 Stop 和跨核收敛、Stop／Deinit 旧代等待不追随后来的 replacement；解锁后的 assistant
  stop 回调重入不会 self-join，self Stop 后旧 body 尚未返回时拒绝复活；失败 replacement
  仍保留旧代票据供 late Stop 等待。

`rodakos_voice_wake_legacy_negative` 冻结 `cbb6c987` 完整旧 `.cc/.h`（与 `34c9e645`
的 wake 源相同），构建后只运行 Stop 回收场景。仅拒绝 IDF 指定名称的 cleanup task 创建，
必须观察两个 IDF 分支 marker 与 SIGABRT；编译失败、超时和无关 teardown 失败不算红例。
生成的 `production-sources.json`、`legacy-negative/result.json` 记录新旧源码指纹。

业务 mutex 下的 runtime、identity clock、assistant GetState 回调仍沿用原有不可重入边界；
上述重入覆盖仅针对已经解锁的 assistant StopInteraction 路径，不宣称任意 callback 重入安全。

故障存储可以区分写调用失败但旧值未变、写后 commit 失败和补偿写失败；这些是软件故障注入，
不证明真实 NVS 掉电原子性或闪存可靠性。此目标也不证明 MultiNet 图构建、麦克风声学识别、
真实 TTS 打断或固件硬件验收。实际 frontend 的宿主检查见
[`tests/voice_audio_frontend_identity`](../voice_audio_frontend_identity/README.md)，
真实 MQTT 与 Wake service 的连接检查见
[`tests/voice_identity_integration`](../voice_identity_integration/README.md)。

错误快照修复仅覆盖 `VoiceWakeRuntime` / `VoiceWakeService` 调用链；保留的
`VoiceRecorderService::last_error()` 及其消费者仍使用旧指针接口，不能据此宣称全域并发安全。

集成目标可复用 `fakes`，不同时链接本目录的 `host_runtime.cc`（仅 Unix 包装和共享 join）。
独立目标优先使用 `retirement_fakes` 的非递归业务 mutex、真实 ticks 和统一 WithCaps 类型。
identity integration 显式开启 `RODAK_IDENTITY_WITH_RETIREMENT`，普通 MQTT worker 保留
独立符号模型，wake WithCaps 仍链接同一生产回收实现；默认 MQTT fixture 不开启此兼容分支。

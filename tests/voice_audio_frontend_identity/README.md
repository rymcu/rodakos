# VoiceAudioFrontend 身份恢复宿主回归

此目标直接编译生产 `main/phone_os/voice_audio_frontend.cc`，使用生产
`VoiceAudioFrontend::ConfigureWakeWord`、`Init`、`StartListening` 和
`ProcessWakeSamples`，只将 MultiNet、AFE、FreeRTOS 与 `AudioCodecInput` 替换为可控
宿主 fake。测试验证失败的命令清理、注册和 update 入口都会释放旧 MultiNet graph，
使 Start 与检测保持封锁；成功 Configure 后由生产 Init 路径重建 graph 才能再次监听。

对话录音用例实际调用生产 `Start`/`StartAfe`，随后注入唤醒配置失败，确认 AFE data、
模型目录、对话模式和运行状态保持不变，避免唤醒 graph 恢复破坏正在进行的录音。
`LastErrorSnapshot` 用例并发读取生产锁内错误副本。模型/声学、麦克风和 FreeRTOS
均为 host fake，不证明真实 MultiNet 识别、声学质量、NVS 或硬件行为。

```powershell
wsl -d Debian -- bash -lc '
  cmake -S /mnt/d/workspace/rodakos/tests/voice_audio_frontend_identity \
        -B ~/.cache/rodakos-voice-audio-frontend-identity -G Ninja -DCMAKE_BUILD_TYPE=Debug &&
  cmake --build ~/.cache/rodakos-voice-audio-frontend-identity &&
  ctest --test-dir ~/.cache/rodakos-voice-audio-frontend-identity --output-on-failure
'
```

CTest 名称为 `rodakos_voice_audio_frontend_identity`，可执行文件为
`rodakos_voice_audio_frontend_identity_tests`。ASan/UBSan 使用独立构建目录并设置
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`。

# AudioService 异步播放宿主回归

目标编译生产 `main/phone_os/audio_service.cc` 和当前 managed Helix MP3 解码器的完整 C 源。
测试使用实际临时 WAV/MP3 文件、真实 `std::thread` 播放任务和互斥锁，音频输出硬件由可控制的
fake 代替。`PlayFile()` 返回值只表示任务接受，最终结果通过 `GetState()` 验证。

```powershell
wsl -d Debian -- bash -lc '
  cmake -S /mnt/d/workspace/rodakos/tests/audio_playback_service \
        -B ~/.cache/rodakos-audio-playback-service -G Ninja -DCMAKE_BUILD_TYPE=Debug &&
  cmake --build ~/.cache/rodakos-audio-playback-service &&
  ctest --test-dir ~/.cache/rodakos-audio-playback-service --output-on-failure
'
```

CTest 名称为 `rodakos_audio_playback_service`，可执行文件为
`rodakos_audio_playback_service_tests`，超时 40 秒。ASan/UBSan 使用独立构建目录并增加：

```text
-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
```

运行时设置 `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`。生产服务、host 边界
和 Helix C 源均受检查，没有 sanitizer 豁免或排除。`helix_host_arithmetic.h` 只替换 Helix
`assembly.h` 中与 CPU 相关的整数乘法高位、绝对值、前导零、64 位累加及移位 intrinsic，
使解码源码可在 x86-64 上构建；这不验证 ESP32-S3 指令实现或实际 DSP 性能。

17 项测试覆盖：

- 完整 WAV 播放；RIFF/实际长度、chunk 大小、零数据、采样率/字节率和 stereo 帧对齐。
- WAV/MP3 已产生进度后的短读、I/O 错误、截断帧、解码错误和输出失败；失败不成为 Completed，
  不强制填满进度，关闭输出所有权并允许新请求重试。
- 合法 MP3 静音、ID3v1、前置/尾部 ID3v2（含 v2.4 footer）、APEv2（含可选 header），
  以及首帧缺少 bit reservoir 后继续播放。无效元数据长度和尾部不完整帧必须报错。
- 任务创建失败与任务接受后的文件/codec 打开失败；最后一次输出中的 Stop 不成为 Completed。
- 硬件 Resume 失败终止暂停线程，保留错误并可重试；Resume 打开期间 Stop 不被迟到结果覆盖。
- 在 MP3 任务启动前暂停，元数据处理后仍能恢复；Deinit 等待在途输出结束后才释放服务和硬件。
- 输入缓冲末尾不足完整 header/CRC/side-info 时不会调用 Helix；合法跨缓冲帧仍能完成。

MP3 fixture 由测试代码自行构造：MPEG-1 Layer III、128 kbps、44.1 kHz、mono，417 字节帧，
零 side-info/main data 描述静音。reservoir 用例只改变第一帧的 `main_data_begin`，其后的完整帧
仍能被真实 Helix 解码。未使用外部录音或有版权的音频素材。此证据不代表真人声学、speaker
可听输出、所有编码器/自由码率组合或硬件 SD 读取故障的验收。

`stdio_faults.cc` 通过 linker wrapper 保留正常 libc 文件操作，只在指定 bulk read 注入短读或
`ferror`，不会构造播放结果。输出 fake 在打开/写入边界注入失败或持有 barrier。默认正常读取
和解码路径使用真实文件与解码器。`host_runtime.cc` 可单独供 Music UI target 复用，无需链接
stdio wrappers；其 task 起点 hook 仅供测试建立确定的交错。

服务析构会等待正在进行的 I/O；如果真实驱动永远不返回，此实现不承诺有界关闭时间。它不会
超时后强杀持锁任务。硬件故障、音频焦点集成、非静音解码和资源压力仍需独立验证。

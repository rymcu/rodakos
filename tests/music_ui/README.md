# Music 服务与 LVGL 错误状态回归

本目标编译生产 MusicApp、MusicPlayerService、AudioService、AudioOutputService、codec adapter
和 LVGL 9.3，使用真实工作线程、临时 WAV 文件和软件显示。FileService、Settings、Board Manager、
codec 和 UI 字体使用 host fake，不打开实际设备或写入设备 NVS。MP3 解码不在此 UI target 内；
真实 Helix 覆盖见 [audio_playback_service](../audio_playback_service/README.md)。

```sh
cmake -S tests/music_ui -B build-host-music-ui -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host-music-ui -j 4
ctest --test-dir build-host-music-ui --output-on-failure -V
```

ASan/UBSan 使用独立目录，并为 C/C++ 添加
`-fsanitize=address,undefined -fno-omit-frame-pointer`；执行时使用
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`。CTest 超时为 60 秒。

18 项用例覆盖缺服务、缺卡到空库、根目录/子目录读取失败、旧曲目清除、纯不支持格式的空库、
真实 Refresh / Retry 按钮恢复、旧列表版本拒绝、其他应用占用音频、monitor 创建失败后的重试、
慢卡扫描时 LVGL 与 Stop 可继续、Deinit 等待扫描、异步文件/codec 错误及后续重试、音量拒绝后
滑杆回读、Stop 保留自动续播停止状态、Resume 与独占焦点排序，以及扫描中操作错误在终态清除。

截图写入 CTest 构建目录的 `music-empty.ppm`、`music-missing-card.ppm` 和
`music-playback-error.ppm`，来自生产 LVGL 控件树。用例检查长标题与错误状态不重叠，状态的两行
文字位于卡片内；最终视觉检查仍须查看渲染图。字体 fake 不证明设备字体/图标显示。

这些测试不等于实际 SD 移除、慢卡、OOM、触摸、Codec/I2C 故障或声学验收。这里通过真实服务
调用顺序检查音乐阻塞/暂停/恢复边界，但不实例化生产 VoiceAssistant/Recorder 的完整焦点流程。

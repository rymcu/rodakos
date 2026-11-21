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

23 项用例覆盖缺服务、缺卡到空库、根目录和子目录读取失败、曲库版本拒绝、异步错误优先于进度、
播放失败后按原路径重试、音频焦点阻塞、Stop 阻止自动续播、音量写入失败回滚，以及慢卡扫描期间
仍可操作界面。歌曲页测试验证 250 首歌只用固定行槽、末曲可选，以及按住时曲库更新会取消点选。
播放器测试检查加载和播放时显示唤醒暂停标记、暂停后隐藏、播放键图标随状态切换、MP3/WAV 时长、
「歌名-歌手」拆分、退出歌曲页保留播放状态，以及系统返回和点击遮罩都会先收起音量面板。

截图写入 CTest 构建目录的 `music-empty.ppm`、`music-missing-card.ppm` 和
`music-playback-error.ppm`、`music-playing.ppm`、`music-songs.ppm` 和 `music-volume.ppm`，
来自生产 LVGL 控件树。用例检查曲名与错误行不重叠。最终视觉检查仍须查看渲染图，设备字体和图标显示需要实机验证。

这些测试不等于实际 SD 移除、慢卡、OOM、触摸、Codec/I2C 故障或声学验收。这里通过真实服务
调用顺序检查音乐阻塞/暂停/恢复边界，但不实例化生产 VoiceAssistant/Recorder 的完整焦点流程。

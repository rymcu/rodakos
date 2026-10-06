# Recorder 保存结果与列表错误界面回归

本目标编译生产 RecorderApp、RecordingService 和 LVGL，使用实际临时文件、真实录音线程与
stdio 故障注入。输入设备、焦点、播放服务、FileService 发现和 UI 字体使用 fake。

```sh
cmake -S tests/recorder_ui -B build-host-media-save/recorder-ui-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host-media-save/recorder-ui-debug
ctest --test-dir build-host-media-save/recorder-ui-debug --output-on-failure -V
```

ASan/UBSan 在独立构建目录为 C/C++ 添加 `-fsanitize=address,undefined -fno-omit-frame-pointer`，
运行时设置 `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`。

14 项用例验证真实保存失败显示、文件头/flush/close 错误与重试、成功保存但列表读取失败时保留
Saved、缺卡与空列表区分、录音期间刷新隔离、旧错误恢复、timer 创建失败及主题切换生命周期。
标题、状态、时长和文件大小的位置有布局检查；实际保存错误、Saved+列表错误、缺卡截图已查看。

保存结果与 `library_error` 分开：列表不可用不能把已完整保存的文件变成保存失败，也不能假装
没有录音。真实触摸、麦克风、扬声器、SD 故障、音频焦点集成与资源压力仍须单独验收。

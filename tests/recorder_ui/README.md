# Recorder 保存结果与列表错误界面回归

本目标编译生产 RecorderApp、RecordingService 和 LVGL，使用实际临时文件、真实录音线程与
stdio 故障注入，以及生产字体（Montserrat → Ionicons → 普惠体），截图显示真实中文。输入设备、
焦点、播放服务和 FileService 发现使用 fake。测试源文件用 `-fno-access-control` 读取页面状态。

```sh
cmake -S tests/recorder_ui -B build-host-media-save/recorder-ui-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host-media-save/recorder-ui-debug
ctest --test-dir build-host-media-save/recorder-ui-debug --output-on-failure -V
```

ASan/UBSan 在独立构建目录为 C/C++ 添加 `-fsanitize=address,undefined -fno-omit-frame-pointer`，
运行时设置 `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`。

23 项用例验证真实保存失败显示（「录音失败」，中间卡片写原因和原始错误）、文件头/flush/close
错误与重试、成功保存但列表读取失败时保留「已保存」、缺卡与空列表区分、录音期间刷新隔离、旧错误
恢复、timer 创建失败及主题切换生命周期；以及改版新增的录音页电平条与唤醒让出标记、150 ms 录音
刷新、主屏回放最新录音、列表页中文日期（同秒录音的 `_0001` 后缀显示为「(2)」）与播放进度、删除
确认（先停播、返回键先关弹窗）。状态行、计时、卡片和按钮行的位置有布局检查。

录音回放和扬声器测试音临时把全局音量提到 90，结束、开始录音或关闭 App 时恢复原值；播放期间
被其他入口（MQTT、语音、Music）改过的音量保持不变，原本高于 90 的音量不会被降低。

保存结果与 `library_error` 分开：列表不可用不能把已完整保存的文件变成保存失败，也不能假装
没有录音。真实触摸、麦克风、扬声器、SD 故障、音频焦点集成与资源压力仍须单独验收。

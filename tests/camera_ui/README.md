# Camera 完成结果与 LVGL 生命周期回归

本目标编译生产 CameraApp、PhoneAppHost 和 LVGL，使用真实 worker 线程及软件显示。
CameraService 和音频焦点是 fake；真实保存链路另见 [camera_capture](../camera_capture/README.md)。

```sh
cmake -S tests/camera_ui -B build-host-media-save/camera-ui-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host-media-save/camera-ui-debug
ctest --test-dir build-host-media-save/camera-ui-debug --output-on-failure -V
```

ASan/UBSan 在独立构建目录为 C/C++ 添加 `-fsanitize=address,undefined -fno-omit-frame-pointer`，
运行时设置 `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`。

13 项测试覆盖 worker 不访问 LVGL、完成结果延迟消费、缺卡失败后重试、重复点击、预览失败时仍能
消费结果、任务与 timer 分配失败、旧 generation、App 销毁与真实主题重建、焦点释放、长路径布局，
以及 UI 销毁等待 LVGL 所有权。独立结果 timer 消费共享 guard，worker 不持 App 指针、不依赖
`lv_async_call` 或 LVGL lock 来交付结果。销毁撤销 guard，并在对象释放前删除全部 timer。

构建目录输出相机长路径、存储错误及不可用状态的截图。最终 PNG 已人工查看，固定主题底栏避免
照片内容影响文本对比度，长路径不会换行覆盖快门。host 字体 fake 不验证设备字体、图标或触摸。
本目标不证明 JPEG 编码质量、真实采集、SD 持久性、全面 OOM 恢复或声学行为。

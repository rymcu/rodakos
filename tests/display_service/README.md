# DisplayService 分配失败与恢复

本目标直接、完整地编译生产 `main/phone_os/display_service.cc` 和原始公开头文件，
不替换 `EncodeRgb565`、`EncodeJpeg`、帧复制、worker 或 Stop 路径。
配置时将生产源码 SHA-256 写入构建目录 `production-sources.json`。

21 个用例覆盖：

- StartCapture 第一／第二份帧缓冲分配失败、初始刷新排队失败；失败后能取得锁并重新启动。
- GetLatestFrame 复制失败清除调用方旧数据、释放锁并允许重试；没有新帧时不返回旧 JPEG。
- RGB888 两级 heap-caps 分配、encoder 打开后的 scratch 两级分配、紧凑 JPEG 的 C++
  分配失败；encoder 与每个 heap-caps 缓冲都有独立的打开／关闭和分配／释放计数。
- codec 打开／处理失败；恢复后输出内容来自真实 RGB565 → RGB888 转换，交付 vector 的
  size/capacity 为 codec 实际输出 4096 字节，不保留 230400 字节 scratch。
- callback 存储／task 创建失败；worker 的帧复制、codec、紧凑输出失败按 5 FPS 节流，
  以独立 producer 每 10 ms 持续发布 flush 唤醒，验证失败尝试仍间隔至少 190 ms，
  避免将 semaphore 自身等待误算为节流。恢复后只发送最新帧，没有新帧时不重复分配
  153600 字节副本。
- callback 抛出 `bad_alloc` 后不重放同一序号、下一新帧可用；callback 自停、外部 Stop
  等待已进入 callback、持续分配失败时及时停止、callback 所有权及异步刷新注销。
- callback 最后一个共享引用在 service mutex 外析构，析构重入 service 不会死锁；旧流 Stop
  仅等待其捕获的 generation，callback 析构同步启动替代流时不会错误等待新 task。
- 并发 StartCapture 只注册一次 LVGL event callback，注册与析构注销都持有 LVGL lock；
  CaptureJpeg 的帧复制 OOM 保留精确错误，不被“尚无画面”覆盖。

主机边界：LVGL 使用最小的 flush 事件、display buffer 和异步排队替身；初始异步刷新仅
检查排队与注销，不运行真实绘制树。FreeRTOS 使用真实 C++ 线程、条件变量和互斥语义，
信号量保留二值通知行为。`portMAX_DELAY` 在测试中以 3 秒硬失败检测未释放锁，CTest
另设 30 秒进程时限。host runtime 还记录 event callback 注册／注销是否持有 LVGL lock。
codec 替身接受实际转换输入，产生带输入标记的固定长度字节；这不是
有效 JPEG，不证明 ESP 编码器的图像质量或内部工作区行为。

Linux 链接器仅包装全局 `operator new/new[]` 的入口，选中 size／次序／worker 线程后
抛出真实 `std::bad_alloc`；其他分配仍交给原始 libstdc++／ASan 分配器。记录故障时间使用
预分配数组，heap-caps 计数使用固定记录，不为故障采样再分配内存。失败重试的真实时间
间隔容许 10 ms 调度误差；没有新帧时观察两个以上 FPS 周期。

## Linux / WSL

```bash
cmake -S tests/display_service -B "$HOME/.cache/rodakos-display-service-debug" \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$HOME/.cache/rodakos-display-service-debug" -j 2
ctest --test-dir "$HOME/.cache/rodakos-display-service-debug" --output-on-failure

cmake -S tests/display_service -B "$HOME/.cache/rodakos-display-service-asan" \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build "$HOME/.cache/rodakos-display-service-asan" -j 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir "$HOME/.cache/rodakos-display-service-asan" --output-on-failure
```

这些测试不读取串口、NVS、真实 SD 或生产密钥，不证明 Photos/PNG 与屏幕并发时的
实机容量、持续帧率或物理设备恢复。真实 LVGL partial-flush 镜像一致性仍由已有
`tests/home_ui/display_capture_test.cc` 覆盖；设备和资源门禁保持独立。

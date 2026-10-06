# RecordingService 保存与任务生命周期回归

本目标编译生产 RecordingService，使用真实线程、临时 WAV 文件和可控 stdio 故障。
音频输入、焦点与存储发现接口使用 fake；不打开麦克风、SD 或设备 NVS。

```sh
cmake -S tests/recording_service -B build-host-media-save/recording-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host-media-save/recording-debug
ctest --test-dir build-host-media-save/recording-debug --output-on-failure -V
```

ASan/UBSan 使用独立目录，并添加
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`；运行时设置
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`。

17 项用例覆盖完整 WAV、最终 seek/header/flush/close 失败、取消期间 finalize/cleanup 错误保留、短写与原始错误保留、文件名碰撞、
排他创建与清理失败、开始/停止/销毁交错、输入/任务/焦点失败及重试、格式验证，以及录音列表的
读取错误与保存结果隔离。列表中无效格式可跳过，真正 I/O 失败不能伪装为空库或部分成功。

录音文件通过 `FileService::WithWriteLease` 按路径保护：租约覆盖排他创建、数据写入、最终头、
flush、close 和失败清理，但不持有整个 SD I/O 锁；同一路径被占用时尝试下一个排他后缀，
不同录音路径可以并发，Delete/Rename/Upload 不能替换活跃路径。时间戳冲突最多尝试 256 个后缀，
目录耗尽会快速报告错误，不会长时间占用焦点。

Start 成功只表示任务接受。只有完整文件正常写入并关闭才发布 Completed/Saved；用户在取得音频前
取消时不发布空录音成功。停止有轮询期限，服务析构仍等待在途任务退出，不能在任务持有对象时删锁。
底层驱动永久阻塞时不承诺有界析构。宿主租约 fake 验证同路径 Delete/Rename 拒绝和不同路径并发，
不替代真实 FAT/SD 多客户端测试。真实麦克风/焦点集成、资源压力与掉电持久性仍待验收。

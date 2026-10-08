# 媒体保存与异步结果边界

本轮媒体保存一致性工作从 RodakOS `b29d8375` 开始，覆盖录音、相机抓拍、FileService 文件写入和 Web 文件上传。实现把“任务已接受”“文件已完成”和“列表刷新成功”分成独立结果；任何一个结果都不会替代另外两个结果。

## 共同的文件写入约束

录音和 Web 上传路径通过 `FileService::WithWriteLease` 取得按路径租约。租约登记规范化后相对于挂载点的路径，拒绝空字节和 `..` 路径，并检查父子路径冲突。租约只登记路径，不长时间持有整个 SD I/O 锁；读操作可以继续，其他 Delete、Rename、WriteFile、WriteNewFile 或上传写入在活跃路径上被拒绝。

录音 worker 在租约回调中完成排他创建、PCM 写入、最终 WAV 头、flush、close 和失败清理；Web 上传保留已有的替换写入语义，并在同一租约内完成接收、写入和清理。租约释放前不能有协作的 FileService 写操作替换文件，因此失败清理不会删除同一 FileService 协作路径上后来创建的同名文件；外部绕过 FileService 的裸文件操作不在保证范围内。录音遇到已有文件或活跃路径时最多尝试原名及 256 个后缀，全部占用则报告错误。

固件的 `FileService` 和 `CameraService` 由 `main.cc` 持有静态实例，必须晚于所有使用它们的 worker 退出。路径租约本身不等待 `FileService::Deinit()`，也不宣称任意服务并发卸载安全。

## 录音

`RecordingService::Start()` 返回 true 只表示录音任务被接受。worker 创建 WAV 后写入 PCM，停止或失败时重写数据长度和 RIFF 长度；只有 seek、最终 header、flush、close 全部成功，状态才是 `Completed`／`Saved`。开始阶段取消正常显示 Cancelled；若取消后的 flush、close 或清理失败，仍显示具体错误。ADC、任务、焦点、写入和最终收尾失败同样保留错误，并释放已取得的资源。

文件使用排他创建，已有文件不会被覆盖。同一路径租约被占用时会尝试下一个文件名；路径租约覆盖整个录音生命周期，因而同名 Delete、Rename 或上传不能替换录音。录音列表的读取错误保存在独立的 `library_error` 中，不会把已经完整保存的文件改成失败，也不会把扫描失败显示成“没有录音”。

Recorder 页面把录音错误和列表错误分开显示，保存失败可以重试；定时器创建失败和主题重建失败会清理半成品 UI。已保存条目仍可执行播放和停止操作。相机页面使用独立的共享结果 guard、generation 和独立 UI result timer；worker 不访问 LVGL，页面销毁只撤销旧结果的 UI 消费，不取消已经进入 `CapturePhoto` 的保存调用。

## 相机与上传

相机抓拍在 `WithIoLock` 短写锁内调用 `WriteNewFile`，只在文件写入、flush、close 成功后发布结果路径；失败会清理本次拥有的文件并保留之前已经成功的照片。它不使用长时 `WithWriteLease`。相机服务析构只排空已经进入 `CapturePhoto` 的调用，不能替代真实设备或存储卸载协调。

Camera DVP 的失败清理随后由 `29aaacd`、`4e08efa` 与 `51927b0` 收敛：底层视频或 I2C 清理失败时保留句柄，`CameraDevice::Acquire()` 在重新初始化前先重试释放，`CloseStream()` 只在释放成功时记录 complete；9 项 source-contract 回归通过。该修复尚未随当前 COM3 包完成设备复验，不能替代真实 Camera 资源与长稳门禁。

Web 上传在打开、接收、写入、flush、close 和失败清理期间持有目标路径租约。租约冲突返回 HTTP 409，普通写入或收尾失败返回 HTTP 500；上传成功只表示本次正常文件写入完成；替换写入失败可能已截断原文件，当前不是原子替换协议。

## 软件证据与开放边界

宿主目标使用真实文件、线程、LVGL 或生产服务代码，并对短写、读写错误、flush、close、timer、路径冲突和重试注入故障。它们不打开真实麦克风、相机、扬声器或 SD 卡，也不证明 FAT 在拔卡时的行为、掉电原子性、任意服务并发销毁、任意 LVGL 内存耗尽、声学输出或八小时 OTA soak。

Photos、File Manager 的扫描错误、重试和图像资源契约见[媒体浏览](media-browsing.md)。真实 SD 移除／慢卡、硬件音频焦点、麦克风和相机资源压力仍在路线图中。Recorder 的成功语义是完整文件正常写入并关闭，不是持久化保证。

相关宿主目标：

- [`tests/recording_service`](../tests/recording_service/README.md)
- [`tests/recorder_ui`](../tests/recorder_ui/README.md)
- [`tests/camera_capture`](../tests/camera_capture/README.md)
- [`tests/camera_ui`](../tests/camera_ui/README.md)
- [`tests/file_writer`](../tests/file_writer/README.md)
- [`tests/file_path_lease`](../tests/file_path_lease/README.md)
- [`tests/web_file_upload`](../tests/web_file_upload/README.md)
- [`docs/music-playback.md`](music-playback.md)

具体测试数、冻结源码和固件身份见[本轮软件证据](ota-release-readiness.md#2026-10-06-media-save-validation)，当前剩余工作见[路线图](roadmap.md)。

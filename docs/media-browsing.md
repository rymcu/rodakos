# Photos 与 Files 的读取失败和重试

Photos 和 Files 将扫描失败与正常空列表分别显示。失败时清除旧列表和部分结果，保留明确错误及重试入口；缺失服务、SD 不可用、目录不可用和一般读取失败不再统一显示为空目录。没有足够错误信息时使用一般失败提示，不推断为文件损坏或内存不足。

## 目录扫描

Photos 通过 `ImageLibrary::ScanPhotoLibrary` 先读取根目录，确认可选的 `/photos`、`/DCIM` 是否存在，再按原有深度扫描相册。相册为空时仍使用根目录扫描回退；任一受扫描目录读取失败都会清除部分集合并显示错误，不使用 `Exists=false` 掩盖 I/O 失败。Photos 保留独立的列表和全屏页头。

Files 在每次读取前记录目标目录并清除旧 entries。进入子目录失败后，Retry 重试该目录，Back 尝试父目录；刷新失败不能继续打开旧文件行。真实空目录显示 `Empty folder` 与 Refresh。服务稍后注册或 SD 恢复后可以显式重试。`ListDirectory` 的未挂载失败设置 `ENODEV`，目录读取错误在日志输出后保留原 errno；调用者先清 errno，不借前一次操作的错误猜测原因。

## 预览与资源

图像接口返回成功、格式不支持、读取失败、已知分配失败或一般解码失败。完整读取和资源所有权分别检查；不把类型未知的解码错误都归为内存不足。Files 预览错误持续显示，Retry 保留原文件路径；重试前重新检查 FileService 和存储状态，成功后隐藏错误和按钮。

PNG 完整读取后直接通过真实 `lodepng_decode32` 生成像素，并由 LVGL 9.3.0 的 checked overlay 保护原始行宽、最终 ARGB8888 行宽和 16 位几何字段。成功结果转换为 LVGL 的 ARGB8888 字节布局并由图像对象持续持有，绘制时不再进行第二次 PNG 解码；只读到尺寸不算成功。保留的像素会一直占用内存到图像释放。BMP 校验头、偏移、尺寸和完整像素范围，保留 24/32 位直接像素及明确 RGB565 masks 的 16 位 bitfields，拒绝调色板、RLE、top-down 等当前未支持形式。BMP 随后仍按 filesystem source 渲染，文件后来被修改/拔卡不在本次加载结果保证内。

替换、返回、刷新与销毁先从 LVGL image 对象分离旧 source，再丢弃 cache 和拥有的图像。关闭应用会取消其待执行 Home 请求；销毁须取得 LVGL 所有权，不能因锁超时就释放仍被界面引用的数据。Photos 的缩略图调度失败保留可见提示和恢复入口，不能一直表现为正在加载。

扫描、挂载和图像读取仍是同步操作，重试不新增后台 worker。底层慢卡可能阻塞调用；这项改动不承诺读取期间导航响应或有界取消。显式失败状态、资源安全与真实 SD/内存耗尽是分别验证的范围。

## 验证与开放边界

- `tests/photos_ui` 编译生产 Photos 和 ImageLibrary，覆盖扫描结果、读取/解码失败、缩略图及重试；硬件依赖和 JPEG 解码使用明确替身。
- `tests/file_manager_ui` 编译生产 Files、PhoneAppHost 与真实 LVGL；FileService 和图像结果为可控替身，单独验证目录/预览状态、路径、操作和释放顺序。
- `tests/camera_capture` 使用生产 FileService 与真实临时主机目录，验证未挂载/缺失目录错误经过 adapter 传播；底层目录读取故障另由 `tests/file_directory` 覆盖。

测试截图中的字体/图标为 host 替身，不证明设备字体或 GT911 实体触摸。软件工作由 [#35](https://github.com/rymcu/rodakos/issues/35) 跟踪；真实 SD 缺失/拔卡/慢卡、功能恢复、任意 OOM、并发和资源归还、八小时 soak 由 [#28](https://github.com/rymcu/rodakos/issues/28) 保留。正常读取关闭不证明掉电持久性；012/013 的有限设备证据见下节，不关闭这些门禁。

## 2026-10-07 软件验证

源码提交：`8b0a03131549f2245dd7ce48ab1c3e91fa34b72c`。

| 套件 | 每种构建通过数 | 实际覆盖 |
| --- | ---: | --- |
| Photos / ImageLibrary | 23 | 生产 UI/loader、真实 PNG/BMP decoder、文件读取和失败注入；ESP JPEG decoder 为替身 |
| Files UI | 15 | 生产 UI/生命周期与真实 LVGL；FileService/image 结果为替身 |
| Camera / FileService | 16 | 生产 adapter、真实临时主机目录及保存链；相机/JPEG 为替身，含新增未挂载/缺目录错误传播 |
| Directory | 8 | 生产目录读取器、真实 POSIX 文件与 opendir/readdir/stat/closedir 故障 |

共 62 项，分别通过 Debug 和 ASan/UBSan/泄漏检查；不是 124 项不同场景。Photos 四张和 Files 两张 host 画面已核对，独立代码审查通过。release host runner 已接入两个新 UI suite。原始记录为 `.codex-temp/media-*-last-test.log`，生产文件冻结摘要为 `media-browsing-production-freeze.json`。

提交后的 ESP-IDF 6.0.2 增量构建通过：`build/rodakos.bin` 为 **7,106,432 B**，SHA-256 **`a6e2620fac3ac673dbc635e41a52385c9423b4aef79df22c2416583a1184b060`**，位于 13,959,168 B 的 `ota_0` 容量以内。Home 测试人口和 release fault injection 关闭，原验证公钥、sdkconfig 和依赖锁保持原基线。该软件阶段未操作设备；后续打包、刷写与有限实测单独记录如下。

## 012–014 实机与长名称修正

012（`20261007-053630`）保留 NVS 刷写后，Photos 扫描到 41 张图片，真实 `a1.jpg` 可显示；`A3.PNG` 显示一般加载错误并可 Retry，重试仍失败，在该次记录中尚未归因。Files 的长目录名覆盖了 `Folder` 说明，由 `3504308ce13b631fe67727b5e120670899050c1f` 修正：列表名称和信息标题按实际字体固定单行省略，垂直 padding 调为 6，行高与间距保持。Files 15 和 Photos 23 项再次分别通过 Debug/ASan/UBSan/leak；Photos 仅扩展既有指针测试，生产代码未改。

013（`20261007-055040` / `media-browsing-layout-013`）的 main 为 **7,106,528 B**，SHA-256 **`91344e3508c4bada6f6dd05c5ec2145016317dc3d367c46333c93ea5460bbc60`**。真实画面确认长目录名及文件信息不再重叠，JPEG 打开/Retry 成功并返回相册。后续对 crash ELF 和终止路径的核对表明，`A3.PNG` Retry 已实际触发，但终止点是并发 `DisplayService` JPEG 路径未处理的 `std::bad_alloc`，随后进入 abort；它不是 `A3.PNG` 已被证明为 16 位 PNG 的证据，也不能把 abort 归因于 PNG 位深或 LodePNG 覆盖层。013 未得到成功或失败后的稳定 PNG 画面。

退出屏幕后的两轮 WSS 静音、MQTT 共存、唤醒监听恢复及两条新遥测通过；原 ID、bound 和 tokenVersion=4 保持，COM3 释放。内部堆历史最低 **335 B** 在首次 WebSocket 任务创建/握手前已有记录，此时语音准备及 HTTPS 刷新已进行；现有采样无法定位低点所属阶段。停止后可观察到资源回升，但不能证明容量充足、无泄漏或全并发安全。真实触摸/声学、稳定 PNG 显示、SD/慢卡/OOM/soak 继续开放。完整包身份、画面、串口与结束状态见 [012–014 联合验收记录](https://github.com/rymcu/rodak/blob/master/docs/media-browsing-verification.md)。

## 014 软件修复、包身份与实机结果

`DisplayService` 的最终软件修复把 capture/frame 复制、RGB565→RGB888、JPEG encoder scratch、紧凑输出和 callback 所有权等分配失败收敛为可报告错误，保证互斥量和 codec/buffer 资源释放，并对持续失败按帧率节流；callback 抛出 `bad_alloc` 后不重放旧序号，下一张新帧仍可恢复。PNG 路径直接保留一次成功的 ARGB8888 解码结果，避免 UI 已报告加载成功后在绘制阶段再次解码和再次承受瞬时分配压力。LVGL 9.3.0 LodePNG 采用 build-local checked overlay，11 个上游文件的 provenance、组件锁和目标替换均 fail closed；详情见 [依赖维护](dependency-maintenance.md#lvgl-lodepng-decode-overlay)。

最终软件证据均通过 Debug 和 ASan/UBSan（含泄漏检查）：

| 套件 | 通过数 | 边界 |
| --- | ---: | --- |
| DisplayService | 21 | 生产 capture/JPEG worker、分配失败、锁、停止和恢复；codec/LVGL/RTOS 边界为明确替身 |
| Home UI | 43 | 真实 LVGL partial-flush 镜像与既有 Home/UI 回归 |
| Photos / ImageLibrary | 24 | 生产 loader/UI、真实 LodePNG/BMP、一次解码后保留 ARGB8888；ESP JPEG 为替身 |
| LodePNG overlay | 8 + 2 | 8 个 8/16 位及 Adam7 PNG 变体，加 2 个几何/stride 拒绝 |

ESP-IDF 6.0.2 最终增量构建通过。源码提交为
`89705604e6d84bdff5db62f1b98d623b19743267`；014 包目录为
`build/packages/ota/20261007-092426`，taskNo `media-png-display-recovery-014`，version
`0.1.2-dev.1`。这是 `buildFlavor=production` 的开发签名普通包，Home 测试人口和故障注入均
关闭。`rodakos.bin` 为 **7,108,688 B**，SHA-256
**`44e2a1130e2d551d34e2971dc47ed59dda9dc8cfee86f6ca8e3179dc7ee631f4`**；包 ZIP SHA-256 为
**`5449af4f51da1d312800ac15a9b8e332aef003b804fa5d1a1afa7966e2af512e`**。Recovery、
Bootloader、分区表、otadata 和公钥与 013 一致，签名 keyId 仍为
`bfd07b5f030766ee83cd06b7889465a47effef99bc44e644dcc4400da8a60fdf`。

014 先通过 `-VerifyOnly -AllowDevelopmentPackage`，再以非 Erase 增量方式只写 otadata 和
`ota_0`，保留 NVS、绑定与 trust。Recovery → main、Home 启动及本地 OTA 确认均通过；设备
保持 MAC `44:1b:f6:c3:b4:30`、原 ID、`bound` 和 `tokenVersion=4`。

同一屏幕会话确认 `/sdcard/photos/A3.PNG` 为 **69,200 B、471 x 423、8-bit、color type 6
(RGBA)**。控制 enable sequence 1、首次打开 sequence 2/3，以及两次 Retry sequence 4/5、
6/7 均收到 `accepted=true`。三次解码都稳定返回 LodePNG error 83，页面显示
`Not enough image memory`；屏幕 JPEG 全程继续出帧，没有 abort、panic 或 reboot。首次失败
时 internal free/largest 为 28,139/13,824 B，PSRAM free/largest 为
1,728,656/1,605,632 B。会话关闭后的只读状态保持 `bound`、`tokenVersion=4`、MQTT online、
voice idle，COM3 已释放。

014 已验证 013 的未处理 JPEG 分配不会再终止设备，也排除了“A3 是 16-bit PNG”的猜测，
但 **A3.PNG 仍未成功显示**。当前高置信诊断是 RGBA8 解码时约 0.8 MB 的 scanlines 与
796,932 B（约 778 KiB）的最终 ARGB8888 像素同时存在形成峰值并受碎片影响；在后续降峰值实现或 allocator
证据前，这仍是诊断结论。A3 成功显示、真实 SD 缺失/移除/慢卡、任意 OOM、跨服务并发、
资源归还和八小时 soak 门禁继续开放。

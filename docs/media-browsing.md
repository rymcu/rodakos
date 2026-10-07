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

测试截图中的字体/图标为 host 替身，不证明设备字体或 GT911 实体触摸。软件工作由 [#35](https://github.com/rymcu/rodakos/issues/35) 跟踪；真实 SD 缺失/拔卡/慢卡、功能恢复、任意 OOM、并发和资源归还、八小时 soak 由 [#28](https://github.com/rymcu/rodakos/issues/28) 保留。正常读取关闭不证明掉电持久性；012–017 的有限设备证据见下节，不关闭这些门禁。

## 2026-10-07 初始软件验证

源码提交：`8b0a03131549f2245dd7ce48ab1c3e91fa34b72c`。

| 套件 | 每种构建通过数 | 实际覆盖 |
| --- | ---: | --- |
| Photos / ImageLibrary | 23 | 生产 UI/loader、真实 PNG/BMP decoder、文件读取和失败注入；ESP JPEG decoder 为替身 |
| Files UI | 15 | 生产 UI/生命周期与真实 LVGL；FileService/image 结果为替身 |
| Camera / FileService | 16 | 生产 adapter、真实临时主机目录及保存链；相机/JPEG 为替身，含新增未挂载/缺目录错误传播 |
| Directory | 8 | 生产目录读取器、真实 POSIX 文件与 opendir/readdir/stat/closedir 故障 |

共 62 项，分别通过 Debug 和 ASan/UBSan/泄漏检查；不是 124 项不同场景。Photos 四张和 Files 两张 host 画面已核对，独立代码审查通过。release host runner 已接入两个新 UI suite。原始记录为 `.codex-temp/media-*-last-test.log`，生产文件冻结摘要为 `media-browsing-production-freeze.json`。

提交后的 ESP-IDF 6.0.2 增量构建通过：`build/rodakos.bin` 为 **7,106,432 B**，SHA-256 **`a6e2620fac3ac673dbc635e41a52385c9423b4aef79df22c2416583a1184b060`**，位于 13,959,168 B 的 `ota_0` 容量以内。Home 测试人口和 release fault injection 关闭，原验证公钥、sdkconfig 和依赖锁保持原基线。该软件阶段未操作设备；后续打包、刷写与有限实测单独记录如下。

## 012–013 实机与长名称修正

012（`20261007-053630`）保留 NVS 刷写后，Photos 扫描到 41 张图片，真实 `a1.jpg` 可显示；`A3.PNG` 显示一般加载错误并可 Retry，重试仍失败，在该次记录中尚未归因。Files 的长目录名覆盖了 `Folder` 说明，由 `3504308ce13b631fe67727b5e120670899050c1f` 修正：列表名称和信息标题按实际字体固定单行省略，垂直 padding 调为 6，行高与间距保持。Files 15 和 Photos 23 项再次分别通过 Debug/ASan/UBSan/leak；Photos 仅扩展既有指针测试，生产代码未改。

013（`20261007-055040` / `media-browsing-layout-013`）的 main 为 **7,106,528 B**，SHA-256 **`91344e3508c4bada6f6dd05c5ec2145016317dc3d367c46333c93ea5460bbc60`**。真实画面确认长目录名及文件信息不再重叠，JPEG 打开/Retry 成功并返回相册。后续对 crash ELF 和终止路径的核对表明，`A3.PNG` Retry 已实际触发，但终止点是并发 `DisplayService` JPEG 路径未处理的 `std::bad_alloc`，随后进入 abort；它不是 `A3.PNG` 已被证明为 16 位 PNG 的证据，也不能把 abort 归因于 PNG 位深或 LodePNG 覆盖层。013 未得到成功或失败后的稳定 PNG 画面。

013 在后续 A3 Retry abort 探测之前，退出屏幕后两轮 WSS 静音、MQTT 共存、唤醒监听恢复及两条新遥测通过；原 ID、bound 和 tokenVersion=4 保持，COM3 释放。这一历史窗口不能作为后续 abort 后恢复证据。内部堆历史最低 **335 B** 在首次 WebSocket 任务创建/握手前已有记录，此时语音准备及 HTTPS 刷新已进行；现有采样无法定位低点所属阶段。停止后可观察到资源回升，但不能证明容量充足、无泄漏或全并发安全。真实触摸/声学、SD/慢卡/OOM/soak 继续开放。完整包身份、画面、串口与结束状态见 [012–017 联合验收记录](https://github.com/rymcu/rodak/blob/master/docs/media-browsing-verification.md)。

## 014–016 软件修复、包身份与实机结果

014 先把 capture/frame 复制、RGB565→RGB888、JPEG encoder scratch、紧凑输出和 callback 所有权等分配失败收敛为可报告错误，保证互斥量和 codec/buffer 资源释放，并对持续失败按帧率节流；callback 抛出 `bad_alloc` 后不重放旧序号，下一张新帧仍可恢复。PNG 路径保留一次成功的 ARGB8888 解码结果，避免绘制阶段二次解码。015 进一步让非交错 RGBA8 PNG 在 LodePNG 的 decompression allocation 内完成 unfilter/compact，并把该分配直接交给 LVGL draw buffer，去掉同时存在的第二份完整 ARGB8888 像素缓冲。016 再修正独立的屏幕 JPEG 峰值：不再为 worker 深拷贝 153,600 B RGB565 帧，而是在一块 230,400 B 缓冲内锁中复制 RGB565、锁外反向原地扩展为 RGB888；输出 scratch 固定为上游 320 x 240 示例使用的 100 KiB，超出上限时安全丢弃该帧并允许新帧恢复。

LVGL 9.3.0 LodePNG 继续采用 build-local checked overlay，11 个上游文件的 provenance、组件锁和目标替换均 fail closed；详情见 [依赖维护](dependency-maintenance.md#lvgl-lodepng-decode-overlay)。以下为包含 017 新增 Camera 与 PNG 回归的当前定向软件证据，均通过 Debug 和 ASan/UBSan（含泄漏检查）；017 的改动与实机范围见后节：

| 套件 | 通过数 | 边界 |
| --- | ---: | --- |
| DisplayService | 24 | 生产 capture/JPEG worker、原地转换、100 KiB 上限、分配失败、锁、停止和恢复；codec/LVGL/RTOS 边界为明确替身 |
| Home UI | 43 | 真实 LVGL partial-flush 镜像与既有 Home/UI 回归 |
| Photos / ImageLibrary | 24 | 生产 loader/UI、真实 LodePNG/BMP、一次解码后保留 ARGB8888；ESP JPEG 为替身 |
| Camera / FileService | 21 | 生产 CameraService/FileService、末位 owner 退出及异常 DQBUF 后归还帧、并发快照与停止发布顺序；V4L2/JPEG/board 为替身 |
| LodePNG overlay | 11 + 2 | 11 个 8/16 位、RGBA filter 及 Adam7 PNG 变体（含 471 × 423 合成 RGBA8），加 2 个几何/stride 拒绝；另核对分配预算、三个拒绝点及恢复 |

016 的 DisplayService 定向 Debug 和 ASan 各 24 项通过；该包完整 release host runner 通过 30 个 suite、42 个 CTest，以及四组 Python 检查 17、15、8、12 项。ESP-IDF 6.0.2 构建通过。这组完整 runner 结果属于 016，不能自动作为 017 的全量验证。应用持有的 heap-caps 编码峰值由约 614,400 B 降为 332,800 B；真实 codec 另使用约 46,080 B PSRAM，不计入该应用峰值。

历史 014 源码为 `89705604e6d84bdff5db62f1b98d623b19743267`，包目录 `build/packages/ota/20261007-092426`，taskNo `media-png-display-recovery-014`，version `0.1.2-dev.1`。main 为 **7,108,688 B**，SHA-256 **`44e2a1130e2d551d34e2971dc47ed59dda9dc8cfee86f6ca8e3179dc7ee631f4`**，ZIP SHA-256 为 **`5449af4f51da1d312800ac15a9b8e332aef003b804fa5d1a1afa7966e2af512e`**。同一屏幕会话确认 `/sdcard/photos/A3.PNG` 为 **69,200 B、471 x 423、8-bit、color type 6 (RGBA)**；首次打开和两次 Retry 均返回 LodePNG error 83 / `Not enough image memory`。屏幕 JPEG 持续出帧，没有 abort、panic 或 reboot。014 因而修复了 013 的致命终止，但没有显示 A3。

015 源码 `bf1bf248056a47e9a902b111f163afb60b0c2718` 打包为 `build/packages/ota/20261007-100828`，taskNo `media-png-inplace-015`，version `0.1.2-dev.1`。main 为 **7,108,624 B**，SHA-256 **`3cf1e0b8566cf8f40f5fb9b0fefe9be699c26fe69bd8faecd4fac2be908f9eaa`**；ZIP SHA-256 为 **`474d62301da21dbf94f10a9350d891852cfcedf35cf565d13824a4c03baf9b86`**。A3 首次打开 190 ms 解码成功，两次 Retry 分别 260 ms、185 ms 并显示成功，验证了 RGBA8 in-place 路径。PNG 常驻后，旧 DisplayService 峰值却使多轮 JPEG stats 的 attempts 为 12 或 13、encoded 为 0，所有尝试均失败；仅在替换间隙成功编码一帧。015 因而不是可接受的显示流结果。

历史 016 源码提交为 `dc2bff4d25b7b4637a010f600e68d4e3b90b0ccb`，包目录 `build/packages/ota/20261007-103517`，taskNo `media-display-stream-016`，version `0.1.2-dev.1`。这是 `buildFlavor=production` 的开发签名普通包，Home 测试人口和 release fault injection 均关闭。`rodakos.bin` 为 **7,108,752 B**，SHA-256 **`95ee90d27e97e39956e56f557e4d76c1ed1a4881df40315cc5bfb789151d0126`**；包 ZIP SHA-256 为 **`97e48b12385c62fca24e3dd6bbbd7a61e38a22b00d832f098367030bc46b6c7c`**。Recovery SHA-256 为 **`ffa412ebe30c714c691bba73c8ab6e4efcaaab14fce5229f595707a8a08f75fd`**，与 015 相同。

016 先完成包核验，再以非 Erase 增量方式保留 NVS 刷写；Recovery → main、Home 启动和本地 OTA 确认均通过。干净重启后的同一屏幕验证中，A3 五次解码全部成功，耗时依次为 **186、180、202、191、184 ms**。截图确认首次打开、两次 Retry 和额外两次压力重复都显示 A3；DisplayService 累计 **34 attempts / 34 encoded / 0 failed**，全程无 abort、panic 或 reboot。

这仍是定向短时结果。PNG 驻留时 PSRAM free 约 **532 KiB**，largest 通常为 **360–426 KiB**；一次替换前采样只有 **229,376 B**，低于下一次 RGB888 所需的 **230,400 B** 单块。当前实现仍保留 ARGB8888 PNG，不在本轮引入 RGB565 retention。真实 SD 缺失/移除/慢卡、Camera/Voice/MQTT 等更宽并发、任意 LVGL/CLIB OOM、长期资源归还和八小时 soak 门禁继续开放，不能把 016 写成彻底稳定。

## 017 相机退出与 PNG 分配峰值

017 源码为 `860752e44474625fdbcd589b71e47b36374f4d21`，修复两条独立资源路径。CameraService 在最后一个 local/remote 预览 owner 停止或 DQBUF 异常退出时清除 `has_frame` 并归还最后一帧的 vector 分配；另一 owner 仍持有预览时保留其帧。任务停止标记移到最后一次服务访问之后，避免 Stop/析构与 worker 尾部竞争。已经复制给调用者的帧仍由调用者拥有，停止不会使这些副本失效。

LodePNG 的 Huffman inflate 在每轮（包括 end code 后）要求 260 B 余量。原先只预留 **797,355 B** 的 A3 同尺寸 scanlines，在结束阶段仍可能触发 vector 约 50% 扩容至 **1,196,213 B**；017 在已知输出长度路径预留 `expected_size + 260`，包含加法溢出检查和分配失败返回，最大请求降为 **797,615 B**。这不改变 ARGB8888 持有方式，也不改变未知输出长度或自定义 zlib 路径。

Camera 21 项 Debug/ASan/UBSan/leak 通过；旧生产源码负对照有 6 项失败，仅 `clear()` 而保留 vector capacity 的变异也有 6 项失败。PNG 使用 471 × 423 合成 RGBA8、11 个有效变体及 2 个几何拒绝场景。单块预算 **1,081,344 B** 下，旧 reserve 实际请求 1,196,213 B 并返回 error 83；修复后最大请求 797,615 B，成功解码并归还全部跟踪分配。IDAT、inflate reserve、收养 draw-buffer descriptor 三个拒绝点均返回 83，清理后再次成功。合成文件压缩输入为 **9,673 B**，其 `peak_live=818804` 只代表 host ledger，不能作为真实 **69,200 B** A3 的设备峰值。017 完整 release host runner 另行通过 **30 个 suite / 42 项 CTest / 17 + 15 + 8 + 12 = 52 项 Python**，关键源文件哈希在运行前后保持一致。

当前设备包为 `build/packages/ota/20261007-112648`，taskNo `media-camera-png-recovery-017`，version `0.1.2-dev.1`；main **7,108,864 B**，SHA-256 **`507eab226775a848afef1b4df3657b7d02f80d8d812b7a0597844ac052497175`**，ZIP SHA-256 **`3db39d56efcb388c71baa12493cde68c6de07b60dc9d0ca48dba37fb027a60eb`**。VerifyOnly、保留 NVS 的非 Erase 增量刷写、Recovery → main → Home 与本地 OTA 确认通过；immutable 资产与 016 相同，仍为原开发签名链，原设备 ID / bound / tokenVersion=4 保持。

017 首轮先开屏幕同传再进 Camera，两次相机初始化都在 DVP DMA 分配失败，失败前最大连续 DMA 块分别为 **6,656 / 4,352 B**。随后 185 ms 的 A3 成功仅发生在相机未真正启动后的路径，不能作为“相机运行后恢复”的证据。停止屏幕同传后，Camera 才真正启动；重开屏幕后持续预览 **32.304 秒 / 467 帧**并退出，再打开 A3 及两次 Retry 均最终成功解码，耗时 **201 / 203 / 191 ms**。后一次 Retry 的桌面控制报告 timeout，串口随后记录成功解码与显示；因此该次证明设备执行结果，不证明远控 ACK 全通过。第二轮同样先启动 Camera 再开屏幕同传，实际预览 **21.960 秒 / 332 帧**；退出后 A3 再次在 **185 ms** 成功解码，真实像素截图已核对。两次真正运行 Camera 后共四次解码成功，与此前相机启动失败后的 185 ms 分开计数。

作为同板对照，016 在 Camera **24 秒 / 327 帧**后打开 A3 返回 error 83，停止后 PSRAM free/largest 为 **2,411,168 / 1,081,344 B**。017 首个完整循环退出后的 MQTT 采样为 PSRAM free/largest **2,561,940 / 1,507,328 B**。两轮结束后，屏幕同传与远控均停止并回到 Home；周期 JPEG 统计合计 **106 attempts / 106 encoded / 0 failed**，这是已记录周期的合计，不是完整逐帧账本。连续记录未出现 abort、panic 或重启。

最终两条 MQTT health 的 PSRAM free 分别为 **2,558,664 / 2,557,636 B**，largest 均为 **1,507,328 B**；internal free 为 **24,971 / 24,935 B**，largest 均为 **7,680 B**，MQTT worker 最低剩余栈 **2,828 B**。最终 voice health 为 enabled=1 / listening=1；internal 历史最低从首轮 235 B 进一步降为 **131 B**，PSRAM 历史最低 **418,800 B**。内部最大连续块 **7,680 B** 仍低于 soak 的 **8 KiB** 门槛，不能称资源余量充足、无泄漏或发布稳定。结束时原 ID / bound / tokenVersion=4 保持，MQTT 在线、voice inactive，COM3 已释放。原始连续日志、汇总与最终设备状态分别位于 Rodak `.codex-temp/camera-resource-017/serial.log`、`result.json` 与 `device-final.json`。

017 已扩展“真正运行 Camera → 退出 → A3 首开/Retry”的定向通过范围。屏幕先开时的 DMA 失败、迟到输入与 ACK/timeout、跨服务并发 OOM、资源长期归还、真实 SD/慢卡、物理触摸/声学和八小时 soak 仍开放；不能以解码成功覆盖这些失败与未测项。

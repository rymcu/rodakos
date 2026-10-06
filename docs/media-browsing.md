# Photos 与 Files 的读取失败和重试

Photos 和 Files 将扫描失败与正常空列表分别显示。失败时清除旧列表和部分结果，保留明确错误及重试入口；缺失服务、SD 不可用、目录不可用和一般读取失败不再统一显示为空目录。没有足够错误信息时使用一般失败提示，不推断为文件损坏或内存不足。

## 目录扫描

Photos 通过 `ImageLibrary::ScanPhotoLibrary` 先读取根目录，确认可选的 `/photos`、`/DCIM` 是否存在，再按原有深度扫描相册。相册为空时仍使用根目录扫描回退；任一受扫描目录读取失败都会清除部分集合并显示错误，不使用 `Exists=false` 掩盖 I/O 失败。Photos 保留独立的列表和全屏页头。

Files 在每次读取前记录目标目录并清除旧 entries。进入子目录失败后，Retry 重试该目录，Back 尝试父目录；刷新失败不能继续打开旧文件行。真实空目录显示 `Empty folder` 与 Refresh。服务稍后注册或 SD 恢复后可以显式重试。`ListDirectory` 的未挂载失败设置 `ENODEV`，目录读取错误在日志输出后保留原 errno；调用者先清 errno，不借前一次操作的错误猜测原因。

## 预览与资源

图像接口返回成功、格式不支持、读取失败、已知分配失败或一般解码失败。完整读取和资源所有权分别检查；不把类型未知的解码错误都归为内存不足。Files 预览错误持续显示，Retry 保留原文件路径；重试前重新检查 FileService 和存储状态，成功后隐藏错误和按钮。

替换、返回、刷新与销毁先从 LVGL image 对象分离旧 source，再丢弃 cache 和拥有的图像。关闭应用会取消其待执行 Home 请求；销毁须取得 LVGL 所有权，不能因锁超时就释放仍被界面引用的数据。Photos 的缩略图调度失败保留可见提示和恢复入口，不能一直表现为正在加载。

扫描、挂载和图像读取仍是同步操作，重试不新增后台 worker。底层慢卡可能阻塞调用；这项改动不承诺读取期间导航响应或有界取消。显式失败状态、资源安全与真实 SD/内存耗尽是分别验证的范围。

## 验证与开放边界

- `tests/photos_ui` 编译生产 Photos 和 ImageLibrary，覆盖扫描结果、读取/解码失败、缩略图及重试；硬件依赖和 JPEG 解码使用明确替身。
- `tests/file_manager_ui` 编译生产 Files、PhoneAppHost 与真实 LVGL；FileService 和图像结果为可控替身，单独验证目录/预览状态、路径、操作和释放顺序。
- `tests/camera_capture` 使用生产 FileService 与真实临时主机目录，验证未挂载/缺失目录错误经过 adapter 传播；底层目录读取故障另由 `tests/file_directory` 覆盖。

测试截图中的字体/图标为 host 替身，不证明设备字体或 GT911 实体触摸。软件工作由 [#35](https://github.com/rymcu/rodakos/issues/35) 跟踪；真实 SD 缺失/拔卡/慢卡、功能恢复、任意 OOM、并发和资源归还、八小时 soak 由 [#28](https://github.com/rymcu/rodakos/issues/28) 保留。正常读取关闭不证明掉电持久性，设备 011 的既有证据不能自动代表新媒体实现通过实机验收。

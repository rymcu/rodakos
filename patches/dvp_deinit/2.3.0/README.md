# DVP 分阶段清理

ESP Video 2.3.0 的 `destroy_dvp_video_device()` 原顺序为 SCCB、sensor、video/VFS、
DVP port。`esp_video_vfs_dev_unregister()` 使用 `asprintf` 分配路径，因此真实内存不足
可以让 VFS 注销失败；此时 SCCB 与 GC0308 sensor 已释放，仍注册的 video 对象却保留旧
sensor 指针。外层保留 Board Manager 句柄后直接重试，又会访问已释放对象。

此 overlay 只替换受 SHA-256 约束的完整 `esp_video_init.c`，不写 managed component。
受审查版本为 ESP Video 2.3.0、ESP Cam Sensor 2.3.0、ESP-IDF 6.0.2，目标是 ESP32-S3 / GC0308
和 Board Manager 借出的 I2C bus。`provenance.json` 固定参与所有权判断的源码及生成结果。

新顺序先保存 sensor 指针，注销并销毁 video/VFS，再依次删除 SCCB、sensor、DVP port。
在此版本中，`esp_video_device_common_free()` 和 `esp_video_destroy_dvp_video_device()`
只释放 video/common/DVP 包装对象，不释放 sensor 或 SCCB；因此先移除公开入口不会丢失它们。

| 步骤 | 成功后的所有权 | 失败后的处理 |
| --- | --- | --- |
| 查找 camera | 尚未删除资源 | 不创建待清理上下文，允许再次查找 |
| 删除 video/VFS | 私有上下文持有 sensor/SCCB，VFS 已不可重新打开 | 保留所有资源，重试此步骤 |
| 删除 SCCB | sensor 仍由上下文持有，SCCB 字段清空 | 不重复 video 查找或销毁 |
| 删除 sensor | 上下文不再保留 sensor 指针 | 不重复 SCCB 删除 |
| 删除 DVP port | 各清理 API 均成功，清空上下文 | 只重试 port，不访问已删除的 video/sensor/SCCB |

状态只在现有 `s_init_lock` 内改变。只在全部成功后清空上下文，由原有调用者清 DVP
initialized flag；后续生命周期重新建立自己的上下文。DVP 清理未完成时，新的 DVP init
在操作其他设备之前返回 `ESP_ERR_INVALID_STATE`，不会误报已初始化或清理其他设备。
不含 DVP flag 或不提供 DVP 配置的 init 请求保持原有处理流程，包括使用默认 ALL flags
的纯 JPEG 配置。创建 DVP 前失败不产生此上下文；DVP 创建完成后
其他设备 init 失败，原 `fail1` 清理仍进入同一分阶段函数，未完成的状态不会被重新初始化覆盖。

当前 BigSmart 的 `sdkconfig` 只启用 DVP video，未启用 DVP 后置的 JPEG/H264 等 video
初始化。host 的 `init_cleanup_failure` 额外开启 JPEG，只验证 SDK 函数保留未完成上下文并
可通过显式 deinit 收尾；它不证明 subtype / Board Manager 会自动恢复该初始化失败。
现有 DVP subtype 在 init 返回失败时仍释放自己的包装句柄和 I2C 引用；若将来启用后置
video 设备，必须先补齐该初始化失败的外层所有权和清理入口，不能直接沿用本轮验收。

下层失败必须保留尚未成功释放的资源：审查的 SCCB 删除先检查 I2C remove 结果再释放包装；
GC0308 delete 释放 sensor 后恒定返回成功；有效 controller 0 的 IDF port deinit 恒定返回成功。
host 在这些边界注入“释放前失败”，不能由此推断未来驱动返回错误后仍保留资源。任何受审查
源文件变化都会拒绝生成，必须重新核对所有权语义。

API 成功不等于底层时钟引用已回收。独立审查发现此 IDF 6.0.2 的 DVP init 和 deinit 均调用
`PERIPH_RCC_ACQUIRE_ATOMIC`，后者未与初始化配对释放；此 overlay 保留该原始实现，port
fake 只验证调用顺序与软件上下文，不作为 RCC 回收证据。底层引用修复和硬件资源回收仍是
独立门禁。

验证见 [`tests/dvp_deinit`](../../../tests/dvp_deinit/README.md)。本修复不证明真实 WiFi、
Camera 图像、内部 SRAM 余量、物理 I2C 错误恢复或设备长期稳定性，也不把任意 init 失败
的所有底层清理扩展成已验证范围。

# Camera STREAMOFF 阶段诊断

本 overlay 只定位 Camera 首次停流卡点，不修复驱动生命周期或修改超时。
`prepare_camera_teardown_patch.py` 校验两个 2.3.0 组件、完整依赖图、项目 manifest、
诊断 C ABI 头及 ESP-IDF 6.0.2 的相关源文件。源文件哈希按 LF 归一化。
依赖锁使用 IDF / host CI 已提供的 `ruamel.yaml` safe loader；与 `firmware_ci.py` 相同，
只排除跨平台重新解析会变化的顶层 `manifest_hash`，其余全部字段经排序、紧凑分隔的
canonical JSON（`ensure_ascii=True`）计算 SHA-256。包哈希、版本、来源、依赖边、
direct dependencies、target、锁格式版本及未知新增字段均不可漂移；嵌套同名字段不豁免。
生成文件位于 `build/rodak_patches/camera_teardown/`；CMake 要求每个组件恰好替换一个
源文件，managed components 保持原样。升级依赖必须重新审阅 provenance 和替换边界。

`common_video_stop` 在 sensor `S_STREAM`、controller stop、disable、del 前后各记一次。
`dvp_cam_ctlr_del` 在 task delete、GPIO disable、capture stop、GPIO ISR remove 前后记录；
其 DMA 清理在 GDMA disconnect、delete 前后记录。每次调用只求值一次，返回标记保存
原始错误码；原有早退、错误日志、忽略清理错误、释放顺序及 handle 清空时机保持不变。

正常路径的驱动阶段顺序为：

```text
5 6 7 8 9 10 11 13 14 15 16 17 18 19 20 21 22 23 24 12
```

加服务层的 4 个标记共 24 个，低于一次开机固定 32 槽容量。诊断新增代码不创建日志、
堆对象、任务、TLS 或锁。两个 DVP 内部启动失败 DMA 清理调用传 `false`，只保留原清理；
正常 controller del 传 `true`。但 video 启动失败若走完整 controller del，仍会记录 DVP
阶段。槽位不 reset、不环绕，第二次停流或此前失败清理可能导致满槽丢弃；必须结合诊断
模块的 committed sequence、pending 与 drop 标志解释，不能假设每次运行都有完整 24 槽。

以下间隙未单独标记：可选 `intf->stop` 在 sensor returned 与 controller stop enter 之间；
`cam_hal_deinit` 在 capture returned 与 GPIO remove enter 之间；末尾 heap/queue 释放在
GDMA returned 与 common del returned 之间。停留在某个 returned 标记后，不能归责于已返回
的操作。当前 ESP32-S3 配置没有启用 video byte-swap 的可选 stop。

`tests/camera_teardown_patch` 从受 pin 保护的生成源提取四个真实函数并作为 C 编译，验证
调用次序、错误行为、24 标记预算、记录丢弃不影响驱动以及不返回时缺失 returned 标记。
生成器测试逐项拒绝输入漂移，检查 managed 不变、幂等、CRLF、输出目录、替换次数和实际
CMake source 替换；这些 host 证据不等于硬件停流恢复或实时性证明。最终固件仍需核验
compile commands、map/ELF 与实际设备阶段快照。

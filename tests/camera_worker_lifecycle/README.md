# DVP worker 生命周期验证

此目标从受 pin 保护的生成 overlay 提取完整真实 C worker、controller 构造/删除、DMA
half 计算及关联生命周期函数，单独按 C 编译。controller 类型文本也来自生成源码；其底层
SDK/FreeRTOS 类型和 API 是 host fake，不能将宿主类型大小视为 ESP32-S3 ABI。
`production-sources.json` 保存完整生成源和实际编译片段的 SHA-256、函数名单及来源。

13 项正向场景覆盖：

- PSRAM-only 的 3,072 B 栈、优先级 23、原实际 7,680 B ring；WithCaps 创建/删除配对。
- controller/ring/descriptor 分配失败，queue 创建失败，以及实际 WithCaps 调用返回
  stack/TCB 分配失败时的既有资源清理。stack/TCB 两种原因由 SDK fake 建模，不是执行
  FreeRTOS 分配器。
- queue 空/满、receive 前后请求退出、已进入的真实 worker callback 和 capture start
  允许正常结束；worker 可早于创建者发布 task handle 开始执行。
- worker 在真实日志路径持有 host stdout mutex 时，owner 必须等待其释放；worker 已发布
  quiesced 但尚未释放 controller lock 时，owner 仍不得删除或释放资源。
- callback 持 controller lock 时同步请求 self-delete，必须在拿锁和日志之前拒绝；空 handle
  拒绝，以及三个独立 create/delete 生命周期无遗留资源。

027 基线的完整原生成 worker 在“日志锁仍持有”场景明确失败：真实 del 函数直接进入
fake `vTaskDelete`，违反不得删除持锁 worker 的断言。测试不异步杀死 C++ 线程，也不以
永久 hang 作为红例；该旧异步删除路径延迟实际 free 到线程回收，避免宿主 UAF。
修复后的 fake owner `vTaskDeleteWithCaps` 同步 join worker，随后生产 `heap_caps_free`
立即 `std::free`，供 ASan 检查正常收尾。该模型核验调用边界，不证明真实设备锁 owner。

6 个源码负变体分别取消合作等待、删掉 receive 后检查、使用错误 owner delete API、把
stack 改回 internal、提前发布 quiesced、owner 不拿锁读 quiesced。每个须完整编译并在指定
断言失败；编译失败、timeout、崩溃及 sanitizer 错误不计检出。

```sh
# Python interpreter must include the existing overlay dependency ruamel.yaml.
cmake -S tests/camera_worker_lifecycle -B ~/.cache/rodakos-camera-worker -G Ninja \
  -DRODAKOS_IDF_PATH=/path/to/esp-idf-6.0.2 -DCMAKE_BUILD_TYPE=Debug
cmake --build ~/.cache/rodakos-camera-worker
ctest --test-dir ~/.cache/rodakos-camera-worker --output-on-failure
```

本地统一入口 `tools/run_release_host_checks.sh` 已登记本目标，并传入 IDF 路径；其两个
CTest 分别执行正向场景与六个完整编译的负变体。

ASan/UBSan/leak 使用独立目录，并给 C、C++、链接器加相应 sanitizer 参数。原
`camera_teardown_patch` 继续验证24标记预算、真实关闭函数、错误返回和生成器漂移拒绝；
`camera_capture` 保留 ioctl 内与返回后日志阻塞的区分；最终固件仍需 recorder ELF 门禁。

本目标不模拟真实 GPIO/GDMA IRQ drain、cache-off、传感器数据质量、PSRAM stack 实时时序
或剩余 DMA。IRQ 注册/解绑的旧跨核边界没有在此切片中扩展保证。真实包、串口、阶段记录及
低内存并发验收由独立实机窗口记录；027 的失败不能被本地绿灯追溯改写。

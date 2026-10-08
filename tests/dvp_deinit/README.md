# DVP 清理重试回归

此目标从哈希核验后的 ESP Video 2.3.0 原源及生成 overlay 中逐字提取 9 个完整生产函数，
包括 DVP destroy、sensor destroy、init/deinit-with-flags 和公开 init/deinit 包装；原始
flags、锁与 init DTO 定义同样编译。硬件初始化、视频/VFS、SCCB、sensor、port 和 JPEG
边界使用 host fake。`production-sources.json` 记录完整输入与实际编译片段哈希，并明确
`completeTranslationUnit: false`。最终固件构建使用完整生成翻译单元。

11 个正常场景覆盖：连续两个生命周期及重复 deinit、video/VFS 注销分配失败、SCCB 失败、
sensor 失败、port 失败、lookup 失败、其他设备 flag 保留、DVP 创建前失败、创建 DVP 后
JPEG init 失败且清理再次失败，以及 sensor/port 部分清理后的再次 deinit。

fake 给每个 sensor/SCCB 分配独立对象并记录生命周期。模拟释放后写入 poison，保留
tombstone 直到测试结束；每个 lookup/delete 都在解引用前检查其存活状态。重复访问返回
指定错误并使断言失败，不依赖崩溃、UB 或 sanitizer 报错作为检测成功。

三个完整旧函数负控分别要求原代码在 VFS 失败后的活对象断言、sensor 失败后的过期 SCCB
访问和 port 失败后的已删除 video 查找处失败。runner 只接受返回码 1 和指定断言，拒绝
超时、崩溃或 sanitizer 输出；原版与修复版正常的两轮生命周期均须通过。另一个 generator
用例验证拒绝 managed 目录输出、依赖源码漂移，并保留先前有效生成文件。

```sh
cmake -S tests/dvp_deinit -B "$HOME/.cache/rodakos-dvp-deinit" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DRODAKOS_IDF_PATH=/path/to/esp-idf-6.0.2 \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build "$HOME/.cache/rodakos-dvp-deinit"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir "$HOME/.cache/rodakos-dvp-deinit" --output-on-failure
```

2026-10-09 在 WSL Debian 的 Debug 和 ASan/UBSan/leak 构建均通过 13/13 CTest。
这些是受版本约束的函数及所有权合同验证；外层完整 CameraDevice / Board Device /
dev_camera 链由 `camera_device_lifecycle` 覆盖。真实 ESP-IDF 调度、I2C/SCCB 驱动失败、
内存容量和 Camera 重复打开的设备验收仍需单独进行。

当前 BigSmart 配置只启用 DVP video。`init_cleanup_failure` 在 host 额外启用 JPEG，用于
验证 SDK 函数的失败上下文和显式 deinit 重试；它不证明 subtype / Board Manager 在 init
失败时仍持有可恢复句柄。启用 DVP 后置 video 设备前，必须补齐外层 init 失败所有权。
port fake 同样不证明实际 RCC 引用回收；受审查 IDF 的 deinit 使用 ACQUIRE 的现存问题
见 [overlay 边界](../../patches/dvp_deinit/2.3.0/README.md)。

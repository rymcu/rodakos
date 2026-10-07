# AES DMA cleanup host 验证

本目标单独编译完整、未经裁剪的 `esp_aes_dma_core.c`、`esp_aes.c`、`esp_aes_common.c`，
并复制真实 `aes/esp_aes.h` context/API。修复 TU 仅含受 pin 的两行 cleanup 变更。
host 使用 ESP32-S3 对应的 PSRAM/DMA、小数据优化及 AES interrupt 编译分支；外围头、
内存地址分类、分配器、缓存、RTOS、AES HAL 和锁由下层 fake 提供。P4 分支未执行。

真实 CBC caller 取得锁、设置 key、调用真实 DMA 函数；非 DMA input 和未对齐 PSRAM
output 触发真实 bounce 分支。分配器只注入指定申请失败并记录申请与 free，不替生产函数
补清理。输出保留前后哨兵，验证全长零化、`-1`、无 DMA 启动、锁和 clock 已释放以及
input payload 归还，覆盖 256 字节与大于 1600 字节的请求；随后正常调用可成功。
input 首次失败和 output-only 失败也保持无遗留。

正常控制覆盖 input-only/output-only/两者/direct、1600 chunk 边界和跨三块的数据复制。
HAL 使用可预测字节变换，仅核对 buffer 处理与调用顺序，不冒充真实 AES 算法测试。
旧源与修复源正常输出、申请次数/释放次数/peak payload 逐字比较。host descriptor 大小
按宿主指针布局，peak 数不能当作 ESP32-S3 DMA 预算。

`aes_dma_upstream_negative_and_normal_parity` 完整编译同 pin 的旧源，必须返回 1 并出现
指定 `state.outstanding_bytes == 0` 失败；崩溃、timeout 或 sanitizer 报错不计检出。
只有故意运行旧泄漏路径的子进程设置 `detect_leaks=0`，以明确所有权断言判红；修复正例
及旧源正常控制保留 ASan/UBSan/leak 设置。首次旧源失败及当时源码另由证据清单保留。
所有 host target 保持 assertions；分配 API 调用不放在 assert 的条件中。

六个 Python generator 测试覆盖逐 pin 漂移拒绝、最小 diff、幂等、LF/CRLF 等价、拒绝向
SDK 写生成文件，以及 CMake 正例和七类目标/源/芯片负例。本地统一入口
`tools/run_release_host_checks.sh` 已登记该目标；不依赖 GitHub Actions。

```sh
cmake -S tests/aes_dma_cleanup -B ~/.cache/rodakos-aes-cleanup -G Ninja \
  -DRODAKOS_IDF_PATH=/path/to/esp-idf-6.0.2 -DCMAKE_BUILD_TYPE=Debug
cmake --build ~/.cache/rodakos-aes-cleanup
ctest --test-dir ~/.cache/rodakos-aes-cleanup --output-on-failure
```

ASan/UBSan 使用独立目录、对应编译/链接参数，`ASAN_OPTIONS=detect_leaks=1`、
`UBSAN_OPTIONS=halt_on_error=1`。固件 target 链接、真实正常峰值、低内存恢复、并发及
持续运行需另行验证；此目标不解释 027 的故障分支或锁 owner。

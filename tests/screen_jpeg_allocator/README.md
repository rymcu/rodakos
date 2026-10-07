# 屏幕 JPEG 分配与链接门禁

本目标编译生产 `main/phone_os/screen_jpeg_allocation.cc`；codec 原 allocator 和 heap 为确定性
host fake。10 项合同测试覆盖 scope 外参数/策略透传、四入口 PSRAM 分配、嵌套/异常恢复、
PSRAM OOM 不回退内部堆、逐点失败清理和重试、零大小/溢出/对齐、scope 外释放及双线程隔离。
真实 `DisplayService::EncodeJpeg` 的 scope 位置、所有权、close 顺序和 callback 恢复另由
`tests/display_service` 完整生产 TU 的 30 项测试覆盖。`tests/home_ui` 同样链接生产 adapter，
其 JPEG codec 保持错误返回 fake，负责真实 LVGL 捕获镜像回归。

```bash
cmake -S tests/screen_jpeg_allocator -B "$HOME/.cache/rodakos-jpeg-allocator" \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build "$HOME/.cache/rodakos-jpeg-allocator" -j 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir "$HOME/.cache/rodakos-jpeg-allocator" --output-on-failure
python3 tests/screen_jpeg_allocator/run_negative.py --output "$HOME/.cache/rodakos-jpeg-negative"
```

负对照把生产文件复制到指定目录后变异，不修改源码。全局 bool、scope bypass、INTERNAL fallback、
删除真实 `EncodeJpeg` scope 四种变异必须以显式测试失败被检出；崩溃不是通过条件。
`tools/run_release_host_checks.sh` 包含正向 CTest 与 Python 检查器，GitHub host CI 还执行负对照。

`cmake/screen_jpeg_allocator.cmake` 在配置时核验 ESP32-S3 / IDF 6.0.2 / `esp_new_jpeg 0.6.1`
archive、公共头、manifest、lock 和 DWARF 四个签名。编译和 preflight 拒绝
`CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS=y`，因为恢复策略要求分配失败返回空指针。
构建后的强制门禁读取最终 ELF 与同次 map，生成 `build/screen-jpeg-linked.json`；
preflight 单独成功不代表最终链接检查通过。

`ScreenJpegAllocationScope` 仅在屏幕 `EncodeJpeg` 期间改变当前任务策略，先于 Encoder RAII
构造并在其关闭后恢复。`task_enable=false` 保持不变；Camera/decoder 不建立 scope，走原策略。
四个 wrapper 均需链接：两个 public allocator 也存在 INTERNAL fallback，只包装 inner 不充分。

可选 CMake `RODAKOS_JPEG_BASELINE_ELF` 保存上一制品路径，最终报告记录 native TLS 每任务对齐
开销差。native TLS 位于任务栈，新增字节可能使对齐后的大小增加；没有动态 TLS 对象/析构注册。

旧 archive 的 `.xt.prop` 被 IDF 6 section GC 丢弃，直接 objdump 不能可靠解码。门禁只为分析
复制最终 ELF，将非 ALLOC `.xt.prop` 节名改成等长 `.no.prop`，然后逐项确认全部 ALLOC 节与
PT_LOAD 的地址、大小及字节哈希不变、全部符号不变。仅对 pin archive/map 已确认的 retained
codec 函数做入口/分支可达遍历，跳转后不穿过 padding；未知间接跳转、越界、重叠、缺失预期
allocator 调用或 retained caller 的 unknown callx 均拒绝。wrapper 和 native TLS 仍审原 ELF。
分析副本不是固件制品，不能刷写；既不修改正式 ELF，也不用 KEEP 拉回未使用 codec 代码。

host 与链接门禁不证明真实 JPEG 质量、吞吐量、DMA 连续块、低 PSRAM 下实际 codec 清理，
也不证明控制 ACK 时延或 Camera/语音并发已恢复。须保留设备同帧四阶段 heap、持续编码、
启停/OOM 恢复和前后 TLS/任务栈水位的独立证据。

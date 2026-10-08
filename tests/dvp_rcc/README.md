# DVP RCC 引用计数回归

此目标从 ESP-IDF 6.0.2 的固定输入生成 `esp_cam_ctlr_dvp_deinit()` overlay，唯一变化是将错误的 `PERIPH_RCC_ACQUIRE_ATOMIC` 改为配对的 `PERIPH_RCC_RELEASE_ATOMIC`。生成器拒绝写入 IDF 或 managed component，并在输入漂移时拒绝生成。

Debug 与 ASan/UBSan/leak 各通过 6 项 CTest：单次、32 次重复、共享 LCD_CAM owner、无效 controller、旧逻辑指定断言负控和 generator 输出/漂移保护。host 使用与 IDF 相同的 RCC 宏和计数模型；不证明真实 GPIO、DMA、Camera 时钟或硬件长稳。

```sh
cmake -S tests/dvp_rcc -B ~/.cache/rodakos-dvp-rcc -G Ninja \
  -DRODAKOS_IDF_PATH=/mnt/c/esp/v6.0.2/esp-idf \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build ~/.cache/rodakos-dvp-rcc -j 4
ctest --test-dir ~/.cache/rodakos-dvp-rcc --output-on-failure
```

完整 ESP-IDF 构建通过根 CMake overlay 使用完整翻译单元；当前候选仍须在设备上验证 Camera 周期、共享 LCD_CAM owner 和真实资源余量。

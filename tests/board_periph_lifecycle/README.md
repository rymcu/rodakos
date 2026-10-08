# Board peripheral 生命周期回归

此目标编译真实的 `components/esp_board_manager/src/esp_board_periph.c`，用宿主 fake
描述表和初始化/反初始化回调验证引用计数合同：底层释放失败或缺少释放回调时，引用
计数必须恢复，后续 `unref` 必须再次调用真实释放回调；成功释放后句柄才清空。

`legacy` 变体恢复修复前的生产分支。负控脚本要求旧实现的两个场景都失败，并检查失败
断言，避免把崩溃或编译失败误当作回归证据。

```sh
cmake -S tests/board_periph_lifecycle -B build/host-evidence/board-periph -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host-evidence/board-periph -j 4
ctest --test-dir build/host-evidence/board-periph --output-on-failure
```

目标只验证 board manager 的宿主引用计数，不代表 ESP32 I2C 总线、SCCB、Camera 或
硬件停流行为已经通过；这些仍需独立的 IDF 编译和设备窗口。

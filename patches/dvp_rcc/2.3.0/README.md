# DVP RCC 引用计数修复

ESP-IDF 6.0.2 的 ESP32-S3 DVP controller 在 `esp_cam_ctlr_dvp_deinit()` 中误用了
`PERIPH_RCC_ACQUIRE_ATOMIC`。该宏递增共享 `LCD_CAM` RCC 引用；与初始化的 acquire 配对后，
每个 DVP 生命周期会泄漏引用，重复打开 Camera 还会让引用计数继续增长。

本 overlay 只把该函数内唯一的 `PERIPH_RCC_ACQUIRE_ATOMIC` 替换为
`PERIPH_RCC_RELEASE_ATOMIC`。IDF 安装目录不被修改；生成器固定 driver、RCC 宏和计数实现、
ESP32-S3 LCD_CAM 映射的 LF 归一化哈希，输入漂移时拒绝生成。

主机回归使用与 IDF 相同的 RCC 宏和 `periph_ctrl.c` 计数逻辑，覆盖单次生命周期、重复周期，
以及另一个 LCD_CAM owner 存在时 DVP 退出不能提前关闭总线时钟。旧 driver 函数作为负控运行，
必须命中指定断言失败。主机证据不替代真实 DVP、GPIO、DMA 或硬件时钟验收。

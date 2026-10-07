# ESP-IDF 6.0.2 AES DMA 失败清理覆盖层

`esp_aes_process_dma_ext_ram` 在 input bounce 已申请成功、output bounce 申请失败时，
原实现直接返回 `-1`，跳过已有 cleanup，丢失 input buffer。ESP32-S3 上该 payload 为
`min(len, 1600)` 字节，使用 `MALLOC_CAP_DMA`；它不包含分配器开销。

覆盖层只将这处返回改成 `ret = -1; goto cleanup;`。原输出全长零化、错误日志、错误码、
分配 caps、chunk 大小及正常处理路径保持。原 CBC caller 自己释放 AES 硬件锁；补丁没有
新增锁或资源。此缺陷不能单凭源码归因于 027 Camera 故障，也不解决正常运行峰值。

生成器校验 provenance 中的 IDF 源文件、版本头及构建入口。根 CMake 仅为 ESP32-S3 的
非 imported 静态 `tfpsacrypto`（输出同名）替换唯一的原 DMA TU；缺失、重复、目标类型、
输出名或芯片变化均拒绝。生成文件位于 build 的 `rodak_patches/aes_dma_cleanup/`，
不修改 IDF 安装或 managed components。版本升级必须重新审查 pins 和真实调用路径。

`tests/aes_dma_cleanup` 编译完整 core、caller、common 三个 C 翻译单元，保留真实公共
context/API 头。它从真实 CBC 调用进入分配失败并验证零化、已分配 payload 归还及原锁
释放，同时以完整旧源作明确断言负控；正常路径的结果及分配记录与旧源逐字比较。
SDK/RTOS/HAL 为 host 替身，不能证明加密结果、真实 DMA/cache/IRQ 时序或硬件内存余量。

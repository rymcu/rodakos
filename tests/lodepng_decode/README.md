# LodePNG 16 位解码与覆盖层

本目标编译项目锁定的 LVGL 9.3.0 及 build-local checked overlay。生成器只使用
Python 标准库构造合法 PNG 和 CRC；测试覆盖灰度、灰度 alpha、RGB、RGBA 的
8/16 位输入、RGBA8 的 filter 0–4，以及 RGBA8 和 16 位 RGB/RGBA 的 Adam7 交错输入。
当前共有 11 个合法变体与 2 个几何拒绝样本。

每个样本同时经过 lodepng_decode32 和 LVGL RAW_ALPHA decoder，逐字节核对
RGBA/BGRA 像素。宽度超过 LVGL 16 位 image header，以及宽度仍合法但 RGBA stride
达到 65536 的 PNG，都必须返回 LodePNG 错误 92，不能进入分配或发生整数截断。

新增的 471×423 RGBA8 合成样本与设备 A3 的尺寸、位深和色型一致，输入内容不同：
合成 PNG 为 9,673 B，实际 A3 为 69,200 B。它轮换 filter 0–4，并使用生产 checked overlay。
`allocation_probe.c` 通过链接包装真实 `lv_malloc_core / lv_realloc_core / lv_free_core`
记录每笔分配、重分配和释放，不替换解码器。测试分别拒绝 IDAT 聚合、inflate reserve 和
收养像素时的 draw-buffer descriptor 分配；每次必须返回 83、没有输出和残留分配，随后
解除拒绝、重新解码必须逐字节匹配全部像素。正常结果仅持有一个像素分配和一个 descriptor。

Huffman 解压在结束码后仍要求 260 B 余量。旧 reserve 先分配 797,355 B，到末尾再按容量
增长规则申请 1,196,213 B；新 reserve 一次申请 797,615 B。测试施加 1,081,344 B 最大单块
请求限制，旧实现返回 83，新实现须成功。只把 build 目录生成源码的 reserve 段换回旧逻辑
的负对照已重现该失败，再恢复 checked overlay 后通过；没有改动 managed component。
该合成样本修复后的 decoder ledger 峰值为 818,804 B，所有场景释放后均为 0；此值不包含
调用方持有的编码 PNG、屏幕流和其他服务，也不代表实际 A3 或设备峰值。

ASan/UBSan 用法与其他 host suite 相同。此目标验证依赖覆盖层、像素安全及上述三个分配
失败位置的回收和恢复，不证明任意硬件 OOM、实际堆碎片、SD I/O 或屏幕/相机并发稳定性。

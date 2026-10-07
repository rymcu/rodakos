# LodePNG 16 位解码与覆盖层

本目标编译项目锁定的 LVGL 9.3.0 及 build-local checked overlay。生成器只使用
Python 标准库构造合法 PNG 和 CRC；测试覆盖灰度、灰度 alpha、RGB、RGBA 的
8/16 位输入，以及 16 位 RGB/RGBA 的 Adam7 交错输入。

每个样本同时经过 lodepng_decode32 和 LVGL RAW_ALPHA decoder，逐字节核对
RGBA/BGRA 像素。宽度超过 LVGL 16 位 image header，以及宽度仍合法但 RGBA stride
达到 65536 的 PNG，都必须返回 LodePNG 错误 92，不能进入分配或发生整数截断。

ASan/UBSan 用法与其他 host suite 相同。此目标验证依赖覆盖层和像素安全，不证明设备
峰值内存、SD I/O、屏幕并发或真实图片 A3.PNG 的具体 IHDR。

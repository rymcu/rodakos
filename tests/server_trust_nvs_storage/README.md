# 真实 NVS 连续页存储回归

本目标直接编译 ESP-IDF 6.0.2 的 `Storage / Page / PageManager / Item / HashList` 五个
实现文件、官方 Linux ROM CRC 实现，以及生产 `server_trust.cc / server_trust_route.cc`。
没有通过 Settings map 替代 NVS 分配算法。

输入完全合成：从全 `0xff` 的六页 RAM 分区开始，用 SDK `Page::writeItem / eraseItem`
构建五个有效、含已擦除空洞的页，另保留一页供 GC。命名空间 `fixture`、保留载荷和填充均为
测试常量。仅复用仓库已有的公开测试证书 `tests/server_trust/test-ca.pem`，不读取私钥、设备
备份、用户数据库、真实命名空间或凭据。目标不接受输入文件路径。

六项回归验证：

- 等值 authority 不产生新写入或擦除。
- 旧 v2 双份 CA 记录需要的连续槽比最佳压缩后页多一个；即使总可用槽充足，真实 SDK 返回
  `ESP_ERR_NVS_NOT_ENOUGH_SPACE`，旧记录与合成保留载荷在即时读取和重新初始化后均完整。
- 生产 v3 Encode 在完全相同的初始页布局中写入成功，并经生产 Decode 还原两个端点、pin、
  绑定要求与数值连接地址。
- v2 分配失败后的同一 RAM 分区可直接写入 v3。
- 30 轮 pending 写入、晋升 active、端口与数值 route 迁移（60 次更新），每步重新初始化并
  读回核验，且确认实际发生 GC。
- 真实 NVS 接受含 NUL 共4000字节的 string，拒绝4001字节，并保留旧值。

`sdk-provenance.json` 固定24份 SDK 源码、相关头文件和版本头的 LF 归一化 SHA-256。
CMake 配置阶段逐项核对，输入更改会重新配置；未知 SDK 必须重新审阅，不能仅更新版本号。

```sh
cmake -S tests/server_trust_nvs_storage -B build/nvs-host -G Ninja \
  -DRODAKOS_IDF_PATH=/path/to/esp-idf-v6.0.2 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/nvs-host
ctest --test-dir build/nvs-host --output-on-failure
```

发布 runner 同时给所有编译的 C/C++ 源码加 ASan / UBSan，并开启泄漏检查。系统提供的
mbedTLS 动态库本身没有重新插桩；host malloc 不证明设备 PSRAM / DMA 分配，RAM NOR 后端
也不模拟硬件 flash 驱动、任意写入中断或全部掉电情形。这里证明的是连续页/GC边界与完整
记录替换行为，不能据此把某一次未记录底层错误码的实机失败认定为已确诊。

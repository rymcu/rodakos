# WebSocket 3xx 拒绝补丁

上游 `espressif/esp_websocket_client` 1.8.0（Apache-2.0，提交
`70bf122fc8bc74622dcf7e15233b5b2af9088df5`）会在握手收到 3xx 后改写 URI 并再次连接，
即使关闭了自动重连也会跟随 `Location`。这会绕过 RodakOS 对逻辑 origin、固定证书身份和
数值连接地址的先行校验，并继续携带原授权头。

`reject_redirect.c` 仅替换该分支，拒绝所有 300–399 返回值以及底层记录的 300–399 HTTP
状态。后者覆盖底层未归入 301/302/303/307/308 的响应：它们可能通过其它握手检查而返回 0。
无论 `Location` 是跨源、同源、相对地址、错误格式还是缺失，都不会读取或跟随它。
负的 transport 返回值继续走上游失败路径。

拒绝事件只报告 HTTP 状态，不输出 `Location` 或授权信息。`ERROR` 和 `DISCONNECTED`
使用 `WEBSOCKET_ERROR_TYPE_HANDSHAKE`，并调用原有 abort 流程关闭 transport。
关闭自动重连时退出；启用时仅按原退避时间重试原地址。URI、Host、路径、端口、认证头、
固定证书、SNI / common name 和证书校验开关均保持原值。停止、销毁和锁顺序沿用上游。
该补丁适用于此固件内所有 WebSocket client，不提供自动跳转例外。

构建生成器核对项目 pin、锁文件的版本 / registry / package hash、managed package hash、
上游 repository / commit / path，以及完整客户端源码、头文件、元数据、许可证、CMake 和
ESP-IDF 6.0.2 的 WebSocket transport / 头文件 / 版本头的 LF 归一化 SHA-256。
任一不符即停止配置。只向 `build/rodak_patches/esp_websocket_client/` 生成副本，
`cmake/websocket_redirect_patch.cmake` 要求恰好替换一个目标源文件；不修改 managed sources。
升级 SDK 或组件时必须重新审阅 provenance 和实际控制流。

主机回归位于 `tests/websocket_redirect_patch/`：从生成的生产副本提取完整 task、abort、
error、stop 函数，使用确定性 transport / RTOS fake 编译运行，不重写状态机。
覆盖所有 100 个 3xx、6 种 Location、底层返回成功但 HTTP 为 3xx、缺失 Location、
原地址重连、线程等待期间调用真实 stop、销毁路径、101 和负的 transport 返回值。
未修改上游运行相同断言的三个负对照必须失败；CTest 将这些预期失败记为通过。
TLS 网络握手和证书验证由实际固件 / 硬件验证承担，主机 fake 不构成真实 WSS 证明。

```sh
export RODAKOS_IDF_PATH=/path/to/esp-idf-v6.0.2
cmake -S tests/websocket_redirect_patch -B build/ws-host -G Ninja \
  -DRODAKOS_IDF_PATH="$RODAKOS_IDF_PATH" -DCMAKE_BUILD_TYPE=Debug
cmake --build build/ws-host
ctest --test-dir build/ws-host --output-on-failure
python3 -m unittest discover -s tests/websocket_redirect_patch -p 'test_*.py'
```

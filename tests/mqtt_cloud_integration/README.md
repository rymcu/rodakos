# MQTT 与设备云凭据集成测试

本目标同时编译完整生产 `UnifiedMqttService` 和 `DeviceCloudConfigService` 翻译单元。
HTTP、NVS Settings、FreeRTOS 与 MQTT SDK 使用下层 host fake；通过
`RODAK_MQTT_REAL_CLOUD` 排除 MQTT 测试运行时中的 Cloud 替身。

6 个正向用例覆盖语音延期后的最新令牌、SDK 初始化与 attach 之间的真实
`PrepareVoiceConfig` 轮换、USB 代次取消、跨 namespace 持久化与回滚失败、
legacy MQTT-only 缓存，以及要求绑定身份但缺少 AIoT 凭据的缓存。
2 个独立完整 Cloud 源码负变体分别移除精确凭据比较或绕过接纳检查，必须编译成功，
并由指定断言拒绝；编译失败、超时或 sanitizer 报错不计为成功检测。

在 Linux/WSL 安装 CMake、Ninja、C++ 编译器和 mbedTLS 开发库后运行：

```sh
cmake -S tests/mqtt_cloud_integration -B /tmp/rodakos-mqtt-cloud -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build /tmp/rodakos-mqtt-cloud
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/rodakos-mqtt-cloud --output-on-failure
```

两个 CTest 分别运行正向集成和负变体。构建目录的 `production-sources.json`
记录实际编译的 MQTT、Cloud 及 policy 源文件哈希；`negative-controls/`
保存变体、编译日志、指定断言与结果。发布 host runner 和 CI 均执行本目标。

此处验证服务之间的并发与持久化合同，不证明真实 NVS commit、物理网络、设备内存容量
或 Camera 清理恢复。SDK 停止等待没有硬超时；成功停止也不保证 FreeRTOS idle
已回收旧任务栈。硬件、完整 SDK 和固件构建仍需各自的证据。

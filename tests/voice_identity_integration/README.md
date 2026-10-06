# Voice identity 真实服务联接

此目标同时编译生产 `UnifiedMqttService`、`VoiceWakeService`、identity 校验与版本记录、
`voice_wake_settings.cc`、`time_service.cc` 和 cJSON。desired 从注册的 MQTT 回调、分片、
消息队列和 worker 进入真实唤醒服务；实际服务进行版本判断、保存、runtime 配置与恢复。
独立 supervisor 使用可控 Unix / monotonic 时钟，实际 MQTT worker 发现状态变化并生成
`shadow/report`。测试和 CLI 只捕获生产发布内容，不生成身份结果。

4 项 host 用例覆盖真实应用与记录保存、重复/冲突、临时身份到期回退并保留水位，以及候选
配置和 rollback 都失败后的 `recovery_failed` / `activeConfirmed: false` 上报。

```sh
cmake -S tests/voice_identity_integration \
  -B ~/.cache/rodakos-voice-identity-integration -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build ~/.cache/rodakos-voice-identity-integration
ctest --test-dir ~/.cache/rodakos-voice-identity-integration --output-on-failure
```

生成的 `rodakos_voice_identity_fixture --device-key=<key>` 接收逐行 JSON：

- `{"op":"desired","topic":"devices/<key>/shadow/desired","payload":"<原始 desired JSON>"}`
- `{"op":"clock","unixMs":1800000060000,"monotonicMs":61000,"waitStatus":"expired"}`
- `{"op":"fail-configuration-and-rollback"}`
- `{"op":"snapshot"}`

`clock` 只改变注入的时钟；故障操作只设置底层 runtime 假接口的两次失败。`waitStatus`
仅等待真实报告出现，不能设置生产状态。每条输出的 `topic` 和 `payload` 来自最近一次真实
MQTT publication；`processed` 和 `runtimeConfigurations` 是测试诊断，不属于 wire 合同。

Rodak 的 `tests/main-integration/voice-identity-gateway.test.ts` 使用此 fixture，经真实配对与
MQTT Broker 下发生产 desired，并把实际捕获的报告原样送入 Broker、数据库和 Base System
Prompt 路径。环境变量为 `RODAKOS_VOICE_IDENTITY_FIXTURE`；Windows WSL 分发版由
`RODAKOS_VOICE_IDENTITY_WSL_DISTRO` 指定，默认为 Debian。Gateway 把设备 Unix 基准设为
Node 的当前时间，避免固定过期日期使未来执行的测试失效。

NVS 驱动、RTOS、唤醒 runtime/assistant 和网络 SDK 是 host 替身。此目标验证生产状态逻辑
与软件报告的完整路径，不证明 Flash 掉电原子性、实际 ESP-SR 命中、PCM/麦克风或无线时序。
原 `tests/mqtt_volume_service` 的 identity 目标只验证严格 JSON 和报告发布，不能替代此联接。

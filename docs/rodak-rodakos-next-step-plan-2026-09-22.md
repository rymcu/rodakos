# Rodak + RodakOS 下一步推进计划

> 适用范围：Rodak 桌面/服务端 + RodakOS 设备云闭环
>
> 功能基线（已推送）：Rodak `d6f4b44`（`origin/master`），RodakOS `9510f1b`（`origin/main`）
>
> 当前计划文档提交：Rodak `4725803`，RodakOS `ebbf323`
>
> 本计划不包含 Xiaozhi pipeline、`runLegacyXiaozhiSpeechPipeline` 或 Xiaozhi session 生命周期重构。

## 1. 当前状态

已完成：

- 两仓同步 `rodak-aiot/v1` 合同，覆盖 canonical identity、binding/token、`unifiedMqtt`、MQTT topics、shadow、voice identity 和 OTA。
- Rodak 新增协议中立的 `AgentDeviceCapabilitySnapshot`、`AgentDeviceCommandResult`、`AgentDeviceEvent` contract。
- Rodak 新增 `agent-runtime/adapters/rodakos`，只接受 canonical product/protocol、可信 credential source 和兼容 protocol version 的设备；它区分产品支持能力与当前授权能力，并对跨 Runtime 的 metadata/payload 做脱敏。
- Rodak `CommandService` 的普通 command event projection 已脱敏；设备实际下发和持久化 command payload 不在本切片改变。
- RodakOS 的 pairing、MQTT credential、voice identity 和 Recovery 相关实现已有静态合同证据与 host test 入口。
- Rodak 的 RodakOS 定向集成测试、renderer typecheck、lint 和 build 已通过。

当前限制：

- 当前电脑没有真实设备。
- 当前电脑缺少 ESP-IDF 6.0.2、`idf.py`、Python、Ninja 和 C/C++ 编译器，因此 RodakOS host CMake/ctest、firmware build 和 hardware gate 尚未执行。
- RodakOS adapter 目前是边界锁定切片，尚未接入 Runtime IPC/API。
- Xiaozhi baseline 仍有独立失败；不将其作为 RodakOS 主线验收条件，也不在本计划中修复。

## 2. 执行分层

### A. 无设备电脑可执行

在当前电脑完成以下工作，不需要连接设备：

1. 保持 Rodak 两仓依赖与静态质量门禁通过：

   ```powershell
   cd E:\workspace\rodak
   pnpm install --frozen-lockfile
   pnpm lint
   pnpm typecheck
   pnpm build
   ```

2. 运行 RodakOS 非硬件主线测试：

   ```powershell
   pnpm vitest run --project main-integration `
     tests/main-integration/server-runtime.test.ts `
     tests/main-integration/server-http-api.test.ts `
     tests/main-integration/device-state-service.test.ts `
     tests/main-integration/server-security-regressions.test.ts `
     tests/main-integration/server-device-mqtt.test.ts `
     tests/main-integration/run-mqtt-ota-v2-gate.test.ts `
     tests/main-integration/agent-runtime-rodakos-adapter.test.ts
   ```

3. 实现 Runtime 只读 capability 入口：

   - 在 `src/main/agent-runtime/api/agent-runtime-service.ts` 增加设备 capability 查询；
   - 在 `src/shared/agent-runtime-api.ts`、`src/shared/ipc-channels.ts`、`src/main/ipc/agent-runtime.ts` 和 `src/preload/bridge.ts` 增加正式 API；
   - 只返回 canonical RodakOS snapshot，不返回 device secret、token、password 或原始 credential payload；
   - Runtime policy 只能使用 `authorizedCapabilities`，不能把设备自报 metadata 当成授权；command/event mapper 必须带 canonical device provenance；
   - 先实现 list/get/read-only，不实现设备 mutation；
   - 增加 main integration、preload 和 renderer contract tests。

4. 补强云闭环的无设备回归：

   - register/activate/token 幂等与并发 credential rotation；
   - binding eligibility、旧 bootstrap migration 和 unbind 后 token revoke；
   - MQTT reconnect、telemetry、shadow desired/report、command ACK；
   - OTA task notify/progress/retry/cancel/anti-rollback；
   - serial provisioning frame、timeout、retry 和 secret redaction。

### B. 有真实设备电脑执行

在接入 RodakOS 真机的电脑上，严格按以下顺序执行：

1. 在两个仓库分别拉取并确认提交：

   ```powershell
   cd E:\workspace\rodak
   git pull --ff-only
   git rev-parse --short HEAD

   cd E:\workspace\rodakos
   git pull --ff-only
   git rev-parse --short HEAD
   ```

   预期分别为 `4725803` 和 `ebbf323`（包含上述功能基线及本计划文档）；若工作树有本地修改，先停下并保留现场，不要用
   `reset --hard` 覆盖它们。

2. 激活并验证 ESP-IDF 6.0.2：

   ```powershell
   cd E:\workspace\rodakos
   . .\activate_idf.ps1 -Version v6.0.2
   .\assert_idf6_environment.ps1
   ```

3. 运行 host app-model tests（可在 Windows CMake 或 WSL Ninja 环境执行）：

   ```powershell
   cmake -S tests/app_model -B build/app-model-tests
   cmake --build build/app-model-tests --config Release
   ctest --test-dir build/app-model-tests -C Release --output-on-failure
   ```

   无设备时还可运行 `tests/home_ui` host tests；这些测试不能替代真机 gate。

4. 构建 firmware 和 Recovery-safe OTA bundle：

   ```powershell
   .\build_rodakos.ps1
   .\build_ota_bundle.ps1
   ```

5. 刷写前只允许先执行 verify-only，确认 bundle manifest、SHA-256、partition offsets、build flavor 和 16 MiB merged image 均通过：

   ```powershell
   .\flash_and_test.ps1 -Port COM<n> -VerifyOnly
   ```

   该模式不写 Flash，但会读取设备并执行一次 reset；先确认现场允许复位。

6. 首次分区迁移或明确批准后，使用 Recovery-safe 脚本刷写：

   ```powershell
   .\flash_and_test.ps1 -Port COM<n> -Erase
   ```

   `-Erase` 会清除 NVS 和 OTA 状态，只能在确认备份/可重新配网后使用。禁止使用裸 `idf.py flash` 或 `app-flash` 覆盖 Recovery layout。生产固件和 `home-hardware-test` flavor 必须分开确认；后者需要显式 `-AllowHomeHardwareTestPopulation`，验证后必须切回 production flavor 并重新构建。

7. 保存以下证据并回传到 Rodak 任务：

   - host `ctest` 输出；
   - firmware build summary 和 OTA manifest；
   - first-boot serial log；
   - pairing -> token -> MQTT connect -> telemetry/shadow -> command ACK；
   - OTA notify -> download -> SHA-256 -> Recovery boot confirmation -> progress/result；
   - 失败时保留完整 log，不重复擦写设备。

   完整串口/云门禁以 Rodak 仓库的 `docs/device-cloud-hardware-gate.md` 为准。先在
   Rodak 仓库设置同一组目标参数，再按 provisioning-only -> full 顺序执行；两个 project
   都会先运行 `pnpm build`：

   ```powershell
   cd E:\workspace\rodak
   $env:RODAK_E2E_HARDWARE_SERIAL_PORT = 'COM<n>'
   $env:RODAK_E2E_HARDWARE_DEVICE_KEY = '<device-key>'
   $env:RODAK_E2E_HARDWARE_ENV_FILE = 'C:\path\to\device.env'

   $env:RODAK_E2E_HARDWARE_PROVISIONING_CONFIRM = '1'
   pnpm test:e2e:hardware:provisioning
   Remove-Item Env:RODAK_E2E_HARDWARE_PROVISIONING_CONFIRM

   $env:RODAK_E2E_HARDWARE_CONFIRM = '1'
   pnpm test:e2e:hardware
   Remove-Item Env:RODAK_E2E_HARDWARE_CONFIRM
   ```

   运行期间只能有一个串口 reader；不要同时打开 Serial Lab、ESP-IDF monitor 或其他串口工具。
   两种 gate 都需要串口、设备 key、环境文件和当前 server bootstrap URL。若只验证串口/云链路而
   不启动 Playwright/Electron，可改用 `pnpm gate:device-cloud-hardware -- --confirm-provisioning-only ...`
   和 `--confirm-hardware ...`，参数定义以 hardware gate 文档为准。

## 3. Runtime 接入顺序

Runtime 接入必须按只读到可写逐步推进：

1. `DeviceCapabilitySnapshot`：设备 identity、binding status、connection、shadow、产品支持能力和 `authorizedCapabilities`。
2. Read-only API：Agent/Workbench 能查询设备能力，但不直接访问 `DeviceStateService` 或 DAO。
3. Policy gate：将 shadow write、MQTT command、OTA 分别标记为 write/command/destructive 风险，并统一 confirmation/audit。
4. Effect host：由 Runtime effect 执行 server-side command；adapter 只做协议映射。
5. MCP host：发现、授权、调用、审计继续由 `agent-runtime/mcp` 统一管理。
6. Prompt/Skill/Memory：只接收协议中立 identity/capability context；不把 RodakOS 或 Xiaozhi 字段写进公共 prompt contract。

本阶段不创建第二套 session lifecycle。等 Runtime 的 session/event/effect 事实源稳定后，任何协议（包括未来 Xiaozhi）都只能作为 adapter 输入。

## 4. 发布门禁

RodakOS 主线发布前必须满足：

- Rodak `lint`、renderer `typecheck`、Rodak build 和 RodakOS 定向 integration tests 通过；
- RodakOS host app-model tests 通过；
- 真实设备 hardware gate 通过，且使用 Recovery-safe flash 流程；
- canonical `rodak-aiot` identity 未被 legacy bootstrap 覆盖；
- token/secret/password 不出现在普通 event、trace、command 或 serial log；command event 使用脱敏 projection，设备下发 payload 与事件 payload 分离；
- 断网重连、凭据轮换、shadow 冲突、OTA 失败恢复和 provisioning 中断有可复现证据；
- Xiaozhi 专项测试单独记录，不阻塞本非语音发布门禁。

## 5. 停止与回滚条件

- ESP-IDF 版本不是 6.0.2：停止，不构建、不刷写。
- `assert_idf6_environment.ps1`、bundle manifest 或 SHA-256 校验失败：停止，不刷写。
- merged image 不是 16 MiB 或 partition offset 不匹配：停止，不刷写。
- 设备日志出现身份降级、token 泄漏或 Recovery boot 未确认：停止后保留日志，不重复擦除。
- Runtime API 需要直接读 DAO 或引入 Xiaozhi 字段才能完成：停止并先修正 contract 边界。

## 6. 验收产物

每个阶段至少提交：

- 代码/测试提交及变更摘要；
- 通过的命令和测试数量；
- 未执行项目及阻塞原因；
- 硬件阶段的 manifest、hash、serial log 和 OTA result；
- 对应合同字段或 Runtime contract 的变更说明。

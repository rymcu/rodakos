# RodakOS 主机测试 CI

[RodakOS host checks](../.github/workflows/host-checks.yml) 在所有 Pull Request、
`main` push 和手动触发时运行。这是 [#26](https://github.com/rymcu/rodakos/issues/26)
的主机测试阶段；首次 GitHub 成功运行仍需记录具体候选 SHA 和 Actions 链接。

## 环境与依赖

- Linux job 使用 `ubuntu-24.04` runner 上的 `debian:13-slim` 容器，安装 Debian 的
  GCC/G++、CMake、Ninja、`libmbedtls-dev`、Python 3、venv 和 `python3-cryptography`。
  Debian 13 与当前 WSL 主机测试基线一致，使用 Mbed TLS 3.x。容器标签及 Debian 软件包会
  接收更新，因此该 job 不声称位级可复现；实际版本写入 `identities.log`。
- `idf-component-manager==3.0.3` 安装在隔离 venv 中。CI helper 使用该固定版本的
  `SolvedComponent`、`ComponentFetcher` 和目录哈希校验 API；升级 manager 时须同时复验
  [helper](../.github/workflows/scripts/prepare_host_dependencies.py)。
- 从 `dependencies.lock` 读取并下载主机测试使用的 6 个 Registry 组件：
  `chmorgan/esp-libhelix-mp3`、`espressif/cjson`、`espressif/esp_codec_dev`、
  `espressif/esp_websocket_client`、`espressif/mqtt`、`lvgl/lvgl`。只接受 Espressif Registry 的锁定来源，下载后用
  Component Manager 重新计算目录哈希并对比 lock 中的 `component_hash`，不能仅信任
  `.component_hash` 文本。此步骤不重新求解依赖，也不改写 lock 或项目 manifest。
- 按锁文件哈希缓存组件下载源；命中缓存后仍做哈希校验。测试构建目录、生成补丁及签名
  fixture 每次重新生成，不从缓存恢复。
- ESP-IDF 仅获取 `v6.0.2` 的源提交 `7101770dc6db2667b3c477cc31365dd1acd6db4e`，
  用于 MQTT event overlay 的来源核验。主机 job 不安装 Xtensa 编译器，不执行固件构建。
- Actions 使用已核对的完整 commit SHA，工作流 token 只有 `contents: read`，checkout
  不保留凭据；PR 检查不使用 `pull_request_target`。
- 容器内通过运行时 `pwd -P` 确定真实 workspace，再写入 `GITHUB_ENV` 供后续步骤使用。
  Git 仅信任当前仓库和嵌套 IDF checkout 的两个精确目录；artifact 使用 workspace
  相对路径，避免宿主机 `/home/runner/work` 与容器 `/__w` 挂载路径混用。

## 检查覆盖

主入口直接运行 [release host runner](../tools/run_release_host_checks.sh)，包含 App/Home
模型与 LVGL UI、OTA 签名/Recovery 状态机、codec/I2S 错误传播、MQTT event overlay 与
真实服务 fixture、串口配网、服务器信任、唤醒/身份、文件读写及媒体/UI 回归，并运行
OTA/采集器、codec 和 MQTT 补丁生成器的 Python 测试。

工作流额外编译并运行已有的 `voice_volume_service`、`remote_input`、
`display_control_ack_service`、`light_service`、`appearance` 五个 CMake 测试目录。
所有 C/C++ 主机测试启用 ASan/UBSan、栈帧指针和泄漏检查；UBSan 报错会使 job 失败。
这不等于已运行桌面端跨仓库 conformance 调用或真实设备协议握手。

受控修补继续调用现有 generator：codec 校验锁/manifest/包身份和原始 codec/I2S 源码；
MQTT 另校验已审核的 IDF `esp_event.c`。源码不匹配时配置直接失败，不跳过检查，也不
修改 `managed_components` 的源文件。详见 [依赖维护](dependency-maintenance.md)。

依赖准备也包含 `esp_websocket_client`，IDF 源码 checkout 包含 `tcp_transport` 和
`esp_common/include`，供 `websocket_redirect_patch` 使用。本文验证的 `0ef848f` 基线
release runner 未包含该 suite；带有受控 WebSocket 修补及测试入口的后续提交会自动
运行它。下面的基线验证统计不包含该 suite。

独立 `windows-2025` job 用 PowerShell 7 parser 检查全部 Git 跟踪的 `.ps1` 文件语法，
只解析、不执行脚本。该检查不验证 Windows 驱动、Board Manager 生成或刷写行为。

无论测试成功或失败，Linux job 均尝试上传 14 天保留期的 `rodakos-host-<候选 SHA>`
artifact，包含候选/IDF SHA、工具与依赖版本、下载哈希、配置/构建日志、CTest 日志及
runner 输出。配置失败优先查看对应 `<suite>.log`；测试失败查看
`Testing/Temporary/LastTest.log`。CI 没有真实串口、设备凭据或生产签名密钥；签名用例
使用现场生成的测试 fixture，不产生可发布的固件包。

## 本地复跑

已有匹配 `managed_components` 和依赖的 Linux/WSL 环境可直接使用原 runner：

```bash
export RODAKOS_IDF_PATH=/path/to/esp-idf-v6.0.2
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
bash tools/run_release_host_checks.sh "$HOME/.cache/rodakos-release"
```

全新 checkout 应先按 workflow 安装依赖，在 manager 3.0.3 的 venv 内运行
`python3 .github/workflows/scripts/prepare_host_dependencies.py`。其可选 `--destination`
参数用于隔离验证下载，不会改变锁文件。额外五组测试的相同编译命令在工作流中，
不需要开启 IDF shell。

### 2026-10-07 本地验证

使用 `0ef848f` 的独立 LF 归档快照，在 WSL Debian 13 中复跑 release runner 及额外五组：
26 个 CMake 项目、30 个 CTest 目标和 40 个 Python 测试全部通过，C/C++ 启用
ASan/UBSan 与泄漏检查。工作流通过 actionlint 1.7.7、YAML/Bash 语法检查；
11 个受 Git 跟踪的 PowerShell 脚本通过 PowerShell 7.6.5 parser。

manager 3.0.3 已在隔离冷缓存中下载并验证六组件，临时 cJSON 源码篡改被目录哈希检查
拒绝。本地验证没有执行 GitHub runner 容器；后续 Actions 结果单独记录如下。
从 Windows 导出本地快照时使用 `git -c core.autocrlf=false archive`，避免全局换行设置
把要求 LF 的证书 fixture 转成 CRLF。

### 2026-10-07 首轮 Actions 失败与修复

候选 `897bcce` 的 [Actions 37506516770](https://github.com/rymcu/rodakos/actions/runs/37506516770)
通过 PowerShell job，但 Linux job 在记录源码身份时因 Git `dubious ownership` 以
128 退出，尚未执行 Linux 主机测试。该轮同时暴露 job-level `github.workspace`
表达式展开为宿主路径 `/home/runner/work/rodakos/rodakos`，与容器实际 checkout
`/__w/rodakos/rodakos` 不同。

修复在两个 checkout 后，从容器运行目录生成 IDF/结果路径并通过 `GITHUB_ENV`
传递；仅对这两个仓库设置精确 `safe.directory`，上传改用相对路径。此修复重新通过
actionlint、YAML/Bash 静态检查及隔离目录的路径/信任配置验证；不重复已经通过的主机
回归。修复后的 Actions 结果见下一节。

### 2026-10-07 修复后 Actions 通过

候选 `ed63790ec628724711e93db260521292992b2e31` 的
[Actions 37507174582](https://github.com/rymcu/rodakos/actions/runs/37507174582)
已成功完成 Linux host 和 PowerShell 两个 job。此记录只验证该提交：不包含后续的
numeric route、WiFi 恢复或 WebSocket redirect 实现；这些改动需要在新提交上重新运行
CI。artifact 内容和 PR 检查展示仍应随发布验收检查。

## #26 仍开放的门禁

此变更没有建立主应用/Recovery 的 ESP-IDF 构建 job。后续仍需固定推荐 Xtensa GCC、
冷启动生成 Board Manager、验证完整组件依赖图、镜像容量/语音模型符号、普通 flavor
禁用故障注入，以及 development OTA 包的一致性。主机依赖校验只覆盖前述六个组件。

PR 检查展示、artifact 验收及仓库套餐允许的 required checks/保护规则仍需确认。
主机测试成功不会关闭 [发布验收](ota-release-readiness.md) 中的真实掉电、设备资源耗尽、
声学、跨网络和长期运行门禁。

# RodakOS 固件构建 CI

[固件工作流](../.github/workflows/firmware-build.yml) 独立于主机测试，在 PR、`main` push
及手动触发时构建普通功能 flavor 的主应用和 Recovery，生成明确标记的开发签名包。
它属于 [#26](https://github.com/rymcu/rodakos/issues/26) 的固件构建阶段；已通过的候选、
远端运行及下载包复核见下文，仓库 required checks 与生产发布门禁仍独立。

## 固定输入与环境

使用 `ubuntu-24.04` runner 和官方 `espressif/idf:v6.0.2` 容器。启动后检查实际 IDF
commit 为 `7101770dc6db2667b3c477cc31365dd1acd6db4e`、版本为 6.0.2，并根据该提交的
`tools/tools.json` 核验推荐 Xtensa GCC 的安装目录和版本输出。镜像标签本身不是 digest
锁；IDF 或编译器不匹配会失败，不静默接受镜像变化。工具和候选 SHA 保存为证据。

Component Manager 固定 3.0.3，Board Manager 所需 PyYAML 固定 6.0.2。完整 Registry 依赖按锁定版本下载并逐个核验实际目录
哈希，仅接受 Registry 根地址 `https://components.espressif.com` 及带单个尾斜杠的写法；
其他 scheme、端口、userinfo、路径、query、fragment 或镜像地址仍被拒绝，不改写 lock。
构建后的依赖图必须与原 lock 一致。跨平台生成 manifest 的 `manifest_hash`
允许重新计算，依赖、来源、版本、component hash、direct dependencies 和 target 不允许
漂移；最终 lock 会随 artifact 保存。缓存仅包含下载源和 ccache，不缓存生成代码、
sdkconfig、CMake build tree、固件或签名密钥。

为防止冷启动生成 manifest 触发重新求解时升级传递依赖，CI 从原 lock 的全部 service
条目生成精确版本约束，通过 Component Manager 3.0.3 的 `IDF_COMPONENT_CONSTRAINT_FILES`
传入每次 IDF 操作。约束不会添加、删除或放宽依赖；reconfigure 后和完整构建后都执行
原有完整依赖图比较，仅允许 `manifest_hash` 变化。无论比较是否成功，都先保存原/新
lock；失败时输出逐项 diff，不能把版本、路径或来源变化当作平台差异忽略。

## 现有流程的 Linux 编排

[CI 脚本](../.github/workflows/scripts/firmware_ci.py) 对应现有入口：

| 现有来源 | CI 使用方式 |
| --- | --- |
| `assert_idf6_environment.ps1` | 同样核验 6.0.2 和推荐 GCC；适配官方 Linux 的 `IDF_TOOLS_PATH/tools/...`，不伪造 Windows EIM 路径 |
| `generate_board_config.ps1` | 复用仓库 `idf.py bmgr -b rymcu_bigsmart -c <精确 board 目录>`，冷生成两遍再 reconfigure |
| `fix_gen_paths.ps1` | 同规则规范化两个生成文件的本地 board 路径，并拒绝绝对工作区路径和旧 managed board 引用 |
| 根 CMake 的 codec / MQTT / WebSocket overlay | 原样执行来源、版本及源码哈希门禁；不改 managed 源码或其 hash 文件 |
| `build_ota_bundle.ps1` | 同分区、flash 设置、rollback、journal v1、flavor、容量和区段一致性检查 |
| `tools/ota_security.py` | 原工具生成编译期 trust anchor、签名、验签和 `verify-package`，不在 CI 重写密码学实现 |

CI 必须从无 build/generated 目录的 checkout 启动；已有 sdkconfig 移为证据，由 Board
Manager 与项目 defaults 重新生成。实际 `compile_commands.json` 必须包含生成的设备、
外设源码和仓库本地 BigSmart `setup_device.c`，ELF 必须包含两个 board 表符号。
同一编译清单还须使用四个受检 overlay 源文件，不能同时编译对应未修补源。
语音模型的 ELF 起止符号跨度必须与新生成的 `srmodels.bin` 大小一致，不能只检查文件存在。

主应用和 Recovery 使用同一次临时 RSA-2048 开发公钥构建。检查普通 flavor 的 CMake
开关关闭，以及二进制中没有 Home 测试人口和 release fault marker；两份分区表一致，
rollback 打开，main 位于 13.3125 MiB 的 `ota_0`、Recovery 不超过 2.5 MiB factory。
使用 IDF `parttool.py` / `check_sizes.py` 和 esptool 生成 16 MiB merged 镜像，逐一核对
bootloader、分区表、otadata、Recovery、main 的精确区段。

先构建本次 CI 的 `development-baseline`，再把其中不可变资产原样用于
`development-refresh`；两个包均通过现有 `ota_security.py verify-package`，并核对
`flash_and_test.ps1` 所需的普通 flavor/schema/分区与资产哈希字段。CI 不调用
`flash_and_test.ps1 -VerifyOnly`，因为该模式仍会访问和复位真实设备。

这证明本次 CI 内的 Recovery 资产保留，不证明与任一已经安装的设备 Recovery 兼容。
包中 `buildFlavor: production` 表示普通功能开关；`developmentPackage: true` 和
`CI-DEVELOPMENT-ONLY.txt` 明确其使用临时信任根，不能当作生产发布。

## 密钥与输出

每次 run 在 runner 临时目录生成 private key，以 0600 权限保存，离开签名流程时清理。
artifact 路径只包含公开证据、ELF/map、生成 board 文件和两个 development 包，不包含
runner 临时目录；包校验额外拒绝带 private-key PEM 的文件。公共 `ota-public.pem`
随包保留，便于独立验证。CI 不接受生产密钥，不访问串口、不自动发布或刷写。

结果保留 14 天：`identity.json`、`result.json`、工具/构建日志、原/新 lock、版本约束、sdkconfig、
CMake/project/flash 元数据、ELF 符号表、生成 board 配置和两个开发签名包。
失败时先查看 `build.log`；尚未生成的后续证据不会被假称已通过。

仅验证一个已有的开发包可以运行：

```bash
python .github/workflows/scripts/firmware_ci.py verify-package /path/to/development-package
```

`build` 子命令限定在临时 Actions checkout，避免覆盖本机开发构建。真实云端构建、
artifact 下载复核和 PR required checks 仍需对应 Actions run 的成功证据。生产密钥、
有线迁移、掉电、资源故障和长期实机门禁仍见 [OTA 发布验收](ota-release-readiness.md)。

## 2026-10-07 本地验证边界

工作流通过 actionlint 1.7.7、YAML/Bash 语法检查，Python 通过 AST 解析；隔离 fixture
验证了 board 路径规范化及未处理绝对路径的拒绝。使用本机已有真实 main/Recovery
构建产物进行只读核验，分区/容量、普通 flavor、board/model ELF 符号、模型长度及四个
overlay 实际编译来源检查通过，没有重新构建或修改这些产物。

已存在的开发包 `20261007-010516` 通过新入口和原 `ota_security.py verify-package`。
临时副本上的 flavor 不一致以及 Recovery 区段篡改（即使更新 merged 总哈希）均被拒绝。
这验证了校验逻辑，没有验证新的 Linux 冷构建或新包生成；后者必须由首次 Actions run
确认。本地 Docker Registry 元数据查询遇到 TLS EOF，未下载镜像或安装额外构建运行时。

## 2026-10-07 首轮 Actions 失败与修复

候选 `38d79bb` 的 [Actions 37510590510](https://github.com/rymcu/rodakos/actions/runs/37510590510)
已通过实际 IDF commit、版本及推荐 Xtensa GCC 核验，但冷组件准备在 `espressif/dl_fft`
处失败：原校验只接受 Registry 地址带尾斜杠的形式，而锁文件同时包含两种合法根地址。
该轮尚未完成固件构建或生成开发包。

修复仅允许上述两个精确字符串，组件来源、版本及目录哈希检查保持不变。工作流在构建
前运行离线回归，检查实际 lock 中每个 service 条目及恶意/不规范 URL 负例。修复后的
云端冷构建和开发包仍须以新 Actions run 结果为准。

本地 3 个回归测试通过，包含实际 lock 的全部 27 个 service 组件以及 21 个非法 URL/
来源负例；生产 helper 对已存在组件执行只读目录哈希校验全部通过，lock 字节未变。
两个工作流重新通过 actionlint；此次没有运行本机固件构建或重新下载组件。

## 2026-10-07 第二轮依赖漂移与修复

候选 `f72cb79` 的 [Actions 37512599635](https://github.com/rymcu/rodakos/actions/runs/37512599635)
通过 Registry 输入检查并完成主应用和 Recovery 编译，但在依赖图门禁失败。日志确认
重新求解把 `espressif/usb` 从锁定的 `1.5.0` 升级为 `1.6.0`；这是实际版本漂移，不能
忽略。该轮没有通过最终固件校验，也没有生成经过验证的开发包。

修复加入上述 27 个精确版本约束及编译前门禁，保留所有原有来源、版本、哈希与图比较。
6 个离线回归测试通过：官方 3.0.3 solver 在这组约束下仅保留 USB 1.5.0；版本、哈希、
Registry、本地路径、组件删除、依赖边、直接依赖和 target 的 8 类变化仍被拒绝，原/新
lock 与 diff 在失败时保留。新的云端完整构建仍需后续 Actions 成功证据。

## 2026-10-07 完整构建与工件复核通过

候选 `9a093c57b8f3026c2f9e1c5523e4d600181593b1` 的
[Actions 37514192397](https://github.com/rymcu/rodakos/actions/runs/37514192397)
最终成功，完成普通 flavor 主应用与 Recovery 冷构建，以及两个开发签名包的生成和验证。
实际 IDF commit 和推荐 GCC 与本文固定输入一致；27 个 Registry 组件哈希、完整 30 条
依赖图、Board Manager 生成/链接、四个 overlay、语音模型跨度、分区容量与普通 flavor
门禁通过。下载的原/新 lock 仅 `manifest_hash` 不同，USB 保持 `1.5.0`。

工件 `rodakos-development-firmware-9a093c57b8f3026c2f9e1c5523e4d600181593b1`：
ID `11435973303`，48,823,592 字节；GitHub 报告的归档 SHA-256 为
`63370d0bd560d54a632314fa18891074ba8afcdfba91b90a647b509dd3b515b9`。
下载后，`development-baseline` 与 `development-refresh` 均再次通过本 CI 入口和原
`ota_security.py verify-package` 的签名、flavor、哈希及 merged 区段检查；两个包的
bootloader、分区表、otadata、Recovery 与公钥逐字节一致。

主应用为 7,095,392 字节，SHA-256
`588f2f1d29c9d9a675d0f48d7e6a17c1e1d5568134eb2e7d4d4042ff8020d07a`；
Recovery 为 338,592 字节。同一候选的主机测试见
[Actions 37514192380](https://github.com/rymcu/rodakos/actions/runs/37514192380)。
这些包使用本次 CI 的临时开发信任根；未刷设备，不替代已安装 Recovery 的兼容性、
生产密钥、实际掉电或长期实机验收。#26 的 PR 可见性与 required checks/保护规则仍开放。

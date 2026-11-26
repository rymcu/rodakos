# Rodak AIoT v1 协议全景与架构入门

> **参考规范**：本文基于 [rodak-aiot-contract-v1.md](https://github.com/rymcu/rodakos/blob/main/docs/rodak-aiot-contract-v1.md) 与 [rymcu/rodakos](https://github.com/rymcu/rodakos) 仓库整理，面向首次接入或维护 `rodak-aiot/v1` 协议的固件、服务端及模拟器开发者。

> 固件版本与协议版本独立：本手册对照的 [CMake 源码版本](https://github.com/rymcu/rodakos/blob/f4d834d3df8ea03f0a4e914d0e7558b78b3a7fa5/CMakeLists.txt)为 0.1.6；线级标识仍为 rodak-aiot/v1、rodak-realtime-voice/v1。不要把 PROJECT_VER、主机测试通过或 CI 开发产物当成公开发布或设备已升级的证明。

## 1. 协议定位与权威边界

`rodak-aiot/v1` 是 Rodak 服务端与 RodakOS 嵌入式设备之间的核心 AIoT 控制面与协同协议。在动手写代码之前，新人开发者需要先明确三处“单一事实来源”的边界：

| 维度 | 权威归属 | 说明 |
| :--- | :--- | :--- |
| **服务端行为** | Rodak 服务端 | 当文档描述与服务端实际行为不一致时，以修复 [rodak-aiot-contract-v1.md](https://github.com/rymcu/rodakos/blob/main/docs/rodak-aiot-contract-v1.md) 文档为准。 |
| **固件端实现** | RodakOS（`main/phone_os/`） | 运行于 ESP32-S3（RYMCU BigSmart）平台，由系统服务承载协议状态机。 |
| **机器可读契约** | `contracts/rodak-aiot/v1/` | 包含 OpenAPI、AsyncAPI、JSON Schema 2020-12 及正反例测试夹具，用于跨仓库双向校验。 |

### 协议覆盖范围与明确不包含的内容

* **覆盖范围**：设备身份校验（Identity）、宿主绑定（Owner binding）、凭证管理（Credentials）、统一 MQTT（Unified MQTT）、设备影子（Shadow state）与 OTA 升级协同。
* **不包含实时语音传输层**：实时语音（WebSocket + Opus）仅通过本协议的 `realtimeVoice` 描述符完成交接，其传输细节由独立契约 `Rodak realtime voice v1` 定义。
* **不包含 XiaoZhi 兼容**：Rodak 当前不存在 XiaoZhi 适配器。固件与新设备实现**严禁**读取旧版 XiaoZhi 字段、WebSocket 凭证或旧 NVS 命名空间。

## 2. 按角色推荐的阅读路径

协议文档本身（§0）为不同岗位的开发者划分了重点阅读章节，配合本系列文章可以快速定位所需内容：

| 开发者角色 | 协议原文重点章节 | 对应本系列文章 |
| :--- | :--- | :--- |
| **服务端开发 / 改造** | §2、§4、§5、§7、§8、§9、§12 及 Command semantics | 全系列 4 篇（重点看第 2、3、4 篇） |
| **设备端 / 模拟器开发** | §2–§9、Command semantics 与 Implementation notes | 第 1、2、3 篇（若涉及升级再看第 4 篇） |
| **RodakOS 固件维护者** | 通读全文（含 §13 模块归属与 §14 契约验证） | 全系列 4 篇通读 |

## 3. 系统参与方与核心模块分工

整个 `rodak-aiot/v1` 体系由**服务端（Rodak）**和**设备端（RodakOS）**两大阵营构成，双方通过 HTTP（控制面引导、绑定、鉴权、OTA 票据与包体）和 MQTT（长连接指令、影子、遥测、OTA 通知与进度）协同工作。

### 服务端（Rodak）三大部分

1. **HTTP API（`/api/v1/aiot`）**：提供引导信息、绑定请求与轮询、局域网建信（Trust Setup）、Token 刷新、解绑以及 OTA 检查/票据/清单/包体/结果上报接口。
2. **MQTT Broker**：负责设备长连接认证与消息路由，承载影子同步、指令下发与回执、遥测、PC 状态同步和 OTA 通知。
3. **Desktop Owner UI**：供设备宿主（用户）在桌面端输入 8 位配对码确认绑定或完成局域网建信交互。

### 设备端（RodakOS）核心服务映射

在 RodakOS 固件中，协议职责被清晰地拆分到独立服务中（见协议 §1 与 §13）：

| 服务类 / 模块 | 职责范围 |
| :--- | :--- |
| `DeviceCloudConfigService` (DCC) | 管理设备身份、入网绑定流程、凭证生成与事务性持久化。 |
| `UnifiedMqttService` (UMS) | 维护 MQTT 连接生命周期、Topic 订阅/发布、Shadow 同步、指令分发与 Telemetry 上报。 |
| `OtaUpdateService` (OTA) | 负责 OTA 检查与下载、日志（journal）记录、Recovery 分区交接以及升级结果上报（配合出厂 Recovery 镜像）。 |
| `VoiceWakeService` | 语音唤醒服务，运行开关与参数通过 Shadow 影子状态控制。 |
| `RodakRealtimeVoiceTransport` | 承接绑定/刷新返回的 `realtimeVoice` 描述符，建立实时语音通道。 |

## 4. 关键术语与身份强校验规则

新人最容易在“凭证种类”和“命名差异”上踩坑，以下术语请务必在编码前对齐：

### 4.1 五类核心标识与凭证

| 术语 | 格式与来源 | 核心用途与安全约束 |
| :--- | :--- | :--- |
| **Device key** | 默认小写 Wi-Fi STA MAC（如 `aa:bb:cc:dd:ee:ff`） | 设备的稳定唯一标识，同时用作 MQTT 连接的 `username`。 |
| **Device secret** | 设备端生成的 32 字节随机数，编码为 64 位小写十六进制 | 在绑定或刷新请求中作为 `deviceSecret` / `credentialSecret` 发送；服务端**仅存储哈希**，设备端严禁打印到日志或串口。 |
| **Request ID** | 服务端下发的 32 位小写十六进制字符串 | 标识一次绑定请求，用于轮询状态与 Trust Setup 路径参数。 |
| **Request token** | 32 字节随机数转 base64url（43 字符） | 用于轮询 `/status` 与调用 `/trust` 的 `Bearer` 鉴权；在 LAN Trust Setup 成功后会被一次性替换。 |
| **Access token** | HS256 JWT（头部 `{"alg":"HS256"}`，载荷含 `tv`、`exp` 及 `did`/`dk`/`pk` 子集） | 用作 MQTT 连接密码（`password`）、OTA 与解绑接口的 HTTP `Bearer` 凭证，以及实时语音的 `Bearer` 凭证。 |

### 4.2 身份常量与防踩坑提示

在协议 §3 中，设备身份包含以下固定字段：

* `protocol`: `"rodak-aiot"`
* `protocolVersion`: `1`
* `productKey`: `"rymcu-bigsmart"`
* `application.name`: `"rodakos"`
* `board.type`: `"rymcu_bigsmart"`

> **高频踩坑警告：`rymcu_bigsmart` ≠ `rymcu-bigsmart`**
> Espressif Board Manager 中使用的板型名称是下划线格式的 `rymcu_bigsmart`，而云端 `productKey` 是连字符格式的 `rymcu-bigsmart`。两者绝对不能混用。

设备在调用 `GET /api/v1/aiot/devices/bootstrap` 后，**必须执行强校验**：只要响应中出现 `module != "aiot"`、`protocol != "rodak-aiot"`、`productKey != "rymcu-bigsmart"` 或 `protocolVersion != 1` 中的任意一项，就必须直接拒绝该引导响应，不得继续绑定。

### 4.3 两种安全模式：Legacy mode vs Pin mode

* **Legacy mode**：设备本地未安装服务端信任锚点，允许通过明文 HTTP/MQTT 及 `ws://` 通信，适合早期开发或无证书环境。
* **Pin mode**：通过 USB 或局域网 Trust Setup 安装服务端信任锚点，并绑定固定 TLS 域名 `rodak-<16 hex>.local`，强制全链路加密通信（HTTPS、`mqtts`、`wss://`），并严格限制重定向与公共 CA 回退。

## 5. RodakOS 仓库结构与本地上手环境

如果你需要直接阅读或修改 [rymcu/rodakos](https://github.com/rymcu/rodakos) 固件源码，以下是核心技术栈与目录布局：

* **硬件与框架**：ESP32-S3（RYMCU BigSmart，320x240 触控屏）、**ESP-IDF 6.0.2**、LVGL 9.3 + `esp_lvgl_port` 2.8、Espressif Board Manager + `rodakos_hal_boards`。
* **依赖锁定**：`main/idf_component.yml` 声明直接依赖，`dependencies.lock` 锁定完整组件依赖图。

| 目录 | 作用说明 |
| :--- | :--- |
| `main/` | 主应用（运行于 `ota_0`），含入口 `main.cc`、内置应用 `apps/`、系统服务 `phone_os/`、LVGL 封装 `phone_ui/`、硬件适配 `rodakos_adapters/` 与 `usb_msc_mode.cc`。 |
| `recovery/` | 不可变的出厂 Recovery 应用，负责在 OTA 阶段安全写入 `ota_0` 分区。 |
| `components/` | 本地与托管组件，含 `rodakos_hal_boards/`、`esp_board_manager/`、`esp_peer/`（WebRTC）、`rodak_ota_state/`（OTA 日志与签名格式）、`rodak_appearance/`、`rodakos_fonts/` 等。 |
| `cmake/` 与 `patches/` | 构建期对托管组件（codec、MQTT、WebSocket、LVGL/LodePNG 等）应用的补丁覆盖。 |
| `contracts/` | 跨仓库机器可读契约（如 `contracts/rodak-aiot/v1/`）。 |
| `tools/` | OTA 签名校验、契约检查脚本（`check_aiot_contract.py`）、补丁生成与串口抓取工具。 |
| `tests/` | 宿主单元测试套件（WSL/Linux 下无硬件运行，启用 ASan/UBSan）。 |
| `docs/` | 架构设计、协议契约（含 `rodak-aiot-contract-v1.md`）、测试证据与规划文档。 |

### 快速构建与烧录命令（PowerShell）

项目严格依赖 **ESP-IDF 6.0.2**，切勿混用其他 IDF 版本：

```powershell
# 1. 激活校验过的 ESP-IDF 6.0.2 环境
. .\activate_idf.ps1
# 或显式指定版本：. .\activate_idf.ps1 -Version v6.0.2

# 2. 首次构建或板型变更时执行全量构建脚本
.\build_rodakos.ps1

# 3. 日常仅修改主应用时的增量编译（ota_0）
idf.py build

# 4. 烧录并运行测试（首次迁移 Recovery 分区需追加 -Erase）
.\flash_and_test.ps1 -Port COM3 -NoMonitor
```

> **烧录红线**：**切勿**在仓库根目录直接执行 `idf.py flash` 或 `idf.py app-flash`，否则会直接覆写较小的 factory（Recovery）分区，导致 OTA Recovery 机制损坏。

---

**下一篇推荐**：阅读《设备入网、宿主绑定、鉴权与安全模式实战》，掌握 9 步绑定流程、HTTP 信封错误处理陷阱以及 LAN Trust Setup 的实现细节。

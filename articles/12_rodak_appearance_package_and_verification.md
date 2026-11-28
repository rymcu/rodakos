# 外观资源包协议与签名校验机制（Appearance Customization）

> **参考规范**：本文基于仓库内的 `docs/appearance-customization.md`、`docs/appearance-verification.md` 与 `components/rodak_appearance/` 源码整理，补齐 [rodak-aiot-contract-v1.md](https://github.com/rymcu/rodakos/blob/main/docs/rodak-aiot-contract-v1.md) 中外设外观定制（开机动画、主屏壁纸与全局主题）的协议契约、RAP1 二进制格式及安全校验边界。

> 验收证据按版本解读：仓库保存了[早期 COM3 无线资源采用与回退记录](https://github.com/rymcu/rodakos/blob/38f16b0d2f916106349fd93206edf16854ccae8f/docs/appearance-verification.md)；它不代表当前 0.1.6 或其他设备已复验。宿主用例可证明编解码、验签、策略及 LVGL 生命周期，[实际触摸授权](https://github.com/rymcu/rodakos/blob/38f16b0d2f916106349fd93206edf16854ccae8f/docs/appearance-customization.md#device-workflow-and-publisher-pinning)、无线下载、重启采用、慢 SD 卡时序与屏幕可读性仍须在选定构建上独立验证。

## 1. 整体定位与发布端公钥固定（Publisher Pinning）

RodakOS 的外观定制采用**桌面端预编译 + 设备端验签渲染**的架构：文本、TTF 字体与 PNG/JPEG 素材全部由 Rodak 桌面端编译为像素数据与有界时间线，ESP32-S3 端**不运行任何字体解析器或脚本引擎**，且外观资源完全独立于主固件、出厂 Recovery 与分区表。

### 1.1 物理触摸信任门控（防远程篡改）

外观资源使用 **RSA-2048 / SHA-256** 签名，设备侧采用**公钥固定（Public-Key Pinning）**而非数字 PIN：

1. `keyId` 为发布端公钥 SPKI DER 的 SHA-256 摘要，设备界面展示其前 32 位十六进制字符（每 4 位分组）。
2. 用户在设备 **Themes（主题）** 应用 → **发布端** → **读取/刷新**，核对指纹与 Rodak 桌面端一致后，**必须退出远程控制并用物理触摸点击“指纹一致，确认信任”**（按下与点击事件都必须源自物理触摸，远程指针与串口事件均无法授予信任）。
3. 信任记录（完整 `keyId`、公钥与 Origin）持久化于 NVS `appearance` 命名空间。

> **信任变更边界**：更换发布密钥、首次从 HTTP 升级到 HTTPS、或变更 HTTPS 端口都会改变 Origin，必须重新进行物理确认；普通 IP 变化保持逻辑主机名不变；设备解绑会清除发布端信任；局域网服务器发现（mDNS）**绝不**自动替用户更新 `appearance` 信任。

## 2. HTTP 端点、断点续传与 Shadow 状态协同

### 2.1 四个固定 HTTP 端点

所有请求均需携带设备 `Bearer accessToken`，固件禁用自动重定向（Pin 模式下强制走已固定证书的 HTTPS，不回退公共 CA 或明文）：

| 端点路径 | 作用与约束 |
| :--- | :--- |
| `GET /api/v1/aiot/appearance/publisher` | 拉取发布端公钥与 `keyId`，供首次核对指纹。 |
| `POST /api/v1/aiot/appearance/download-ticket` | 申请下载票据（绑定设备、deployment 与 `tokenVersion`，**10 分钟**过期，拒绝已被取代的部署）。 |
| `GET /api/v1/aiot/appearance/manifests/:deploymentId?ticketId=…` | 获取已签名的部署清单（Manifest）。 |
| `GET /api/v1/aiot/appearance/artifacts/:deploymentId?ticketId=…` | 下载 RAP1 二进制包体，支持单段 `Range`。 |

* **断点续传与重试**：首次发起完整 `GET`，存在合法部分文件时携带 `Range: bytes=N-` 续传；收到 `206` 时必须精确核对起始/结束/总长与剩余长度，收到 `200` 则清空文件重下。瞬态失败按 **5 / 15 / 60 秒**最多重试 **3 次**；`401` 仅触发一次鉴权刷新，再次被拒即终态失败。

### 2.2 Shadow 协同与 10 种状态机

* **Desired Shadow**：携带 `appearance.{deploymentId, releaseId, revision, mode, keyId}`（总长 ≤ 1024 字节，**严格限定这 5 个协议字段**，MQTT 解析时投影进固定 384 字节缓冲）。部分更新灯光等字段时必须保留 `appearance` 不被覆盖。
* **Reported Shadow**：上报能力集、`activeRevision` / `pendingRevision`、状态/进度、发布端信息、失败原因、`themeSource`（`remote` / `local`）与加载耗时。
* **状态枚举**：`idle`、`awaiting_trust`、`waiting_device`、`deferred_busy`、`downloading`、`pending_reboot`、`applied`、`failed`、`fallback`、`superseded`。只有 `applied` 代表新版本已实际生效。
* **下载让路条件（`deferred_busy`）**：启动试用期、OTA 进行中、语音/录音活跃、摄像头/屏幕推流活跃，或内部最大连续空闲块 **< 8192 字节**时，自动推迟下载并由 MQTT 主循环定期重新调度。

## 3. RAP1 二进制包格式与签名清单

### 3.1 RAP1 文件结构（16 字节头 + UTF-8 JSON + 连续像素区）

ASCII 魔数后的所有整型字段均为**小端序（Little-Endian）**，资源偏移量均相对于像素区起始位置：

| 字节偏移 | 长度（Byte） | 含义与取值 |
| ---: | ---: | :--- |
| `0` | `4` | ASCII 魔数 `RAP1` |
| `4` | `2` | 版本号 `1` |
| `6` | `2` | 屏幕宽度 `320` |
| `8` | `2` | 屏幕高度 `240` |
| `10` | `2` | 资源数量（Resource count） |
| `12` | `4` | 元数据 JSON 字节数（≤ 16 KiB） |

* **JSON Schema（`rodak-appearance-v1`）**：定义动画类型/模板/配色/时长（1000–5000 ms，默认 2500 ms）、最多 **32 个**动画单元（`resourceId/x/y/startMs/durationMs`，左上角坐标，SmoothStep 入场，33 ms LVGL 定时器按真实耗时推进，结束后 220 ms 淡出）、可选壁纸（RGB565，153,600 B / 150 KiB）、主题色与资源索引。
* **三种像素格式**：
  * `a4`：每行 `ceil(width/2)` 字节，高半字节为左像素，解码时每 nibble `× 17` 展开为 A8（仅用 ≤160 B 内部行缓冲）。
  * `rgb565`：小端 2 字节/像素，直接读入目标 PSRAM 缓冲。
  * `rgb565a8`：完整 RGB565 色彩平面后接完整 1 字节 Alpha 平面。

### 3.2 签名清单（Signed Manifest）验签红线

Manifest 信封格式为 `{ signedPayload, signature, keyId }`，RSA-2048 / SHA-256 针对 **Base64 解码后的原始 UTF-8 字节流**签名。**严禁先 JSON 解析再重新序列化后验签**。载荷绑定部署 ID、设备 ID/Key、产品、版本号、模式、包大小与 SHA-256、分辨率及 `keyId`；从 SD 卡启动加载时会再次复核摘要与元数据边界。

## 4. 内存预算、SD 双槽落盘与启动试用回滚

| 预算 / 约束项 | 上限值 |
| :--- | :--- |
| 包体大小 / 包内 JSON | **≤ 512 KiB** / **≤ 16 KiB** |
| 解码后像素总数据（动画 + 壁纸） | **≤ 384 KiB**（满屏 RGB565A8 Logo + RGB565 壁纸恰好 384,000 B） |
| 私有有界 JSON 分配器 | **≤ 96 KiB**（含 16 B 分配头，不修改全局 cJSON 钩子） |
| 启动加载验收预算 | `BeginBootLoad()` 起 **1500 ms**（超时或损坏则本次回退内置 EDIX） |
| 下载前 SD 剩余空间 | 剩余包字节数 + **8192 B** |

* **SD 双槽与续传元数据**：位于 `/rodakos/appearance/`，维护 `a.rap` / `b.rap`、对应 `.manifest`、临时 `.rap.part` 及 `a.part.meta` / `b.part.meta`（续传前先校验 Origin 与已签名 Manifest，防止跨部署续传）。只有非活跃槽校验、落盘并重命名完成后，才更新 `appearance/state` 的 pending 状态。
* **启动试用与回滚**：下载完成进入 `pending_reboot`（不强制重启当前应用）；下次开机先持久化为 boot trial，待系统启动与开机动画播完后在主循环确认转 `applied`。未确认的试用在下一次开机自动回退至上一有效包或内置 EDIX（默认暗色 `#79cbff`）。
* **本地主题覆盖与恢复内置**：在 Themes 应用本地切换预设会立即生效并保存本地覆盖（保留自定义动画/壁纸），直到下一次在线发布更高版本才清除覆盖；“恢复内置”是一次不下载包体的已签名 `builtin` 部署，会清除本地覆盖。

## 5. 对应源码与主机测试套件

| 模块 | 核心源码 | 主机测试套件 |
| :--- | :--- | :--- |
| **下载、Pin 与状态机** | `main/phone_os/appearance_service.*`、`appearance_status.h` | `tests/themes_ui/`、`tests/appearance/` |
| **RAP1 编解码、验签与策略** | `components/rodak_appearance/`（`rodak_appearance.*`、`_crypto.*`、`_json.c`、`_policy.*`） | `tests/appearance/`（26 项编解码/RSA/策略/分配器用例） |
| **开机动画与主屏壁纸渲染** | `main/phone_ui/boot_animation.h`、`phone_ui.h` | `tests/home_ui/`（43 项真 LVGL 渲染与生命周期用例） |

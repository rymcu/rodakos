# 无 USB 局域网首次接入设计

状态：提案，未实现。本文扩展 [可信服务器发现](trusted-server-discovery.md)，先读那一页；
设备认领与配对码规则见 Rodak 仓库 `docs/device-binding-authentication.md`。

## 目标

用户第一次拿到设备，只在设备屏幕上连 Wi-Fi、在 Rodak 桌面端输一次配对码，就能把设备
接入同一局域网内的 Rodak 服务器，并获得与 USB 下发等价的服务器信任。不需要数据线、手机
App、蓝牙或电脑热点。

非目标：

- 跨网段、跨 VLAN 或经由互联网的接入；
- 替代 USB 下发。USB 保留，作为开发和高级用户路径；
- 已绑定设备换服务器。已绑定设备仍只认 USB 或已固定的证书；
- 实现完整的 Matter。Matter 面向第三方平台控制设备，不负责绑定 Rodak 自己的服务器；本方案
  只借用它“已入网设备 + mDNS + 短码 PAKE”的接入方式。

## 现状与缺口

| 能力 | 现状 |
| --- | --- |
| 设备在屏幕上连 Wi-Fi | 已有（设置 → Wi-Fi） |
| 设备注册 `rodakos-<MAC 后三字节>.local` | 已有（随 Wi-Fi 启动） |
| Rodak 广播 `_rodak._tcp`（PTR/SRV/TXT `v=1`、`id=<server_id>`/A） | 已有，热点与以太网均实测 |
| 已绑定设备换网后经 mDNS 找回服务器 | 已有，2026-10-18 实机通过 |
| 屏幕配对码 + 桌面端确认绑定 | 已有，但只证明“用户同意绑定”，不建立服务器信任 |
| 服务器证书（`server_trust`）安装 | **只能经 USB** |

缺口只有一个：设备首次在网络上见到服务器时，无法判断对方是否是用户想接入的那台 Rodak。
mDNS 记录、TXT 中的 `id`、TLS 自签证书都可以被同网段的攻击者伪造。现有配对码在 TLS 建立
之后才使用，若设备事先不认证书，中间人可以把设备的配对请求转发给真服务器，自己留在中间。

## 方案概述

把配对码从“绑定确认码”升级为 PAKE（口令认证密钥交换）的共享口令。双方用 PAKE 证明
“对方知道同一个屏幕上显示的码”，并在 PAKE 的协商记录里绑定服务器证书指纹。PAKE 成功后，
设备得到与 USB 下发完全相同的 `server_trust` 对象，后续沿用现有的固定证书链路。

```text
设备（屏幕）                                Rodak 桌面端
─────────────                              ──────────────
1. 用户选 Wi-Fi，入网
2. 查询 _rodak._tcp → 列出服务器
   用户确认一台（或只有一台时自动选中）
3. 生成 8 位随机码 + 会话 ID，屏幕显示
4. ── HTTPS（暂不验证证书）POST onboarding/start ──▶
                                            5. 弹出“RodakOS-c3b430 请求接入”
                                               用户输入屏幕上的 8 位码
6. ◀────────────── SRP6a 交换 ──────────────▶
   （协商记录包含服务器证书 SPKI 指纹、设备 ID、会话 ID）
7. 双方确认消息校验通过
   设备从加密通道收到 server_trust
   （server_id、tls_name、ca_pem）
8. 设备以 USB 下发相同的事务写入 NVS（pending → active）
9. 之后走现有的固定证书 bootstrap / token / MQTTS
```

## 关键决策

### 由设备生成口令

码由设备生成并显示，用户在 Rodak 输入。理由：

- 设备有屏幕，Rodak 是用户正在操作的那台电脑。用户“看着设备，输到电脑”，能同时确认设备
  和服务器两端；
- 攻击者即使控制网络，也看不到设备屏幕；
- 与现有绑定流程方向一致，屏幕交互改动小。

### 协议选择

| 候选 | 设备端 | Rodak 端 | 结论 |
| --- | --- | --- | --- |
| SPAKE2+（RFC 9383，Matter 使用） | TF-PSA-Crypto 1.1 只有密钥类型定义，没有可用实现 | 需要自研或引入 Matter 库 | 暂不选 |
| EC J-PAKE（P-256，PSA `PSA_ALG_JPAKE`） | ESP-IDF 6.0.2 自带，`PSA_WANT_ALG_JPAKE=1` | Node 无现成实现，需基于 `@noble/curves` 自研，三轮交互 | 备选 |
| SRP6a（RFC 5054，3072 位，SHA-512） | ESP-IDF `protocomm` 自带 `esp_srp`，ESP 统一配网 Security 2 正在用 | 有维护中的 JS 实现，也可基于 `@noble/hashes` 自写，代码量小 | **首选** |

选 SRP6a：设备端是 Espressif 已在配网场景使用的成熟代码，两轮交互，Rodak 端实现成本最低。

`esp_srp` 只实现 SRP 的服务器一侧（`esp_srp_srv_pubkey`、`esp_srp_get_session_key`、
`esp_srp_exchange_proofs`），因此**设备担任 SRP 服务器，Rodak 担任客户端**。这与 ESP
统一配网 Security 2 的角色一致：设备持有口令并生成 salt/verifier，手机或电脑作为客户端。
口令只在本次接入期间有效，verifier 只存在内存中，不落 NVS。

`esp_srp` 随 `CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2` 编译，头文件
`include/crypto/srp6a/esp_srp.h` 由 `protocomm` 导出。主程序 `REQUIRES protocomm` 并开启该
选项即可复用，不需要复制源码。

> 待验证：3072 位模幂在 ESP32-S3 上的耗时需实测，预计 1–3 秒，可接受，但必须在后台任务
> 中执行，不能阻塞 LVGL。

### 证书绑定

PAKE 本身只证明“双方知道同一个码”，必须把服务器证书一起绑定进去，否则中间人可以用
自己的证书完成 TLS，再分别与两端做 PAKE。做法：

- 设备第一次连接 Rodak 的 HTTPS 时不验证证书（只此一次），但记录服务器出示证书的 SPKI
  SHA-256，即 `server_id` 的候选值；
- SRP 的 username 固定为 `rodak-onboarding/v1`，salt 由设备随机生成；
- 双方的确认消息（M1/M2）之后，再交换一段 HMAC，HMAC 密钥由 SRP 会话密钥派生，输入为：
  `"rodak-onboarding/v1" ‖ session_id ‖ device_id ‖ 设备看到的 SPKI 指纹 ‖ Rodak 自己的 SPKI 指纹`；
- 如果设备看到的指纹与 Rodak 自己的不同（存在 TLS 中间人），HMAC 不一致，双方都中止；
- 校验通过后，Rodak 下发 `server_trust`，设备再检查 `server_id` 与记录的指纹一致、
  `tls_name` 符合 `rodak-<id 前 16 位>.local` 规则（复用 `ValidateServerTrust`）。

### 抗暴力破解

8 位数字约 26.6 比特，在线猜测必须受限：

- 一个码只允许一次 PAKE 尝试。失败后设备丢弃该码并重新生成；连续 3 次失败，设备锁定
  接入入口 60 秒；
- 码 5 分钟过期，期间屏幕显示倒计时；
- Rodak 端同一会话只接受一次口令输入；同一来源每分钟最多 5 个接入会话（参照现有
  binding/request 的限流）；
- 离线攻击：SRP 只交换公钥和证明，被动窃听者拿到记录后无法离线穷举口令；
- 主动攻击：SRP-6a 的乘数 `k` 使冒充任一方的攻击者每次会话只能验证一个猜测，
  不能从对方的证明中离线穷举。中间人原样转发 SRP 消息时，双方能算出同一会话密钥，
  但证书绑定 HMAC 会因两端看到的指纹不同而失败。

### 入口限制

- 只有“未固定服务器”（`device_cloud/server_pinned` 未置位）且未绑定的设备开放接入入口；
- 已固定服务器的设备在设置里看不到这个入口，要换服务器必须先由服务器确认解绑，或走 USB；
- 入口由用户在屏幕上主动进入，开机不会自动发起；
- Rodak 端接入弹窗需要用户在当前窗口操作，不能被网页或插件触发。

## 交互细节

### 设备端

1. 首次开机或设置 → 设备云 → “连接到电脑上的 Rodak”。
2. 若 Wi-Fi 未连接，先进入现有 Wi-Fi 页面。
3. 查询 `_rodak._tcp`，超时 3 秒，可手动刷新：
   - 只有 1 台：显示“找到 t14p 上的 Rodak”，按钮“连接”；
   - 多台：列表显示主机名和 `id` 前 8 位；
   - 0 台：提示检查是否同一网络、是否开了访客网络或 AP 隔离，并提供“手动输入地址”
     （`https://<IP>:9443`，后续流程相同）。
4. 显示 8 位码（4+4 分组，大字号）、倒计时、“取消”。
5. 成功：显示“已接入 t14p 上的 Rodak”，随后进入现有绑定完成状态。失败：说明原因，
   提供“重新生成配对码”。

主机名展示需要 Rodak 在 TXT 中新增 `name=<计算机名>`。它只用于展示，不参与信任。

### Rodak 端

1. 收到 `onboarding/start` 后，在托盘和主窗口弹出通知：“RodakOS-c3b430 请求接入”，
   显示设备 ID（MAC）。
2. 用户输入 8 位码并确认，可选择家庭空间（复用现有绑定表单）。
3. 结果实时显示；失败时说明“配对码不正确，请在设备上重新生成”。
4. 接入成功即视为用户已确认绑定，不再要求第二次输入现有的绑定配对码。

## 网络接口

均在 Rodak 的 HTTPS 端口（默认 9443）。除 start 外，每个请求都带上一步返回的
`session_id`；会话 60 秒无进展即失效。

| 方法与路径 | 方向 | 内容 |
| --- | --- | --- |
| `POST /api/v1/aiot/devices/onboarding/start` | 设备 → Rodak | `device_id`、`device_name`、`firmware`、SRP salt、设备（SRP 服务器）公钥 `B`；返回 `session_id` |
| `GET /api/v1/aiot/devices/onboarding/:session_id` | 设备轮询 | 用户在 Rodak 输码前返回 `waiting`；输码后返回 Rodak（SRP 客户端）公钥 `A`、证明 `M1` 和 Rodak 的证书绑定 HMAC |
| `POST /api/v1/aiot/devices/onboarding/:session_id/confirm` | 设备 → Rodak | 设备校验 `M1` 通过后发送 `M2` 和设备的证书绑定 HMAC；Rodak 校验通过后返回加密的 `server_trust` |
| `POST /api/v1/aiot/devices/onboarding/:session_id/cancel` | 设备 → Rodak | 用户取消或码过期 |

因为设备是 SRP 服务器，用户在 Rodak 输码后才能计算 `A` 与 `M1`。设备只需轮询，不需要
开放监听端口；轮询间隔 1 秒，与现有 `binding/requests/:id/status` 一致。

`server_trust` 以 SRP 会话密钥派生的 AES-GCM 密钥加密，即使 TLS 未验证也不泄露。
随后设备立即用新固定的证书发起现有的 `binding/request`，Rodak 识别同一 `session_id`
后直接确认，不再生成新码。

## 设备端改动

- 新增 `main/phone_os/lan_onboarding_service.{h,cc}`：mDNS 浏览、SRP 服务器（`esp_srp`）、证书指纹记录、
  事务提交；在后台任务中运行，PSRAM 栈。
- 复用 `DiscoverServerTrustRoutes` 的 mDNS 实例，新增“列出所有 `_rodak._tcp`”查询
  （现有函数只接受与已固定 `id` 匹配的记录）。
- 首次 HTTPS 连接用专门的“仅记录指纹”传输配置：不加载 CA bundle，在 mbedTLS 验证回调中
  取 SPKI 后放行。只允许接入服务使用，不对其他模块开放。
- 信任写入复用 `SaveSerialProvisioning(..., &server_trust)` 的同一事务，不另开写入路径，
  保持 pending → active 和失败回滚语义一致。
- 设置 → 设备云新增入口和三个页面：发现列表、配对码、结果。
- 主机测试：SRP 往返（与 Rodak 端测试向量互通）、指纹不一致中止、码过期、单码单次、
  锁定计时、入口在已固定服务器时关闭。

## Rodak 端改动

- `src/main/server/lan-onboarding-service.ts`：会话表、SRP 客户端、限流、证书绑定 HMAC、
  `server_trust` 加密下发，与 `device-binding-authorization.ts` 衔接。
- `http-api.ts`：上述路由；接入会话只存内存，不落库，重启即失效。
- `server-discovery.ts`：TXT 增加 `name=`，长度上限 63 字节。
- IPC：`server:onboarding:pending` 事件与 `server:onboarding:confirm`；渲染进程只提交
  用户输入的码，不接触 SRP 状态。
- 渲染进程：通知与输码对话框，复用现有配对确认表单的样式。
- 集成测试：与设备端相同的 SRP 测试向量、中间人场景（两端指纹不同）、限流、超时、
  会话重放。

## 风险与边界

| 风险 | 处理 |
| --- | --- |
| 同网段攻击者在用户操作时抢先接入 | 需要知道屏幕上的码；猜测每码一次，3 次后锁定 |
| 攻击者伪装成 Rodak 让设备接入 | 攻击者不知道码，PAKE 失败，设备不保存任何东西 |
| 用户在 Rodak 上误输入别的设备的码 | 弹窗显示设备 ID 和名称；码与会话一一对应，输错只会失败 |
| 访客网络、AP 隔离、组播被过滤 | 手动输入地址，流程不变 |
| `esp_srp` 3072 位运算慢 | 后台任务执行并显示进度；实测超过 5 秒再考虑 EC J-PAKE |
| 内部 RAM 紧张（MQTT 连上后约 22 KB） | 接入发生在绑定之前，此时 MQTT、语音、投屏均未运行；SRP 大数运算使用 PSRAM |

本方案不改变：Secure Boot / Flash Encryption 策略、证书有效期校验（仍未启用）、
外观发布者信任、已绑定设备的任何路径。

## 分阶段

1. **协议与测试向量**：确定消息格式和 HMAC 输入，两端各自实现 SRP 并用同一组测试向量
   互通。
2. **Rodak 端服务与桌面 UI**：可用脚本模拟设备完成全流程。
3. **设备端服务与屏幕 UI**：先在主机测试中跑通，再上实机。
4. **实机验收**：全新 NVS 设备；热点和路由器两种网络；同网段放一个伪造 `_rodak._tcp`
   的响应者，确认设备拒绝；码输错、过期、取消；接入后换 Wi-Fi，确认仍能经现有发现找回。
5. **文档**：更新本页状态、`trusted-server-discovery.md`、`serial-provisioning.md`
   和 Rodak 的 `device-binding-authentication.md`。

## 待决问题

1. 码长度：8 位数字（与现有一致）还是 6 位？在单码单次、3 次锁定的前提下 6 位也足够，
   输入更快。
2. 接入后是否仍要求在 Rodak 选择家庭空间？建议可选，默认放入“未分配”。
3. 是否需要“Rodak 主动发现设备”？即设备注册 `_rodakos-setup._tcp`，Rodak 列出附近
   未接入设备。这会让用户从电脑端发起，但需要设备开放一个监听端口，建议放到第二版。

# 无 USB 局域网首次接入设计

状态：设计已确认，按分阶段实施；第 1 步（测试向量）与第 2 步（Rodak 端）已完成，第 3 步（设备端）未开始。本文扩展 [可信服务器发现](trusted-server-discovery.md)，先读那一页；
设备认领规则见 Rodak 仓库 `docs/device-binding-authentication.md`。

## 目标

用户第一次拿到设备，只在设备屏幕上连 Wi-Fi、在 Rodak 桌面端输一次 6 位配对码，就能把设备
接入同一局域网内的 Rodak 服务器，并获得与 USB 下发等价的服务器信任。不需要数据线、手机
App、蓝牙或电脑热点。

方案不新增独立的接入流程，而是在现有设备绑定上增加一个“信任建立模式”：请求、轮询、
桌面端输码和确认都沿用现有代码，只改变配对码的来源，并在已有消息中增加几个字段。

非目标：

- 跨网段、跨 VLAN 或经由互联网的接入；
- 替代 USB 下发。USB 保留，作为开发和高级用户路径；
- 已固定服务器的设备换服务器。它们仍只认 USB 或已固定的证书；
- 实现完整的 Matter。本方案只借用它“已入网设备 + mDNS + 短码 PAKE”的接入方式。

## 现有绑定为什么不能直接用

现有绑定（`binding/request` → 屏幕显示码 → 桌面端 `confirmDevicePairing` → 设备轮询
`binding/requests/:id/status` 取 token）证明的是“这台设备归这个用户”。它假设设备已经能
认出服务器，而这一步目前只有 USB 下发 `server_trust` 能做到。

| | 现有绑定 | 本方案补上的部分 |
| --- | --- | --- |
| 证明 | 设备归这个用户 | 服务器确实是用户自己的 Rodak |
| 配对码来源 | Rodak 生成，经网络发给设备显示 | **设备生成，不经过网络** |
| 前提 | 信道已可信（USB 装过证书），否则为明文 HTTP | 信道不可信 |
| 产出 | 设备身份、token、MQTT 凭据、家庭空间 | 设备固定服务器证书 `server_trust` |

不插线时，用户只能在设备上手动填 `http://` 地址。此时配对码、token 和 MQTT 凭据都是明文；
设备没有固定证书，分辨不出冒充者；已实机验证的“换 Wi-Fi 后经 mDNS 找回服务器”也不生效，
因为 `RefreshWithDiscovery` 在没有 `server_trust` 时直接返回（`device_cloud_config.cc`）。

现有配对码不能直接当口令，因为它由 Rodak 生成、经网络传给设备。中间人可以原样转发设备的
申请和 Rodak 返回的码：用户输入的码是正确的，绑定成功，但 token、MQTT 凭据和设备固定的
证书都经过了中间人。配对码要起认证作用，必须只经用户的眼睛从一块屏幕带到另一块屏幕，
不经过网络。

## 方案概述

```text
设备（屏幕）                                    Rodak 桌面端
─────────────                                  ──────────────
1. 用户选 Wi-Fi，入网
2. 查询 _rodak._tcp → 列出服务器，用户选一台
3. 本地生成 6 位码，屏幕显示（码不发出）
4. HTTPS（仅记录证书指纹）POST binding/request
   附带 trustSetup：SRP salt、B、看到的 SPKI 指纹
   （不带 deviceSecret）─────────────────────────▶
                                                5. 现有“待确认设备”列表出现申请
                                                   用户输入设备屏幕上的码
                                                   Rodak 以码为 SRP 口令计算 A、M1
6. 轮询 binding/requests/:id/status ◀───────────── 返回 trustSetup：A、M1、Rodak 的绑定 HMAC
   设备校验 M1 和 HMAC
7. POST binding/requests/:id/trust ──────────────▶ 发送 M2、设备的绑定 HMAC、
                                                   加密的 deviceSecret
                                                   Rodak 校验后完成 completeManualPairing，
                                                   返回加密的 server_trust 和新 requestToken
8. 设备按 USB 下发相同的事务写入 server_trust
9. 改用固定证书和新 requestToken 轮询 status，取 token（现有逻辑）
```

与现有流程相比，只有第 3、4、6、7、8 步有变化，其余代码不动。

## 两种模式

| 设备状态 | 模式 | 配对码 | 变化 |
| --- | --- | --- | --- |
| 已用 USB 固定 `server_trust` | 现有模式 | Rodak 生成 | 无 |
| 未固定（`device_cloud/server_pinned` 未置位） | 信任建立模式 | 设备生成 | 本文描述的部分 |

Rodak 只根据请求中是否带 `trustSetup` 区分两种模式。旧固件不带该字段，行为不变。

## 协议

### 协议选择

| 候选 | 设备端 | Rodak 端 | 结论 |
| --- | --- | --- | --- |
| SPAKE2+（RFC 9383，Matter 使用） | TF-PSA-Crypto 1.1 只有密钥类型定义，没有实现 | 需自研或引入 Matter 库 | 不选 |
| EC J-PAKE（P-256，`PSA_ALG_JPAKE`） | ESP-IDF 6.0.2 自带 | Node 无现成实现，三轮交互 | 备选 |
| SRP6a（RFC 5054，3072 位，SHA-512） | ESP-IDF `protocomm` 自带 `esp_srp`，ESP 统一配网 Security 2 正在用 | 基于 `@noble/hashes` 与 BigInt 实现，代码量小 | **首选** |

`esp_srp` 只实现 SRP 的服务器一侧，因此**设备担任 SRP 服务器，Rodak 担任客户端**，与 ESP
统一配网 Security 2 的角色一致。设备持有口令并生成 salt 和 verifier；verifier 只在内存中，
不写入 NVS。`esp_srp` 随 `CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2` 编译，头文件
由 `protocomm` 导出，主程序 `REQUIRES protocomm` 即可复用。

SRP 参数：username 固定为 `rodak-trust-setup/v1`；salt 16 字节，由设备随机生成；口令为
6 位码去掉分隔符后的 ASCII。Rodak 端实现必须与 `esp_srp` 的 `k`、`u`、`M1`、`M2` 计算
（含 PAD 规则）逐字节一致，以同一组测试向量验证。

> 待验证：3072 位模幂在 ESP32-S3 上的耗时需实测，预计 1–3 秒，必须在后台任务中执行，
> 不能阻塞 LVGL。

`esp_srp` 有两处限制，设备端必须绕开（第 1 步的主机测试已覆盖或记录）：

- `esp_srp_srv_pubkey` / `esp_srp_gen_salt_verifier` 自行生成 salt 时，若首字节为 0，
  `esp_mpi_to_bin` 会把它去掉：返回的 salt 只有 15 字节，verifier 也按 15 字节计算，
  调用方却按 16 字节读取。设备应自行生成 16 字节 salt、按参考公式计算 verifier，再用
  `esp_srp_set_salt_verifier` + `esp_srp_srv_pubkey_from_salt_verifier`（与 Security 2 相同）；
- `esp_srp_get_session_key` 在 `A mod N == 0` 时的错误路径跳过了哈希对象的初始化，随后
  对它调用 `psa_hash_abort`，属于未定义行为。设备在调用前须先检查 `A` 为 384 字节且
  `A mod N != 0`，不依赖该分支。

### 证书绑定

SRP 只证明双方知道同一个码。中间人如果原样转发 SRP 消息，双方仍能算出同一会话密钥，
所以必须把服务器证书一并绑定：

- 信任建立模式下，设备第一次连接 Rodak 的 HTTPS 时不验证证书，只记录服务器出示证书的
  SubjectPublicKeyInfo SHA-256，记为 `seen_spki`，并在请求中带上；
- 双方从 SRP 会话密钥 `K`（64 字节）派生
  `bind_key = HKDF-SHA256(IKM = K, salt = 空, info = "rodak-trust-setup/v1 bind", L = 32)`；
- 绑定 HMAC-SHA256 的输入为 `LP("rodak-trust-setup/v1") ‖ LP(requestId) ‖ LP(deviceKey) ‖
  LP(seen_spki) ‖ LP(server_spki) ‖ LP(角色)`，`LP` 为 2 字节大端长度加原始字节，字符串按
  UTF-8；角色分别为 `"server"` 和 `"device"`，防止反射；
- Rodak 用自己真实的 `server_spki` 计算；设备用 `seen_spki` 同时填两个位置计算并比对。
  只要设备看到的证书不是 Rodak 自己的，HMAC 就不一致，双方中止；
- Rodak 最后下发的 `server_trust` 用下文的 `srv_key` 加密。设备解密后再检查
  `server_id == seen_spki`，以及 `tls_name` 符合 `rodak-<id 前 16 位>.local` 规则
  （复用 `ValidateServerTrust`）。

### 凭据不经未验证信道

第 4–7 步的 TLS 没有验证证书，中间人能看到其中的一切。因此：

- `binding/request` 在信任建立模式下**不带 `deviceSecret`**。现有模式的请求会携带它，
  它是设备的长期凭据；
- `deviceSecret` 改在 `/trust` 请求中用设备方向密钥加密发送，Rodak 解密后计算
  `__pairedSecretHash`，再交给 `completeManualPairing`；
- 第 4 步返回的 `requestToken` 也会经过未验证信道。`/trust` 成功后 Rodak 作废这个令牌，
  在加密的响应中下发新令牌；设备之后用固定证书和新令牌轮询 status 取 token。中间人即使
  拿到旧令牌，也领不到 AIoT token；
- 两个方向使用不同的密钥：
  `dev_key = HKDF(K, "rodak-trust-setup/v1 device")`，`srv_key = HKDF(K, "rodak-trust-setup/v1 server")`，
  均为 AES-256-GCM：nonce 12 字节随机，附加数据为 UTF-8 的 `requestId`，16 字节标签追加在
  密文末尾；明文为紧凑 JSON。线上格式为 `{ "nonce": "<base64>", "ciphertext": "<base64>" }`。

以上派生、绑定与加密规则的测试向量见 `tests/lan_trust_setup/trust-setup-protocol-vectors.json`，
Rodak（Node crypto）与本仓库主机测试（mbedTLS PSA）各自独立重算，结果一致。

中间人能做的只剩下：拿旧令牌轮询到公开的 SRP 参数（不含口令信息），或伪造 `abort`
让本次接入失败。后者只是拒绝服务，用户重新生成码即可。

### 抗暴力破解

6 位数字约 19.9 比特。在线猜测每次会话只能试一个码，按下面的限制，攻击者平均需要
数十万次会话、每次都要用户在桌面端配合输码，实际不可行：

- 每个码只允许一次 SRP 尝试。Rodak 对同一申请只接受一次输码；码是否正确由设备校验 `M1`
  得出，失败时设备通过 `/trust` 发送 `abort`，Rodak 将申请标记为 `revoked`，设备丢弃该码
  并重新生成。即便 `abort` 被中间人拦下，该申请也不会再接受第二次输码，到期自动失效；
- 设备连续 3 个码失败后锁定入口 60 秒；
- 码与申请均 5 分钟过期（现有申请是 10 分钟，信任建立模式缩短）；
- 被动窃听者拿到完整记录也无法离线穷举口令；冒充任一方的主动攻击者每次会话只能验证
  一个猜测；
- 现有限流不变：单个来源每分钟 20 个申请、单设备最多 3 个待处理申请。

## 对现有接口的改动

### `POST /api/v1/aiot/devices/binding/request`

请求新增可选对象（不带时行为与现在完全一致）：

```json
{
  "trustSetup": {
    "version": 1,
    "srpSalt": "<base64>",
    "srpB": "<base64>",
    "seenSpki": "<64 位小写十六进制>"
  }
}
```

带 `trustSetup` 时，Rodak：

- 只在 HTTPS 监听器上接受；
- 要求请求中**没有** `deviceSecret` / `credentialSecret`，有则拒绝，避免固件误把长期凭据
  发进未验证信道；
- 不生成配对码，响应中不返回 `pairingCode`，改为 `trustSetup: { "required": true }`；
- 申请记录保存 `srpSalt`、`srpB`、`seenSpki`，与现有 payload 一样以 AES-GCM 加密存储；
- `remainingAttempts` 设为 1，有效期设为 5 分钟。

### 桌面端确认

`server:confirmDevicePairing` 的入参不变，仍是 `{ requestId, pairingCode }`。主进程根据
申请类型分支：

- 现有模式：照旧校验码的摘要；
- 信任建立模式：以输入的码为口令执行 SRP 客户端计算，得到 `A`、`M1`、`K`，再计算
  Rodak 的绑定 HMAC；这三项写入申请记录，状态转为 `awaiting-device`，此时**不创建设备
  身份**。码是否正确要等设备验证 `M1` 后才知道。

桌面端界面上，该申请显示为“等待设备确认…”，几秒后变为成功或“配对码不正确”。

### `GET /api/v1/aiot/devices/binding/requests/:requestId/status`

状态枚举新增 `awaiting-device`。首次接入申请在 `pending` 时 `trustSetup` 为 `true`（只作标记，
不含挑战）；处于 `awaiting-device` 时，响应附带：

```json
{
  "status": "awaiting-device",
  "trustSetup": { "srpA": "<base64>", "srpM1": "<base64>", "serverBind": "<base64>" }
}
```

现有固件不会遇到这个状态，因为它不发 `trustSetup`。

### 新增 `POST /api/v1/aiot/devices/binding/requests/:requestId/trust`

这是唯一新增的路由，用请求令牌认证，与 status 一致：

- 请求：`{ "srpM2": "<base64>", "deviceBind": "<base64>", "credentials": { "nonce": "...", "ciphertext": "..." } }`，
  `credentials` 是用 `dev_key` 加密的 `{ "deviceSecret": "..." }`。
- Rodak 校验 `M2` 和设备绑定 HMAC，并解密 `credentials`：
  - 通过：用 `deviceSecret` 补上 `__pairedSecretHash`，调用现有的 `completeManualPairing`
    和 `markConfirmed`；作废旧 `requestToken`；响应为
    `{ "status": "confirmed", "sealed": { "nonce", "ciphertext" } }`，`sealed` 用 `srv_key` 解密后为
    `{ "serverTrust": { "version": 1, "server_id", "tls_name", "ca_pem" }, "requestToken": "<新令牌>" }`。
    `serverTrust` 与 USB 下发的 `server_trust` 字段完全相同，设备可直接复用 `ParseServerTrust`；
  - 任一校验失败：申请标记为 `revoked`，返回 401。
- 每个申请只接受一次成功的 `/trust`，重放返回 409。
- 设备若校验 `M1` 失败（码输错），也调用此接口，发送 `{ "abort": "proof_mismatch" }`，
  让 Rodak 立即把申请标记为 `revoked`，桌面端显示“配对码不正确”。

之后设备写入 `server_trust`，改用固定证书和新令牌继续轮询 status，status 返回
`confirmed` 和 token，这部分是现有逻辑。

## 设备端改动

- `DeviceCloudConfigService` 的申请分支：未固定服务器时生成 6 位码（`esp_fill_random`，拒绝采样避免取模偏差），
  计算 SRP salt/verifier/B，随申请发送 `trustSetup`，**不发送 `deviceSecret`**；码只保存在
  内存和屏幕上，不写 NVS，重启即作废，需重新发起。
- 新增“仅记录指纹”的 TLS 配置：不加载 CA bundle，在 mbedTLS 验证回调里取对端证书 SPKI
  后放行。只在信任建立模式的 `binding/request`、`status`、`trust` 三个请求中使用，并且
  只针对用户选定的那个地址。
- SRP 会话：自行生成 16 字节 salt 并计算 verifier，经 `esp_srp_set_salt_verifier` 注入；
  收到 `A` 后先做长度与 `A mod N != 0` 预检，再调用 `esp_srp_get_session_key`。
- status 轮询遇到 `awaiting-device`：校验 `M1` 和服务器绑定 HMAC，调用 `trust` 接口
  （附加密的 `deviceSecret`），解密并校验 `server_trust`，用新 `requestToken` 替换旧令牌，
  然后用 `SaveSerialProvisioning(..., &server_trust)` 的同一事务写入。这样 pending → active、
  失败回滚和 cloud generation 的语义与 USB 完全一致。
- mDNS：在 `DiscoverServerTrustRoutes` 旁新增“列出全部 `_rodak._tcp`”查询，供首次选择
  服务器用（现有函数只接受与已固定 `id` 匹配的记录）。
- 界面：设置 → 设备云新增“连接到电脑上的 Rodak”，包括服务器列表和配对码页。配对码页复用
  现有配对码显示组件，增加倒计时。
- 主机测试：SRP 与 Rodak 测试向量互通；`seen_spki` 不一致中止；码过期；单码单次；
  3 次锁定；已固定服务器时入口关闭；现有模式回归不变。

## Rodak 端改动

- `device-binding-authorization.ts`：
  - 申请记录增加可选的 `trustSetup` 状态（加密）；
  - `request()` 在信任建立模式下不生成码，也不接受 `deviceSecret`；
  - `confirm()` 按申请类型分支；
  - 新增 `completeTrustSetup()`：校验 `M2`/HMAC、解密凭据、补 `__pairedSecretHash`、
    轮换 `requestToken`。
- 新增 `src/main/server/srp6a-client.ts`：与 `esp_srp` 一致的 SRP 客户端，独立单元测试。
- `http-api.ts`：binding/request 透传 `trustSetup`；status 增加 `awaiting-device`；
  新增 `/trust` 路由。
- 只有 HTTPS 监听器接受 `trustSetup`，明文 9080 端口一律拒绝。信任建立模式要求
  `secureNetwork` 已启用。
- `server-discovery.ts`：TXT 增加 `name=<计算机名>`，长度上限 63 字节，只用于在设备上
  展示，不参与信任。
- 渲染进程：沿用现有配对确认表单，增加 `awaiting-device` 的“等待设备确认”状态和失败
  文案。不新增 IPC 通道。
- 集成测试：真实 HTTPS 下的完整往返；中间人场景（设备上报的 `seenSpki` 与 Rodak 实际
  证书不同）；输错码；过期；`/trust` 重放；旧 `requestToken` 在 `/trust` 后无法领取 token；
  信任建立模式下带 `deviceSecret` 的申请被拒；现有模式回归。

## 用户体验

**设备端**

1. 首次开机，或设置 → 设备云 → “连接到电脑上的 Rodak”；
2. 若未联网，先进入现有 Wi-Fi 页面；
3. 查询 `_rodak._tcp`，超时 3 秒，可手动刷新：
   - 只有 1 台：直接显示“连接到 t14p 上的 Rodak”；
   - 多台：列表显示计算机名和 `id` 前 8 位；
   - 没有：提示检查是否同一网络、是否开了访客网络或 AP 隔离，并提供手动输入
     `https://<IP>:9443`，后续流程相同；
4. 显示 6 位码（3+3 分组、大字号）和倒计时；
5. 成功后显示“已接入 t14p 上的 Rodak”。

**桌面端**

现有智能家居或设备云页面的“待确认设备”列表出现该设备，标注“首次接入”。用户输入 6 位码，
家庭空间可选（不选则放入“未分配”），确认即可，和现在的绑定确认是同一个对话框。
输码框需同时接受现有模式的 8 位码和信任建立模式的 6 位码，按申请类型校验长度。

## 风险与边界

| 风险 | 处理 |
| --- | --- |
| 同网段攻击者冒充 Rodak 让设备接入 | 不知道码，`M1` 校验失败，设备不保存任何东西 |
| 攻击者在 TLS 层做中间人 | 证书指纹进入绑定 HMAC，两端不一致即中止 |
| 中间人窃取未验证信道中的令牌 | 长期凭据不进入该信道；`requestToken` 在 `/trust` 后轮换，旧令牌领不到 token |
| 攻击者冒充设备发起申请 | 用户看不到对应的屏幕码，不会输码；即便输了别的码，也只是失败 |
| 用户把码输到别的申请上 | 列表显示设备名和 MAC；码与申请一一对应，输错一次即作废，重新生成 |
| 访客网络、AP 隔离、组播被过滤 | 手动输入地址，流程不变 |
| `esp_srp` 运算慢 | 后台任务执行并显示进度；实测超过 5 秒再评估 EC J-PAKE |
| 内部 RAM 紧张 | 接入发生在绑定之前，此时 MQTT、语音、投屏均未运行；大数运算使用 PSRAM |
| Rodak 未开启 HTTPS | 信任建立模式不可用；Rodak 在首次收到申请或用户打开配对页时提示开启 |

本方案不改变：已固定服务器设备的任何路径、USB 下发、Secure Boot / Flash Encryption、
证书有效期校验（仍未启用）、外观发布者信任。

## 分阶段

1. **测试向量（已完成）**：`tests/lan_trust_setup` 直接编译 `esp_srp` 生成
   `srp6a-esp-vectors.json`，覆盖 salt、`A`、`B`、`S` 的前导零边界；Rodak 的
   `srp6a-client.ts` 用同一份向量逐字节对齐。协议层（HKDF、绑定 HMAC、AES-GCM）向量
   `trust-setup-protocol-vectors.json` 由 Rodak 生成，本仓库用 PSA 独立重算。
2. **Rodak 端（已完成）**：绑定服务分支、`/trust` 路由、桌面端“首次接入”标记、6 位输码与
   “等待设备确认”状态。集成测试在真实 HTTPS 上用设备模拟器（已用 `esp_srp` 向量校验）跑通
   成功接入、码输错、TLS 中间人、HMAC 篡改与重放、明文端口拒绝、携带密钥拒绝和现有模式回归。
3. **设备端**：申请分支、指纹记录、status 处理、信任事务，先主机测试再上实机。
4. **实机验收**：
   - 全新 NVS 设备，在热点和路由器两种网络下各接入一次；
   - 同网段放一个伪造 `_rodak._tcp` 的响应者，确认设备拒绝；
   - 码输错、过期、取消；
   - 接入后换 Wi-Fi，确认经现有发现找回；
   - 已固定服务器设备的现有绑定回归。
5. **文档**：更新本页状态、`trusted-server-discovery.md`、`serial-provisioning.md`，以及 Rodak
   的 `device-binding-authentication.md`。

## 已确认的决定（2026-10-18）

1. 信任建立模式使用 **6 位**数字码；现有模式保持 8 位不变。
2. 家庭空间**可选**，不选时放入“未分配”。
3. “Rodak 主动发现附近未接入设备”（设备注册 `_rodakos-setup._tcp`）放到**第二版**。

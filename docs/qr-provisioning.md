# 扫码配网二维码格式 v1

状态：格式为草案。固件端解析、解码、保存、扫码服务和设置页已实现并通过主机测试与固件构建，
尚未实机验收（见文末“实机验收”）。本文固定二维码格式，供 RodakOS 与服务端（首个对接方为
IntelliConnect）对接。设备连上服务器之后的协议见 [Rodak AIoT v1](rodak-aiot-contract-v1.md)
与 [Rodak realtime voice v1](rodak-realtime-voice-contract-v1.md)；本文不改动这两份约定。

## 目标与范围

服务端控制台把 Wi-Fi 与服务器地址编成一个二维码，用户用设备相机扫码，确认后设备保存
Wi-Fi 和引导地址，再走现有的 AIoT 绑定流程。适用于服务器不在同一局域网、无法用 mDNS
发现的部署。

二维码只提供：

- Wi-Fi SSID、认证方式与密码；
- 服务器的 HTTPS 地址。

二维码不提供证书、设备密钥、token、MQTT 或语音配置。设备经内置公共 CA 证书包
（`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL`）校验服务器证书和域名，其余配置从
`bootstrap` 和绑定响应取得，规则与 [串口配网](serial-provisioning.md) 相同。

非目标：

- 自签证书、纯 IP 地址或 `.local` 服务器。这些部署使用 USB 或
  [局域网首次接入](lan-onboarding-design.md)；
- 替换已固定服务器证书的设备的服务器；
- 企业级 Wi-Fi（802.1X）和 WEP。

## 载荷语法

二维码使用字节模式，内容为 UTF-8 文本，语法沿用通用 `WIFI:` 码：

```text
RODAK:V:1;S:<SSID>;T:<认证方式>;P:<密码>;U:<服务器地址>;;
```

- 前缀 `RODAK:` 区分大小写。
- 字段形式为 `<名称>:<值>;`。名称是一个大写字母，名称与值以第一个未转义的 `:` 分隔，值
  中其余的 `:` 按字面处理。
- 载荷以一个空字段结尾，即最后一个字段后再跟一个 `;`。
- 字段顺序任意。
- 解码后的载荷最长 512 字节。

### 转义

值中的 `\`、`;` 必须写成 `\\`、`\;`。生成方也可以转义 `,`、`:`、`"`，写成 `\,`、`\:`、
`\"`。解析器只接受这五种转义；其他反斜杠序列和结尾的单个 `\` 都使载荷无效。

`RODAK:` 载荷中 `"` 没有引号含义，按字面处理。

### 字段

| 字段 | 必需 | 规则 |
| --- | --- | --- |
| `V` | 是 | 格式版本，本文为 `1`。其他值返回 `qr_version_unsupported` |
| `S` | 是 | SSID，1–32 字节，不得含控制字符（0x00–0x1F、0x7F） |
| `T` | 是 | `WPA` 或 `nopass`，见下表 |
| `P` | 见 `T` | Wi-Fi 密码 |
| `U` | 是 | 服务器地址，见“服务器地址” |
| `H` | 否 | 隐藏网络标记，v1 解析后忽略 |

同一字段出现两次，载荷无效。其他单字母字段在 v1 中忽略，供以后的次版本扩展；必须理解的
新字段要升级 `V`。

| `T` 值（不区分大小写） | 处理 | `P` 规则 |
| --- | --- | --- |
| `WPA`、`WPA2`、`WPA3`、`SAE` | 个人级加密网络 | 8–63 字节，或正好 64 个十六进制字符 |
| `nopass` | 开放网络 | 缺省或为空 |
| `WEP`、含 `EAP` 的值、其他值 | 返回 `qr_auth_unsupported` | — |

生成方只写 `WPA` 或 `nopass`，其余写法只为兼容手工编写的码。

### 服务器地址

`U` 必须满足以下全部规则，否则返回 `qr_url_invalid`，另有说明的除外：

1. 协议为 `https`。`http` 返回 `qr_url_insecure`。
2. 主机为 ASCII 域名（国际化域名写成 `xn--` 形式），不得为 IP 地址，也不得以 `.local`
   结尾。证书按这个域名校验，确认页也显示这个域名。
3. 可带端口 1–65535。`:443` 在规范化时去掉。
4. 路径只能为空、`/` 或 `/api/v1/aiot/devices/bootstrap`。不支持把服务部署在子路径下。
5. 不得含用户信息（`user@`）、查询串、片段、空白或反斜杠。
6. 不得为 `/xiaozhi/ota/` 等旧路径，也不得指向 `tenclass.net` 及其子域名，二者都返回
   `qr_url_legacy`。串口配网仍接受 `/xiaozhi/ota/` 别名，扫码不接受。
7. 不超过 256 字节。

规范化结果为 `https://<主机>[:<端口>]/api/v1/aiot/devices/bootstrap`，与
`NormalizeSerialProvisioningBootstrapUrl` 的结果相同。主机名统一转为小写。

## 兼容标准 Wi-Fi 码

设备也接受手机“分享 Wi-Fi”生成的标准码，例如：

```text
WIFI:T:WPA;S:HomeNet;P:s3cret-pass;;
```

此时只保存 Wi-Fi，服务器地址保持不变。`WIFI:` 码中 `T` 可缺省，缺省等于 `nopass`；值两端
成对的未转义 `"` 会被去掉；其余规则与上文相同。

## 设备端流程

1. 用户在设置中打开“扫码配置”。设备只开相机，不同时开语音或云端连接，原因见
   [相机内存诊断](camera-teardown-diagnostics.md)。
2. 识别到载荷后立即关闭相机，再解析。解析失败时显示错误，用户可以重扫。
3. 确认页显示 SSID 和服务器域名，不显示密码。用户确认后才写入任何配置。
4. 设备已固定服务器证书时，返回 `qr_server_pinned`，不改动任何配置。用户需先解除绑定并
   清除服务器信任，或使用 USB。
5. 保存规则与串口配网相同，并共用其中断恢复标记 `serial_prov/pending`：置标记 → 保存
   Wi-Fi → 用 `kPreserveCredentials` 保存引导地址 → 清标记。地址保存失败时恢复原来的
   Wi-Fi（原来没有则清空）。规范化地址与当前地址相同时，保留已有绑定和凭据；地址不同时，
   按串口配网的迁移规则清空 AIoT、MQTT 和语音缓存。回滚失败或云端状态不确定时保留标记，
   下次开机由 `SerialProvisioningService::RecoverPendingTransaction` 清空 Wi-Fi 并重置引导
   地址。`WIFI:` 码只保存 Wi-Fi，已固定证书的设备也允许。
6. 连上 Wi-Fi 后走现有流程：`bootstrap` → `binding/request` → 设备屏幕显示配对码 → 用户
   在服务端确认 → 轮询 `status` 取得 token 和 `unifiedMqtt`。

设备不在日志、串口回复或遥测中输出 `P` 或整段载荷。相机帧和解码缓冲在离开扫码页后释放。

### 错误码

错误码用于日志和界面映射，不回显载荷内容。

| 错误码 | 原因 |
| --- | --- |
| `qr_not_provisioning` | 不是 `RODAK:` 或 `WIFI:` 载荷 |
| `qr_version_unsupported` | `V` 缺失或不为 `1` |
| `qr_syntax_invalid` | 转义错误、缺少结尾 `;;`、字段重复、超过 512 字节 |
| `qr_ssid_invalid` | `S` 缺失、为空、超长或含控制字符 |
| `qr_auth_unsupported` | `T` 缺失（仅 `RODAK:`）或不受支持 |
| `qr_password_invalid` | `P` 与 `T` 不匹配或长度不符 |
| `qr_url_invalid` | `U` 缺失或违反“服务器地址”规则 1 以外的规则 |
| `qr_url_insecure` | `U` 使用 `http` |
| `qr_url_legacy` | `U` 为旧服务路径或旧服务主机 |
| `qr_server_pinned` | 设备已固定服务器证书，或服务器信任记录无法读取 |

保存结果码（`SetupQrApplyStatusCode`）：

| 结果码 | 含义 | 后续动作 |
| --- | --- | --- |
| `qr_wifi_saved` | 只保存了 Wi-Fi | 重连 Wi-Fi |
| `qr_server_unchanged` | Wi-Fi 已保存，引导地址与当前相同，绑定保留 | 重连 Wi-Fi |
| `qr_server_changed` | Wi-Fi 与新引导地址已保存，云端缓存已清空 | 重连 Wi-Fi 并重启云端 |
| `qr_storage_failed` | 未改动，或改动已全部回滚 | 提示重试 |
| `qr_state_uncertain` | 回滚失败，保留恢复标记 | 提示重启设备 |

## 服务端要求

以 IntelliConnect 为例，nginx 终止 TLS。

### 二维码生成

- 按上文语法生成，`V:1`，`T` 只用 `WPA` 或 `nopass`，`U` 用控制台对外的 HTTPS 域名。
- 纠错级别建议 M，四周留 4 个模块的空白，白底黑码。载荷尽量短：常见长度约 100–150
  字节，对应二维码版本 6–8，手机屏幕显示时易于识别。
- 二维码含 Wi-Fi 明文密码。控制台只在用户主动请求时显示，不记录日志，不保存图片。

### HTTPS 与 nginx

- 证书由公共 CA 签发，SAN 包含 `U` 中的域名；nginx 发送完整证书链（`fullchain.pem`）。
- `/api/v1/aiot/` 下的路径不得返回 301/302。固件关闭了自动重定向，例如补斜杠或
  http→https 跳转都会使请求失败。
- 实现 [Rodak AIoT v1](rodak-aiot-contract-v1.md) 第 2 节的五个设备接口，路径从域名根开始。

### 实时语音

- `bootstrap` 或 `status` 返回的语音地址使用 `wss://`，证书同样由公共 CA 签发。
- nginx 对语音路径配置 `proxy_http_version 1.1`、`Upgrade` 和 `Connection` 头；
  `proxy_read_timeout` 大于 30 秒，设备每 30 秒发一次 ping。
- 线路格式见 [Rodak realtime voice v1](rodak-realtime-voice-contract-v1.md)。

### MQTT

- 未固定服务器证书时，当前固件用明文 `mqtt://` 连接 `unifiedMqtt` 中的主机和端口，不读取
  `transport` 字段，也不校验 broker 证书。`unifiedMqtt` 必须指向 EMQX 的明文监听端口
  （通常为 1883）。v1 不要求 MQTT 使用 TLS。
- 风险：MQTT 密码默认等于设备 access token，会以明文经过网络。服务器在公网时，建议
  token 有效期保持较短，并只开放给设备所需的主题权限。

## 扫码服务与设置页

`main/phone_os/setup_qr_scan_service.cc` 用一个 PSRAM 栈（12 KiB）的后台任务完成整个扫码：

1. 申请独占音频焦点并释放编解码器，与 Camera 应用相同，为 DVP 驱动留出 DMA 空间。
2. 以 `kLocal` 身份开 320×240 预览。相机已被 Camera 应用或远程画面占用时返回
   `qr_camera_busy`，不抢占已有租约。
3. 每 150 ms 取一次新帧交给 `SetupQrDecoder`，两次解码之间让出 20 ms。5 秒内没有第一帧，
   或预览报错，返回 `qr_camera_unavailable`；90 秒内没有识别到返回 `qr_scan_timeout`。
4. 识别到后先停相机、释放音频焦点和解码缓冲，再解析载荷。相机与 Wi-Fi/TLS 不会同时占用
   内存。
5. 进入确认状态。快照只给界面 SSID、是否开放网络和服务器 `host[:port]`，不含密码。
6. 用户确认后，保存事务交给 main 循环所在的内部 RAM 栈任务执行（PSRAM 栈上不能写
   NVS），与局域网首次接入相同。成功后断开并重连 Wi-Fi；引导地址变化时调用
   `UnifiedMqttService::RequestCredentialRefresh()` 重启设备，与 USB 配网相同。
7. 取消、离开设置或返回键都会让后台任务在一个轮询周期内停相机，并清除已解析的载荷。

设置页（`main/apps/settings/settings_setup_qr_page.cc`）在“连接 Rodak”页底部提供“扫码配置
Wi-Fi 与服务器”入口；服务器证书已固定或信任记录不可读时隐藏入口。全屏页按快照显示
“打开相机 → 扫码（显示已检查帧数）→ 确认 → 保存 → 结果”，每个错误码对应一条中文提示；
`qr_server_pinned` 和 `qr_state_uncertain` 不提供“重新扫码”。

## 实机验收

尚未进行。需要在 BigSmart 上验证：

- 扫码页打开到第一帧的时间，室内光照下识别 M 级版本 5–8 码的距离范围与耗时；
- 扫码期间与关闭相机后 `Main health` 的 `internal_free`/`internal_largest`，关闭后最大内部
  块回到 8 KiB 门槛以上；
- 取消、返回键和超时三条路径都能停相机，且再次进入 Camera 应用正常；
- `qr_server_changed` 后设备重启、连接新服务器并显示配对码；`qr_server_unchanged` 保留绑定；
- 已固定证书的设备上入口隐藏，`WIFI:` 码仍只改 Wi-Fi；
- 保存过程中断电后，下次开机由恢复标记清空 Wi-Fi 并重置引导地址。

## 测试向量

“规范化地址”列中省略了公共后缀 `/api/v1/aiot/devices/bootstrap`。

| # | 载荷 | 结果 |
| --- | --- | --- |
| 1 | `RODAK:V:1;S:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com;;` | 成功；SSID `HomeNet`；地址 `https://ic.example.com` |
| 2 | `RODAK:V:1;S:Cafe\;2F;T:nopass;U:https://IC.Example.com:8443/api/v1/aiot/devices/bootstrap;;` | 成功；SSID `Cafe;2F`，开放网络；地址 `https://ic.example.com:8443` |
| 3 | `RODAK:U:https://ic.example.com:443/;T:wpa;P:s3cret-pass;S:HomeNet;V:1;;` | 成功；字段顺序任意；地址 `https://ic.example.com` |
| 4 | `RODAK:V:1;S:HomeNet;T:WPA;P:s3cret-pass;U:http://ic.example.com;;` | `qr_url_insecure` |
| 5 | `RODAK:V:1;S:HomeNet;T:WPA;P:s3cret-pass;U:https://192.168.1.10;;` | `qr_url_invalid` |
| 6 | `RODAK:V:1;S:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com/xiaozhi/ota/;;` | `qr_url_legacy` |
| 7 | `RODAK:V:2;S:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com;;` | `qr_version_unsupported` |
| 8 | `RODAK:V:1;S:HomeNet;T:WPA;P:short;U:https://ic.example.com;;` | `qr_password_invalid` |
| 9 | `RODAK:V:1;S:HomeNet;S:Other;T:WPA;P:s3cret-pass;U:https://ic.example.com;;` | `qr_syntax_invalid` |
| 10 | `RODAK:V:1;S:HomeNet;T:WEP;P:s3cret-pass;U:https://ic.example.com;;` | `qr_auth_unsupported` |
| 11 | `RODAK:V:1;S:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com` | `qr_syntax_invalid`（缺少结尾 `;;`） |
| 12 | `WIFI:T:WPA;S:"HomeNet";P:s3cret-pass;;` | 成功，只配 Wi-Fi；SSID `HomeNet` |
| 13 | `https://ic.example.com` | `qr_not_provisioning` |

## 固件实现计划

1. 解析器 `ParseSetupQr()`（已完成）：纯函数，复用串口配网的地址规范化，用上面的测试向量做主机单测。
2. 解码 `SetupQrDecoder`（已完成）：相机 RGB565 帧转灰度后交给 quirc 解码，缓冲放在 PSRAM。
3. 扫码服务与设置页（已完成，未实机验收）：见“扫码服务与设置页”。
4. 保存 `ApplySetupQrProvisioning`（已完成）：事务核心通过 `SetupQrProvisioningStore` 接口
   访问存储，主机测试用假存储对每一步写入做故障注入；固件实现
   `DeviceSetupQrProvisioningStore` 封装 `WiFiConfig`、恢复标记和 `DeviceCloudConfigService`。
   保存后重连 Wi-Fi、重启云端由扫码页负责。

## 解码器与 quirc

`components/quirc/` 是 ESP-IDF 组件注册表 `espressif/quirc` 1.2.0（上游
[dlbeer/quirc](https://github.com/dlbeer/quirc)，ISC 许可）中的 `lib/` 源码与许可证，未作
改动。没有通过 `idf_component.yml` 引入：注册表依赖会改变 `dependencies.lock`，而 Camera
overlay 会拒绝未审阅的依赖图。来源、归档哈希与各文件 LF 归一化 SHA-256 见
`components/quirc/provenance.json`。编译选项与上游组件相同：`QUIRC_FLOAT_TYPE=float`、
`QUIRC_USE_TGMATH`。

`main/phone_os/setup_qr_decoder.cc` 封装 quirc：

- RGB565（小端，`stride` 以字节计）按 BT.601 转为 8 位亮度，白色映射为 255。
- 返回第一个能解码的码，镜像码翻转后再试一次。超过 512 字节的载荷不返回。
- 所有缓冲都从堆分配，`CONFIG_SPIRAM_USE_MALLOC` 下会进 PSRAM。`QUIRC_MAX_REGIONS` 为
  254，标记直接写在图像缓冲里，没有单独的标记缓冲。320×240 帧约需 105 KB：图像
  76,800 B、`struct quirc` 约 12 KB、`quirc_code` 加 `quirc_data` 约 13 KB、洪水填充栈
  约 2.5 KB（结构体大小按主机构建计，Xtensa 上相同或略小）。`Release()` 释放全部缓冲。
- 每次解码后清零 `quirc_data`，不在可复用缓冲中留下 Wi-Fi 密码。
- 上游测试显示 `quirc_end`/`quirc_decode` 在 Xtensa 上约用 10 KiB 栈，因此必须在至少
  12 KiB 栈的专用任务中调用，不能在 LVGL 或相机回调任务中调用。

### Flash 与内存增量

2026-11-20 在 ESP-IDF 6.0.2 下对当前工作区构建两次。第二次只加入一个用 `volatile` 条件
保护、运行时不会执行的调用，强制链接解码器和解析器；用 `esp_idf_size --diff` 对比。
测量用的调用已删除。

| 项目 | 基线 | 链接解码器后 | 增量 |
| --- | --- | --- | --- |
| 镜像（`esp_idf_size`） | 7,182,809 B | 7,207,405 B | +24,596 B |
| 其中 `libquirc.a` | — | — | +17,010 B（代码 12,946 B，`version_db` 常量 4,064 B） |
| 其中 `libmain.a` | — | — | +6,900 B（解码器、解析器及其模板实例） |
| IRAM | 16,384 B（100%） | 16,384 B | 0 |
| DIRAM `.bss` | 27,496 B | 27,512 B | +16 B |

增量约 24 KB，占 `ota_0`（13.3125 MiB）的 0.18%。IRAM 不变，内部 RAM 静态段只多 16 B；
运行时的大缓冲在 PSRAM 中按需分配。

接入扫码服务和设置页后再次构建（同一工作区，对比上表“链接解码器后”）：

| 项目 | 链接解码器后 | 完整扫码功能 | 增量 |
| --- | --- | --- | --- |
| 镜像 | 7,207,405 B | 7,226,105 B | +18,700 B |
| IRAM | 16,384 B（100%） | 16,384 B | 0 |
| DIRAM `.bss` | 27,512 B | 27,784 B | +272 B（服务对象） |

相对最初基线，整个扫码功能共 +43,296 B Flash、+288 B 内部 RAM 静态段，IRAM 不变。

### 主机测试

`tests/setup_qr_decoder/` 直接编译固件同一份 quirc 源码，测试：上游 `test of quirc` 图像；
RODAK 载荷（版本 5、M 级）的灰度图与 320×240 帧；带行填充的 RGB565；镜像码；跨尺寸
复用缓冲；空白帧；非法几何参数。`fixtures/` 中的 PGM 图像由 `generate_fixtures.py`
（segno 1.6.6）一次性生成并提交，运行测试不需要二维码编码库。

quirc 首次 `quirc_resize` 时以空指针、长度 0 调用 `memcpy`，结果无害但会触发 UBSan
`nonnull-attribute`。测试只对 `quirc.c` 关闭这一项检查，不修改供应商源码。

# 信任建立模式 SRP6a 测试向量

[无 USB 局域网首次接入设计](../../docs/lan-onboarding-design.md) 第 1 步。直接编译 ESP-IDF
v6.0.2 的 `esp_srp`（设备端实际运行的 SRP 服务器），配合一份按同样哈希与填充规则写的主机
参考客户端，两者互相验证后生成 `srp6a-esp-vectors.json`。Rodak 端的 TypeScript 客户端用
这份文件逐字节对齐。

`CMakeLists.txt` 锁定 4 个 `esp_srp` 源文件的 SHA-256；IDF 升级导致源码变化时配置失败，
需重新审阅填充与长度处理后再更新哈希。`esp_fill_random` 由确定性队列代替，私钥 `b` 经
该队列注入，因此向量可复现。

向量覆盖的边界：

| 名称 | 边界 |
| --- | --- |
| `typical` | 常规值 |
| `salt-leading-zero` | salt 首字节为 0，仍按 16 字节原样参与 `x` 与 `M1` |
| `short-B` | `B` 不足 384 字节：`u` 用 `PAD(B)`，`M1` 用原样 `B` |
| `A-leading-zero` | `A` 首字节为 0，仍固定 384 字节 |
| `S-leading-zero` | `S` 不足 384 字节，`K = H(S)` 不填充 |

测试同时记录了 `esp_srp` 的两个限制，设备端实现必须绕开：

- `esp_srp_gen_salt_verifier` / `esp_srp_srv_pubkey` 自行生成的 salt 若首字节为 0，返回的
  salt 只有 15 字节，verifier 也按 15 字节计算。设备应自行生成 16 字节 salt、计算 verifier，
  再用 `esp_srp_set_salt_verifier` + `esp_srp_srv_pubkey_from_salt_verifier`；
- `esp_srp_get_session_key` 拒绝 `A mod N == 0` 时，错误路径会对未初始化的哈希对象调用
  `psa_hash_abort`。设备在调用前须先检查 `A` 为 384 字节且 `A mod N != 0`。

```sh
cmake -S tests/lan_trust_setup -B build/lan-trust -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DRODAKOS_IDF_PATH=/path/to/esp-idf-v6.0.2
cmake --build build/lan-trust
ctest --test-dir build/lan-trust --output-on-failure
```

`trust-setup-protocol-vectors.json` 是 SRP 之上的协议层向量（HKDF-SHA256 派生、证书绑定
HMAC-SHA256、AES-256-GCM），由 Rodak `scripts/generate-trust-setup-protocol-vectors.ts` 生成；
`protocol_vectors_test.cc` 用固件同一套 mbedTLS PSA API 独立重算并比对。两份文件在两个
仓库中保持同一副本。

只有规则或边界有意变更时才重新生成向量：设置 `LAN_TRUST_SETUP_WRITE_VECTORS=1` 运行一次，
提交更新后的 JSON，并同步 Rodak 仓库中的副本。

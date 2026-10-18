#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 信任建立模式 SRP6a 的主机参考实现与 esp_srp 驱动。
//
// 设备端实际运行 ESP-IDF 自带的 esp_srp（SRP 服务器）。Rodak 是 SRP 客户端，没有现成的
// esp_srp 客户端可对照，因此这里按 esp_srp 的哈希与填充规则写一份独立的客户端参考实现，
// 两者互相验证后导出测试向量，供 Rodak 的 TypeScript 客户端逐字节对齐。
namespace lan_trust_setup {

using Bytes = std::vector<uint8_t>;

inline constexpr const char* kUsername = "rodak-trust-setup/v1";
inline constexpr size_t kGroupBytes = 384;
inline constexpr size_t kSaltBytes = 16;
inline constexpr size_t kPrivateBytes = 32;
inline constexpr size_t kHashBytes = 64;

const Bytes& GroupN();
Bytes Sha512(const Bytes& data);
Bytes Concat(std::initializer_list<const Bytes*> parts);
Bytes FromString(const std::string& text);
Bytes FromHex(const std::string& hex);
std::string ToHex(const Bytes& bytes);
// 去掉前导零字节，与 esp_srp 的 esp_mpi_to_bin 一致。
Bytes Minimal(const Bytes& bytes);
Bytes PadToGroup(const Bytes& bytes);

// k = H(N | PAD(g))
Bytes ComputeK();
// x = H(salt | H(I ":" P))，salt 按原样（含前导零）参与哈希。
Bytes ComputeX(const Bytes& salt, const std::string& username, const std::string& password);
// v = g^x mod N，最小长度编码。
Bytes ComputeVerifier(const Bytes& salt, const std::string& username, const std::string& password);

struct ClientResult {
    Bytes a;   // 客户端私钥
    Bytes A;   // 公钥，固定 384 字节
    Bytes u;
    Bytes S;   // 最小长度编码
    Bytes K;   // H(S)
    Bytes M1;  // H(H(N) xor H(PAD(g)) | H(I) | salt | A | B | K)
    Bytes M2;  // H(A | M1 | K)，客户端据此验证设备
};

// Rodak 一侧的计算。B 为设备发来的原始字节（最小长度编码），按原样参与 M1。
// B mod N == 0 或 u == 0 时抛出异常。
ClientResult RunClient(const std::string& username, const std::string& password,
                       const Bytes& salt, const Bytes& B, const Bytes& a);

struct EspServerSession;

// esp_srp 设备一侧。salt 与 verifier 经 esp_srp_set_salt_verifier 注入，b 由确定性随机源
// 提供。不使用 esp_srp_srv_pubkey 自带的 salt 生成：它在 salt 首字节为 0 时长度不一致。
class EspServer {
public:
    EspServer(const Bytes& salt, const Bytes& verifier, const Bytes& b);
    ~EspServer();
    EspServer(const EspServer&) = delete;
    EspServer& operator=(const EspServer&) = delete;

    const Bytes& B() const { return B_; }
    // 调用前必须已校验 A：384 字节且 A mod N != 0，见 ValidClientPublicKey。
    Bytes SessionKey(const Bytes& A);
    // M1 正确时返回 true 并写出 M2。
    bool ExchangeProofs(const std::string& username, const Bytes& M1, Bytes& M2);

private:
    EspServerSession* session_;
    Bytes B_;
};

// 设备端在调用 esp_srp_get_session_key 之前的预检。esp_srp 自身的 A mod N == 0 拒绝分支
// 用 goto 跳过了 hash_op 的初始化，错误路径随后对它调用 psa_hash_abort（未定义行为），
// 不能依赖它；长度也必须由调用方保证。
bool ValidClientPublicKey(const Bytes& A);

}  // namespace lan_trust_setup

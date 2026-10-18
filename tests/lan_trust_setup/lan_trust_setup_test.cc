#include "test_framework.h"

#include "esp_random.h"
#include "esp_srp.h"
#include "srp_harness.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace lan_trust_setup;

namespace {

// 确定性派生 32 字节私钥候选（SHA-512 前半），保证向量可复现；按条件搜索以覆盖前导零等边界。
Bytes Candidate(const std::string& label, int counter) {
    const Bytes digest = Sha512(FromString(label + "-" + std::to_string(counter)));
    return Bytes(digest.begin(), digest.begin() + kPrivateBytes);
}

Bytes ServerPublicKey(const Bytes& verifier, const Bytes& b) {
    EspServer server(FromHex("01"), verifier, b);
    return server.B();
}

struct Vector {
    std::string name;
    std::string password;
    Bytes salt;
    Bytes b;
    Bytes a;
};

struct Evaluated {
    Vector input;
    Bytes x;
    Bytes verifier;
    Bytes B;
    ClientResult client;
};

Evaluated Evaluate(const Vector& vector) {
    Evaluated out{vector, ComputeX(vector.salt, kUsername, vector.password),
                  ComputeVerifier(vector.salt, kUsername, vector.password), {}, {}};
    EspServer server(vector.salt, out.verifier, vector.b);
    out.B = server.B();
    out.client = RunClient(kUsername, vector.password, vector.salt, out.B, vector.a);

    RODAK_CHECK(ValidClientPublicKey(out.client.A));
    RODAK_CHECK(server.SessionKey(out.client.A) == out.client.K);
    Bytes M2;
    RODAK_CHECK(server.ExchangeProofs(kUsername, out.client.M1, M2));
    RODAK_CHECK(M2 == out.client.M2);
    return out;
}

Bytes FindPrivateKey(const std::string& label, bool (*accept)(const Bytes&)) {
    for (int counter = 0; counter < 20000; ++counter) {
        const Bytes candidate = Candidate(label, counter);
        if (accept(candidate)) return candidate;
    }
    throw std::runtime_error("no candidate found for " + label);
}

const std::string kPassword = "482913";
const Bytes& BaseSalt() {
    static const Bytes salt = FromHex("8f1c2a7e5b90d34612a7c0e9f4b8d215");
    return salt;
}
const Bytes& BaseVerifier() {
    static const Bytes verifier = ComputeVerifier(BaseSalt(), kUsername, kPassword);
    return verifier;
}

std::vector<Vector> Vectors() {
    const Bytes base_a = Candidate("rodak-srp-vector-a", 0);
    const Bytes base_b = Candidate("rodak-srp-vector-b", 0);
    std::vector<Vector> vectors;
    vectors.push_back({"typical", kPassword, BaseSalt(), base_b, base_a});
    vectors.push_back({"salt-leading-zero", "000017",
                       FromHex("00a3915c7e2b48d06f1e83c4a95b7d20"), base_b, base_a});
    // M1 按原样哈希设备发来的最小长度 B，u 则使用 PAD(B)；短 B 是最容易写错的地方。
    vectors.push_back({"short-B", kPassword, BaseSalt(),
                       FindPrivateKey("rodak-srp-vector-short-b", [](const Bytes& b) {
                           return ServerPublicKey(BaseVerifier(), b).size() < kGroupBytes;
                       }),
                       base_a});
    // A 必须固定 384 字节编码，不能去掉前导零。
    vectors.push_back({"A-leading-zero", kPassword, BaseSalt(), base_b,
                       FindPrivateKey("rodak-srp-vector-a-zero", [](const Bytes& a) {
                           const auto result = RunClient(kUsername, kPassword, BaseSalt(),
                               ServerPublicKey(BaseVerifier(),
                                               Candidate("rodak-srp-vector-b", 0)), a);
                           return result.A[0] == 0;
                       })});
    // K = H(S) 使用最小长度的 S，不填充。
    vectors.push_back({"S-leading-zero", kPassword, BaseSalt(), base_b,
                       FindPrivateKey("rodak-srp-vector-s-zero", [](const Bytes& a) {
                           const auto result = RunClient(kUsername, kPassword, BaseSalt(),
                               ServerPublicKey(BaseVerifier(),
                                               Candidate("rodak-srp-vector-b", 0)), a);
                           return result.S.size() < kGroupBytes;
                       })});
    return vectors;
}

std::string Field(const char* name, const Bytes& value, bool last = false) {
    return std::string("      \"") + name + "\": \"" + ToHex(value) + "\"" + (last ? "\n" : ",\n");
}

std::string RenderVectors(const std::vector<Evaluated>& evaluated) {
    std::ostringstream out;
    out << "{\n"
        << "  \"description\": \"RodakOS 信任建立模式 SRP6a 测试向量，由 ESP-IDF esp_srp 设备端与"
           "主机参考客户端互相验证后生成。字节串均为小写十六进制。\",\n"
        << "  \"generator\": \"rodakos tests/lan_trust_setup\",\n"
        << "  \"source\": \"ESP-IDF v6.0.2 7101770dc6db2667b3c477cc31365dd1acd6db4e "
           "components/protocomm/src/crypto/srp6a\",\n"
        << "  \"rules\": {\n"
        << "    \"hash\": \"SHA-512\",\n"
        << "    \"k\": \"H(N | PAD(g))\",\n"
        << "    \"x\": \"H(salt | H(username ':' password))，salt 按原样含前导零\",\n"
        << "    \"verifier\": \"g^x mod N，最小长度编码\",\n"
        << "    \"B\": \"(k*v + g^b) mod N，最小长度编码，设备按此发送\",\n"
        << "    \"A\": \"g^a mod N，固定 384 字节\",\n"
        << "    \"u\": \"H(PAD(A) | PAD(B))\",\n"
        << "    \"S\": \"(B - k*g^x)^(a + u*x) mod N，最小长度编码\",\n"
        << "    \"K\": \"H(S)\",\n"
        << "    \"M1\": \"H(H(N) xor H(PAD(g)) | H(username) | salt | A | B | K)，B 为最小长度原样字节\",\n"
        << "    \"M2\": \"H(A | M1 | K)\",\n"
        << "    \"privateKeys\": \"a 与 b 各 32 字节，按大端整数解释\"\n"
        << "  },\n"
        << "  \"username\": \"" << kUsername << "\",\n"
        << "  \"N\": \"" << ToHex(GroupN()) << "\",\n"
        << "  \"g\": \"05\",\n"
        << "  \"k\": \"" << ToHex(ComputeK()) << "\",\n"
        << "  \"vectors\": [\n";
    for (size_t index = 0; index < evaluated.size(); ++index) {
        const auto& item = evaluated[index];
        out << "    {\n"
            << "      \"name\": \"" << item.input.name << "\",\n"
            << "      \"password\": \"" << item.input.password << "\",\n"
            << Field("salt", item.input.salt) << Field("x", item.x)
            << Field("verifier", item.verifier) << Field("b", item.input.b) << Field("B", item.B)
            << Field("a", item.input.a) << Field("A", item.client.A) << Field("u", item.client.u)
            << Field("S", item.client.S) << Field("K", item.client.K)
            << Field("M1", item.client.M1) << Field("M2", item.client.M2, true)
            << (index + 1 == evaluated.size() ? "    }\n" : "    },\n");
    }
    out << "  ]\n}\n";
    return out.str();
}

}  // namespace

RODAK_TEST("esp_srp 生成的 B 与参考客户端互通，b 按大端解释") {
    const Bytes b = Candidate("rodak-srp-vector-b", 0);
    EspServer server(BaseSalt(), BaseVerifier(), b);
    const auto client = RunClient(kUsername, kPassword, BaseSalt(), server.B(),
                                  Candidate("rodak-srp-vector-a", 0));
    RODAK_CHECK(server.SessionKey(client.A) == client.K);
    RODAK_CHECK_EQ(server.B().size() <= kGroupBytes, true);
}

RODAK_TEST("参考计算的 verifier 与 esp_srp_gen_salt_verifier 一致（salt 首字节非零）") {
    const Bytes salt = FromHex("c4a1f07e3b9d26508e7f1a2c3d4b5e6f");
    lan_trust_setup_queue_random(salt.data(), salt.size());
    char* esp_salt = nullptr;
    char* esp_verifier = nullptr;
    int verifier_length = 0;
    RODAK_CHECK_EQ(esp_srp_gen_salt_verifier(kUsername, static_cast<int>(std::string(kUsername).size()),
                                             kPassword.data(), static_cast<int>(kPassword.size()),
                                             &esp_salt, static_cast<int>(salt.size()),
                                             &esp_verifier, &verifier_length),
                   ESP_OK);
    const Bytes returned_salt(esp_salt, esp_salt + salt.size());
    const Bytes returned_verifier(esp_verifier, esp_verifier + verifier_length);
    free(esp_salt);
    free(esp_verifier);
    RODAK_CHECK(returned_salt == salt);
    RODAK_CHECK(returned_verifier == ComputeVerifier(salt, kUsername, kPassword));
}

RODAK_TEST("esp_srp_gen_salt_verifier 在 salt 首字节为零时丢掉该字节，设备不能用它生成 salt") {
    // esp_mpi_to_bin 输出最小长度：返回的 salt 缓冲只有 15 字节，调用方却按 16 字节使用，
    // verifier 也按 15 字节计算。设备改为自行生成 16 字节 salt 并计算 verifier，再经
    // esp_srp_set_salt_verifier 注入（见 srp_harness.h）。
    const Bytes salt = FromHex("00b7c2e94f1a6d3058c2e1f7a9b4d306");
    lan_trust_setup_queue_random(salt.data(), salt.size());
    char* esp_salt = nullptr;
    char* esp_verifier = nullptr;
    int verifier_length = 0;
    RODAK_CHECK_EQ(esp_srp_gen_salt_verifier(kUsername, static_cast<int>(std::string(kUsername).size()),
                                             kPassword.data(), static_cast<int>(kPassword.size()),
                                             &esp_salt, static_cast<int>(salt.size()),
                                             &esp_verifier, &verifier_length),
                   ESP_OK);
    const Bytes returned_verifier(esp_verifier, esp_verifier + verifier_length);
    free(esp_salt);
    free(esp_verifier);
    RODAK_CHECK(returned_verifier == ComputeVerifier(Minimal(salt), kUsername, kPassword));
    RODAK_CHECK(returned_verifier != ComputeVerifier(salt, kUsername, kPassword));
}

RODAK_TEST("错误口令产生的 M1 被 esp_srp 拒绝") {
    const Bytes b = Candidate("rodak-srp-vector-b", 0);
    EspServer server(BaseSalt(), BaseVerifier(), b);
    const auto client = RunClient(kUsername, "482914", BaseSalt(), server.B(),
                                  Candidate("rodak-srp-vector-a", 0));
    RODAK_CHECK(server.SessionKey(client.A) != client.K);
    Bytes M2;
    RODAK_CHECK_FALSE(server.ExchangeProofs(kUsername, client.M1, M2));
}

RODAK_TEST("设备在调用 esp_srp 前拒绝 0、N 与非 384 字节的客户端公钥") {
    RODAK_CHECK_FALSE(ValidClientPublicKey(Bytes(kGroupBytes, 0)));
    RODAK_CHECK_FALSE(ValidClientPublicKey(GroupN()));
    RODAK_CHECK_FALSE(ValidClientPublicKey(Bytes(kGroupBytes - 1, 1)));
    RODAK_CHECK(ValidClientPublicKey(PadToGroup(FromHex("02"))));
}

RODAK_TEST("客户端拒绝 B mod N == 0") {
    bool rejected = false;
    try {
        (void)RunClient(kUsername, kPassword, BaseSalt(), GroupN(), Candidate("rodak-srp-vector-a", 0));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    RODAK_CHECK(rejected);
}

RODAK_TEST("测试向量覆盖全部边界、双端互通，且与提交的向量文件一致") {
    std::vector<Evaluated> evaluated;
    for (const auto& vector : Vectors()) evaluated.push_back(Evaluate(vector));

    RODAK_CHECK(evaluated[1].input.salt[0] == 0);
    RODAK_CHECK(evaluated[2].B.size() < kGroupBytes);
    RODAK_CHECK(evaluated[3].client.A[0] == 0);
    RODAK_CHECK(evaluated[4].client.S.size() < kGroupBytes);
    for (const auto& item : evaluated) RODAK_CHECK(item.client.A.size() == kGroupBytes);

    const std::string rendered = RenderVectors(evaluated);
    if (std::getenv("LAN_TRUST_SETUP_WRITE_VECTORS") != nullptr) {
        std::ofstream(LAN_TRUST_SETUP_VECTORS, std::ios::binary) << rendered;
    }
    std::ifstream committed(LAN_TRUST_SETUP_VECTORS, std::ios::binary);
    RODAK_CHECK(committed.good());
    std::ostringstream content;
    content << committed.rdbuf();
    RODAK_CHECK(content.str() == rendered);
}

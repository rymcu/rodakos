#include "srp_harness.h"

#include "esp_random.h"
#include "esp_srp.h"

#include <mbedtls/bignum.h>
#include <psa/crypto.h>

#include <stdexcept>

namespace lan_trust_setup {
namespace {

constexpr const char* kGroupHex =
    "ffffffffffffffffc90fdaa22168c234c4c6628b80dc1cd129024e088a67cc74020bbea63b139b22514a08798e3404dd"
    "ef9519b3cd3a431b302b0a6df25f14374fe1356d6d51c245e485b576625e7ec6f44c42e9a637ed6b0bff5cb6f406b7ed"
    "ee386bfb5a899fa5ae9f24117c4b1fe649286651ece45b3dc2007cb8a163bf0598da48361c55d39a69163fa8fd24cf5f"
    "83655d23dca3ad961c62f356208552bb9ed529077096966d670c354e4abc9804f1746c08ca18217c32905e462e36ce3b"
    "e39e772c180e86039b2783a2ec07a28fb5c55df06f4c52c9de2bcbf6955817183995497cea956ae515d2261898fa0510"
    "15728e5a8aaac42dad33170d04507a33a85521abdf1cba64ecfb850458dbef0a8aea71575d060c7db3970f85a6e1e4c7"
    "abf5ae8cdb0933d71e8c94e04a25619dcee3d2261ad2ee6bf12ffa06d98a0864d87602733ec86a64521f2b18177b200c"
    "bbe117577a615d6c770988c0bad946e208e24fa074e5ab3143db5bfce0fd108e4b82d120a93ad2caffffffffffffffff";

class Mpi {
public:
    Mpi() { mbedtls_mpi_init(&value); }
    ~Mpi() { mbedtls_mpi_free(&value); }
    Mpi(const Mpi&) = delete;
    Mpi& operator=(const Mpi&) = delete;
    mbedtls_mpi value;
};

void Check(int result, const char* what) {
    if (result != 0) throw std::runtime_error(std::string("mbedtls failure: ") + what);
}

void EnsurePsa() {
    static const bool ready = psa_crypto_init() == PSA_SUCCESS;
    if (!ready) throw std::runtime_error("psa_crypto_init failed");
}

void Load(Mpi& target, const Bytes& bytes) {
    Check(mbedtls_mpi_read_binary(&target.value, bytes.data(), bytes.size()), "read_binary");
}

Bytes Store(const Mpi& source, size_t length) {
    Bytes out(length);
    Check(mbedtls_mpi_write_binary(&source.value, out.data(), out.size()), "write_binary");
    return out;
}

Bytes StoreMinimal(const Mpi& source) {
    return Store(source, mbedtls_mpi_size(&source.value));
}

const Mpi& GroupMpi() {
    static Mpi* n = [] {
        auto* value = new Mpi();
        Load(*value, GroupN());
        return value;
    }();
    return *n;
}

Bytes GroupG() {
    return Bytes{5};
}

void ExpMod(Mpi& result, const Mpi& base, const Mpi& exponent) {
    Check(mbedtls_mpi_exp_mod(&result.value, &base.value, &exponent.value, &GroupMpi().value,
                              nullptr), "exp_mod");
}

}  // namespace

const Bytes& GroupN() {
    static const Bytes n = FromHex(kGroupHex);
    return n;
}

Bytes Sha512(const Bytes& data) {
    EnsurePsa();
    Bytes digest(kHashBytes);
    size_t length = 0;
    if (psa_hash_compute(PSA_ALG_SHA_512, data.data(), data.size(), digest.data(), digest.size(),
                         &length) != PSA_SUCCESS || length != kHashBytes) {
        throw std::runtime_error("sha512 failed");
    }
    return digest;
}

Bytes Concat(std::initializer_list<const Bytes*> parts) {
    Bytes out;
    for (const auto* part : parts) out.insert(out.end(), part->begin(), part->end());
    return out;
}

Bytes FromString(const std::string& text) {
    return Bytes(text.begin(), text.end());
}

Bytes FromHex(const std::string& hex) {
    if (hex.size() % 2 != 0) throw std::invalid_argument("odd hex length");
    Bytes out(hex.size() / 2);
    for (size_t index = 0; index < out.size(); ++index) {
        out[index] = static_cast<uint8_t>(std::stoul(hex.substr(index * 2, 2), nullptr, 16));
    }
    return out;
}

std::string ToHex(const Bytes& bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        out.push_back(kDigits[byte >> 4]);
        out.push_back(kDigits[byte & 0x0f]);
    }
    return out;
}

Bytes Minimal(const Bytes& bytes) {
    size_t first = 0;
    while (first < bytes.size() && bytes[first] == 0) ++first;
    return Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(first), bytes.end());
}

Bytes PadToGroup(const Bytes& bytes) {
    const Bytes minimal = Minimal(bytes);
    if (minimal.size() > kGroupBytes) throw std::invalid_argument("value larger than group");
    Bytes out(kGroupBytes - minimal.size(), 0);
    out.insert(out.end(), minimal.begin(), minimal.end());
    return out;
}

Bytes ComputeK() {
    const Bytes g = PadToGroup(GroupG());
    return Sha512(Concat({&GroupN(), &g}));
}

Bytes ComputeX(const Bytes& salt, const std::string& username, const std::string& password) {
    const Bytes identity = Sha512(FromString(username + ":" + password));
    return Sha512(Concat({&salt, &identity}));
}

Bytes ComputeVerifier(const Bytes& salt, const std::string& username, const std::string& password) {
    Mpi g, x, v;
    Load(g, GroupG());
    Load(x, ComputeX(salt, username, password));
    ExpMod(v, g, x);
    return StoreMinimal(v);
}

ClientResult RunClient(const std::string& username, const std::string& password,
                       const Bytes& salt, const Bytes& B, const Bytes& a) {
    const Mpi& n = GroupMpi();
    Mpi g, a_value, b_value, b_mod_n;
    Load(g, GroupG());
    Load(a_value, a);
    Load(b_value, B);
    Check(mbedtls_mpi_mod_mpi(&b_mod_n.value, &b_value.value, &n.value), "mod B");
    if (mbedtls_mpi_cmp_int(&b_mod_n.value, 0) == 0) throw std::invalid_argument("B mod N == 0");

    ClientResult result;
    result.a = a;
    Mpi A_value;
    ExpMod(A_value, g, a_value);
    result.A = Store(A_value, kGroupBytes);

    const Bytes padded_B = PadToGroup(B);
    result.u = Sha512(Concat({&result.A, &padded_B}));
    Mpi u;
    Load(u, result.u);
    if (mbedtls_mpi_cmp_int(&u.value, 0) == 0) throw std::invalid_argument("u == 0");

    Mpi x, k, gx, kgx, base, ux, exponent, S;
    Load(x, ComputeX(salt, username, password));
    Load(k, ComputeK());
    ExpMod(gx, g, x);
    Check(mbedtls_mpi_mul_mpi(&kgx.value, &k.value, &gx.value), "k*g^x");
    Check(mbedtls_mpi_mod_mpi(&kgx.value, &kgx.value, &n.value), "mod k*g^x");
    Check(mbedtls_mpi_sub_mpi(&base.value, &b_value.value, &kgx.value), "B-k*g^x");
    Check(mbedtls_mpi_mod_mpi(&base.value, &base.value, &n.value), "mod base");
    Check(mbedtls_mpi_mul_mpi(&ux.value, &u.value, &x.value), "u*x");
    Check(mbedtls_mpi_add_mpi(&exponent.value, &a_value.value, &ux.value), "a+u*x");
    ExpMod(S, base, exponent);
    result.S = StoreMinimal(S);
    result.K = Sha512(result.S);

    const Bytes hash_n = Sha512(GroupN());
    const Bytes hash_g = Sha512(PadToGroup(GroupG()));
    Bytes hash_n_xor_g(kHashBytes);
    for (size_t index = 0; index < kHashBytes; ++index) {
        hash_n_xor_g[index] = hash_n[index] ^ hash_g[index];
    }
    const Bytes hash_i = Sha512(FromString(username));
    result.M1 = Sha512(Concat({&hash_n_xor_g, &hash_i, &salt, &result.A, &B, &result.K}));
    result.M2 = Sha512(Concat({&result.A, &result.M1, &result.K}));
    return result;
}

bool ValidClientPublicKey(const Bytes& A) {
    if (A.size() != kGroupBytes) return false;
    Mpi value, remainder;
    Load(value, A);
    Check(mbedtls_mpi_mod_mpi(&remainder.value, &value.value, &GroupMpi().value), "mod A");
    return mbedtls_mpi_cmp_int(&remainder.value, 0) != 0;
}

struct EspServerSession {
    esp_srp_handle_t* handle = nullptr;
};

EspServer::EspServer(const Bytes& salt, const Bytes& verifier, const Bytes& b)
    : session_(new EspServerSession) {
    EnsurePsa();
    if (b.size() != kPrivateBytes) throw std::invalid_argument("b must be 32 bytes");
    session_->handle = esp_srp_init(ESP_NG_3072);
    if (session_->handle == nullptr ||
        esp_srp_set_salt_verifier(session_->handle, reinterpret_cast<const char*>(salt.data()),
                                  static_cast<int>(salt.size()),
                                  reinterpret_cast<const char*>(verifier.data()),
                                  static_cast<int>(verifier.size())) != ESP_OK) {
        esp_srp_free(session_->handle);
        delete session_;
        throw std::runtime_error("esp_srp setup failed");
    }
    lan_trust_setup_queue_random(b.data(), b.size());
    char* public_key = nullptr;
    int public_key_length = 0;
    const esp_err_t result = esp_srp_srv_pubkey_from_salt_verifier(
        session_->handle, &public_key, &public_key_length);
    if (result != ESP_OK || public_key == nullptr || lan_trust_setup_random_remaining() != 0) {
        esp_srp_free(session_->handle);
        delete session_;
        throw std::runtime_error("esp_srp public key failed");
    }
    B_.assign(public_key, public_key + public_key_length);
}

EspServer::~EspServer() {
    esp_srp_free(session_->handle);
    delete session_;
}

Bytes EspServer::SessionKey(const Bytes& A) {
    Bytes client_key = A;
    char* key = nullptr;
    uint16_t key_length = 0;
    if (esp_srp_get_session_key(session_->handle, reinterpret_cast<char*>(client_key.data()),
                                static_cast<int>(client_key.size()), &key, &key_length) != ESP_OK) {
        throw std::runtime_error("esp_srp session key failed");
    }
    return Bytes(key, key + key_length);
}

bool EspServer::ExchangeProofs(const std::string& username, const Bytes& M1, Bytes& M2) {
    if (M1.size() != kHashBytes) return false;
    std::string identity = username;
    Bytes proof = M1;
    M2.assign(kHashBytes, 0);
    return esp_srp_exchange_proofs(session_->handle, identity.data(),
                                   static_cast<uint16_t>(identity.size()),
                                   reinterpret_cast<char*>(proof.data()),
                                   reinterpret_cast<char*>(M2.data())) == ESP_OK;
}

}  // namespace lan_trust_setup

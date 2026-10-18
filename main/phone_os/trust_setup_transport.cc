#include "phone_os/trust_setup_transport.h"

#include <esp_http_client.h>
#include <esp_log.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <mutex>
#include <vector>

namespace rodakos {
namespace {

constexpr const char* TAG = "TrustSetupHttp";
constexpr int kTimeoutMs = 8000;
constexpr size_t kMaxResponseBytes = 8192;
// esp-tls 在调用 crt_bundle_attach 前把 authmode 设为 VERIFY_REQUIRED；mbedTLS 要求此时有
// CA 链或回调。给一个空链以满足这条检查，实际判定全部在 RecordPeerCallback 中完成。
const mbedtls_x509_crt kEmptyChain = {};

// 验证回调没有按请求的上下文参数，因此一次只允许一个首次接入请求，并经此槽位取回指纹。
std::mutex g_request_mutex;
std::array<uint8_t, 32> g_peer_spki{};
bool g_peer_spki_valid = false;

int RecordPeerCallback(void*, mbedtls_x509_crt* certificate, int depth, uint32_t* flags) {
    if (depth == 0 && certificate != nullptr && certificate->pk_raw.p != nullptr) {
        size_t length = 0;
        g_peer_spki_valid =
            psa_hash_compute(PSA_ALG_SHA_256, certificate->pk_raw.p, certificate->pk_raw.len,
                             g_peer_spki.data(), g_peer_spki.size(), &length) == PSA_SUCCESS &&
            length == g_peer_spki.size();
    }
    // 不验证证书链与名称：服务器身份由 SRP 之上的证书绑定 HMAC 认证
    *flags = 0;
    return 0;
}

esp_err_t AttachRecordOnlyVerifier(void* conf) {
    auto* ssl_conf = static_cast<mbedtls_ssl_config*>(conf);
    if (ssl_conf == nullptr) return ESP_ERR_INVALID_ARG;
    mbedtls_ssl_conf_ca_chain(ssl_conf, const_cast<mbedtls_x509_crt*>(&kEmptyChain), nullptr);
    mbedtls_ssl_conf_verify(ssl_conf, RecordPeerCallback, nullptr);
    return ESP_OK;
}

}  // namespace

bool TrustSetupHttpRequest(const std::string& base_url, const std::string& path,
                           const char* method, const std::string& body,
                           const std::string& bearer_token, TrustSetupHttpResult& result,
                           std::string& error) {
    result = {};
    if (base_url.rfind("https://", 0) != 0 || path.empty() || path[0] != '/') {
        error = "trust_setup_url_invalid";
        return false;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        error = "trust_setup_crypto_unavailable";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_request_mutex);
    g_peer_spki_valid = false;

    const std::string url = base_url + path;
    const bool post = std::string(method) == "POST";
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = post ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    config.timeout_ms = kTimeoutMs;
    config.buffer_size = 1024;
    config.buffer_size_tx = 1024;
    config.user_agent = "RodakOS/trust-setup";
    config.disable_auto_redirect = true;
    config.keep_alive_enable = false;
    // 用户选定的地址可能是数字 IP，证书名为 rodak-<id>.local；名称不参与此阶段的判定
    config.skip_cert_common_name_check = true;
    config.crt_bundle_attach = AttachRecordOnlyVerifier;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        error = "trust_setup_http_init_failed";
        return false;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    if (post) esp_http_client_set_header(client, "Content-Type", "application/json");
    std::string authorization;
    if (!bearer_token.empty()) {
        authorization = "Bearer " + bearer_token;
        esp_http_client_set_header(client, "Authorization", authorization.c_str());
    }

    bool ok = false;
    const esp_err_t opened = esp_http_client_open(client, post ? static_cast<int>(body.size()) : 0);
    if (opened != ESP_OK) {
        error = std::string("trust_setup_http_open_failed: ") + esp_err_to_name(opened);
    } else if (post && esp_http_client_write(client, body.data(), static_cast<int>(body.size())) !=
                           static_cast<int>(body.size())) {
        error = "trust_setup_http_write_failed";
    } else if (esp_http_client_fetch_headers(client) < 0) {
        error = "trust_setup_http_headers_failed";
    } else {
        std::vector<char> buffer(kMaxResponseBytes + 1, '\0');
        const int read = esp_http_client_read_response(client, buffer.data(),
                                                       static_cast<int>(kMaxResponseBytes));
        if (read < 0) {
            error = "trust_setup_http_read_failed";
        } else if (read == static_cast<int>(kMaxResponseBytes)) {
            error = "trust_setup_http_response_too_large";
        } else {
            result.status_code = esp_http_client_get_status_code(client);
            result.body.assign(buffer.data(), static_cast<size_t>(read));
            ok = true;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (ok && !g_peer_spki_valid) {
        ESP_LOGW(TAG, "TLS handshake completed without a recorded peer certificate");
        error = "trust_setup_peer_certificate_missing";
        ok = false;
    }
    if (ok) {
        result.peer_spki = g_peer_spki;
        result.peer_spki_valid = true;
    }
    return ok;
}

}  // namespace rodakos

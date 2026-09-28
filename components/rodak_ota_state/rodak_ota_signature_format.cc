#include "rodak_ota_signature.h"

#include <algorithm>
#include <cctype>

namespace rodakos {
namespace {

bool IsHex(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

bool IsField(const std::string& value, size_t limit) {
    return !value.empty() && value.size() <= limit &&
           std::none_of(value.begin(), value.end(), [](unsigned char ch) {
               return ch < 0x20 || ch == 0x7f;
           });
}

}  // namespace

std::string BuildOtaSignaturePayload(const OtaUpdateRecord& record) {
    if (!IsField(record.task_no, kOtaTaskNoMaxBytes) ||
        !IsField(record.target_version, kOtaVersionMaxBytes) ||
        record.pending_size == 0 || record.pending_size > 0xd50000 ||
        record.pending_sha256.size() != 64 ||
        !std::all_of(record.pending_sha256.begin(), record.pending_sha256.end(), IsHex)) {
        return {};
    }
    return std::string("rodakos-ota-v2\nrymcu-bigsmart\nota_0\nrsa2048-sha256\n") + record.task_no + "\n" + record.target_version +
           "\n" + std::to_string(record.pending_size) + "\n" + record.pending_sha256 + "\n";
}

bool IsValidOtaSignatureHex(const std::string& value) {
    return value.size() == 512 &&
           std::all_of(value.begin(), value.end(), [](char character) {
               return IsHex(character);
           });
}

}  // namespace rodakos

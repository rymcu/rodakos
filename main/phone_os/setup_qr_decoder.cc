#include "phone_os/setup_qr_decoder.h"

#include "phone_os/setup_qr_payload.h"

#include <quirc.h>

#include <cstring>
#include <new>

namespace rodakos {

SetupQrDecoder::SetupQrDecoder() = default;

SetupQrDecoder::~SetupQrDecoder() { Release(); }

void SetupQrDecoder::Release() {
    if (quirc_ != nullptr) quirc_destroy(quirc_);
    quirc_ = nullptr;
    code_.reset();
    data_.reset();
    width_ = 0;
    height_ = 0;
}

uint8_t* SetupQrDecoder::Begin(int width, int height) {
    if (width <= 0 || height <= 0) return nullptr;
    if (quirc_ == nullptr) {
        // Both structs are several KiB; keep them off the stack that quirc_end already needs.
        code_.reset(new (std::nothrow) quirc_code);
        data_.reset(new (std::nothrow) quirc_data);
        quirc_ = quirc_new();
        if (quirc_ == nullptr || code_ == nullptr || data_ == nullptr) {
            Release();
            return nullptr;
        }
    }
    if (width != width_ || height != height_) {
        if (quirc_resize(quirc_, width, height) < 0) {
            Release();
            return nullptr;
        }
        width_ = width;
        height_ = height;
    }
    return quirc_begin(quirc_, nullptr, nullptr);
}

bool SetupQrDecoder::Finish(std::string& payload) {
    quirc_end(quirc_);
    const int count = quirc_count(quirc_);
    bool found = false;
    for (int index = 0; index < count && !found; ++index) {
        quirc_extract(quirc_, index, code_.get());
        if (quirc_decode(code_.get(), data_.get()) != QUIRC_SUCCESS) {
            // quirc reads mirrored codes only after flipping the grid.
            quirc_flip(code_.get());
            if (quirc_decode(code_.get(), data_.get()) != QUIRC_SUCCESS) continue;
        }
        const size_t length = static_cast<size_t>(data_->payload_len);
        if (data_->payload_len <= 0 || length > kSetupQrMaxPayloadBytes) continue;
        payload.assign(reinterpret_cast<const char*>(data_->payload), length);
        found = true;
    }
    // The payload can hold a WiFi password; do not leave a copy in the reusable buffer.
    std::memset(data_.get(), 0, sizeof(*data_));
    return found;
}

bool SetupQrDecoder::DecodeGray(const uint8_t* gray, int width, int height, int stride,
                                std::string& payload) {
    payload.clear();
    if (gray == nullptr || stride < width) return false;
    uint8_t* image = Begin(width, height);
    if (image == nullptr) return false;
    for (int y = 0; y < height; ++y) {
        std::memcpy(image + static_cast<size_t>(y) * width,
                    gray + static_cast<size_t>(y) * stride, static_cast<size_t>(width));
    }
    return Finish(payload);
}

bool SetupQrDecoder::DecodeRgb565(const uint8_t* rgb565, int width, int height, int stride,
                                  std::string& payload) {
    payload.clear();
    if (rgb565 == nullptr || stride < width * 2) return false;
    uint8_t* image = Begin(width, height);
    if (image == nullptr) return false;
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = rgb565 + static_cast<size_t>(y) * stride;
        uint8_t* out = image + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            const uint16_t pixel = static_cast<uint16_t>(row[x * 2]) |
                                   static_cast<uint16_t>(row[x * 2 + 1] << 8);
            const unsigned r = (pixel >> 11) & 0x1f;
            const unsigned g = (pixel >> 5) & 0x3f;
            const unsigned b = pixel & 0x1f;
            // BT.601 luma with the 5/6/5-bit expansion folded into the weights: white maps to 255.
            out[x] = static_cast<uint8_t>((r * 631u + g * 609u + b * 240u) >> 8);
        }
    }
    return Finish(payload);
}

}  // namespace rodakos

#include "test_framework.h"

#include "phone_os/setup_qr_decoder.h"
#include "phone_os/setup_qr_payload.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using rodakos::SetupQrDecoder;

constexpr char kRodakPayload[] =
    "RODAK:V:1;S:HomeNet;T:WPA;P:s3cret-pass;U:https://ic.example.com;;";

struct GrayImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

GrayImage LoadPgm(const std::string& name) {
    const std::string path = std::string(RODAK_SETUP_QR_FIXTURES) + "/" + name;
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("missing fixture " + path);
    std::string magic;
    int max_value = 0;
    GrayImage image;
    file >> magic >> image.width >> image.height >> max_value;
    file.get();
    if (magic != "P5" || max_value != 255 || image.width <= 0 || image.height <= 0) {
        throw std::runtime_error("unsupported fixture " + path);
    }
    image.pixels.resize(static_cast<size_t>(image.width) * image.height);
    file.read(reinterpret_cast<char*>(image.pixels.data()),
              static_cast<std::streamsize>(image.pixels.size()));
    if (!file) throw std::runtime_error("truncated fixture " + path);
    return image;
}

// Little-endian RGB565, as CameraService stores frames, with `padding` spare bytes per row.
std::vector<uint8_t> ToRgb565(const GrayImage& image, int padding) {
    const int stride = image.width * 2 + padding;
    std::vector<uint8_t> frame(static_cast<size_t>(stride) * image.height, 0xA5);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const unsigned value = image.pixels[static_cast<size_t>(y) * image.width + x];
            const uint16_t pixel = static_cast<uint16_t>(((value >> 3) << 11) |
                                                         ((value >> 2) << 5) | (value >> 3));
            frame[static_cast<size_t>(y) * stride + x * 2] = static_cast<uint8_t>(pixel & 0xff);
            frame[static_cast<size_t>(y) * stride + x * 2 + 1] = static_cast<uint8_t>(pixel >> 8);
        }
    }
    return frame;
}

GrayImage Mirror(const GrayImage& image) {
    GrayImage mirrored = image;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            mirrored.pixels[static_cast<size_t>(y) * image.width + x] =
                image.pixels[static_cast<size_t>(y) * image.width + (image.width - 1 - x)];
        }
    }
    return mirrored;
}

}  // namespace

RODAK_TEST("Setup QR decoder reads the upstream quirc fixture") {
    const GrayImage image = LoadPgm("quirc_upstream_test_of_quirc.pgm");
    SetupQrDecoder decoder;
    std::string payload;
    RODAK_CHECK(decoder.DecodeGray(image.pixels.data(), image.width, image.height, image.width,
                                   payload));
    RODAK_CHECK_EQ(payload, std::string("test of quirc"));
}

RODAK_TEST("Setup QR decoder reads a RODAK payload that the parser accepts") {
    const GrayImage image = LoadPgm("rodak_payload_v5_scale4.pgm");
    SetupQrDecoder decoder;
    std::string payload;
    RODAK_CHECK(decoder.DecodeGray(image.pixels.data(), image.width, image.height, image.width,
                                   payload));
    RODAK_CHECK_EQ(payload, std::string(kRodakPayload));

    rodakos::SetupQrPayload parsed;
    RODAK_CHECK(rodakos::ParseSetupQr(payload, parsed) == rodakos::SetupQrError::kNone);
    RODAK_CHECK_EQ(parsed.bootstrap_url,
                   std::string("https://ic.example.com/api/v1/aiot/devices/bootstrap"));
}

RODAK_TEST("Setup QR decoder reads RGB565 camera frames with row padding") {
    const GrayImage image = LoadPgm("rodak_payload_320x240.pgm");
    for (const int padding : {0, 64}) {
        const std::vector<uint8_t> frame = ToRgb565(image, padding);
        SetupQrDecoder decoder;
        std::string payload;
        RODAK_CHECK(decoder.DecodeRgb565(frame.data(), image.width, image.height,
                                         image.width * 2 + padding, payload));
        RODAK_CHECK_EQ(payload, std::string(kRodakPayload));
    }
}

RODAK_TEST("Setup QR decoder reads a mirrored code") {
    const GrayImage image = Mirror(LoadPgm("rodak_payload_320x240.pgm"));
    SetupQrDecoder decoder;
    std::string payload;
    RODAK_CHECK(decoder.DecodeGray(image.pixels.data(), image.width, image.height, image.width,
                                   payload));
    RODAK_CHECK_EQ(payload, std::string(kRodakPayload));
}

RODAK_TEST("Setup QR decoder reuses buffers across frames and sizes") {
    const GrayImage small = LoadPgm("quirc_upstream_test_of_quirc.pgm");
    const GrayImage large = LoadPgm("rodak_payload_320x240.pgm");
    SetupQrDecoder decoder;
    std::string payload;
    for (int round = 0; round < 2; ++round) {
        RODAK_CHECK(decoder.DecodeGray(large.pixels.data(), large.width, large.height,
                                       large.width, payload));
        RODAK_CHECK_EQ(payload, std::string(kRodakPayload));
        RODAK_CHECK(decoder.DecodeGray(small.pixels.data(), small.width, small.height,
                                       small.width, payload));
        RODAK_CHECK_EQ(payload, std::string("test of quirc"));
    }
    decoder.Release();
    RODAK_CHECK(decoder.DecodeGray(large.pixels.data(), large.width, large.height, large.width,
                                   payload));
    RODAK_CHECK_EQ(payload, std::string(kRodakPayload));
}

RODAK_TEST("Setup QR decoder reports no code on a blank frame") {
    const std::vector<uint8_t> blank(320 * 240, 170);
    SetupQrDecoder decoder;
    std::string payload = "stale";
    RODAK_CHECK_FALSE(decoder.DecodeGray(blank.data(), 320, 240, 320, payload));
    RODAK_CHECK(payload.empty());
}

RODAK_TEST("Setup QR decoder rejects invalid frame geometry") {
    const std::vector<uint8_t> frame(64 * 64 * 2, 0);
    SetupQrDecoder decoder;
    std::string payload = "stale";
    RODAK_CHECK_FALSE(decoder.DecodeGray(nullptr, 64, 64, 64, payload));
    RODAK_CHECK_FALSE(decoder.DecodeGray(frame.data(), 0, 64, 64, payload));
    RODAK_CHECK_FALSE(decoder.DecodeGray(frame.data(), 64, -1, 64, payload));
    RODAK_CHECK_FALSE(decoder.DecodeGray(frame.data(), 64, 64, 63, payload));
    RODAK_CHECK_FALSE(decoder.DecodeRgb565(frame.data(), 64, 64, 127, payload));
    RODAK_CHECK(payload.empty());
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

struct quirc;
struct quirc_code;
struct quirc_data;

namespace rodakos {

// Wraps quirc for the setup-QR scan. Free of ESP-IDF headers so host tests can feed fixtures.
//
// quirc_end/quirc_decode use about 10 KiB of stack on Xtensa (upstream ESP-IDF test). Call Decode*
// from a task with at least 12 KiB of stack, never from the LVGL or camera callback task.
// Every quirc buffer comes from malloc, so with CONFIG_SPIRAM_USE_MALLOC it lands in PSRAM.
class SetupQrDecoder {
public:
    SetupQrDecoder();
    ~SetupQrDecoder();

    SetupQrDecoder(const SetupQrDecoder&) = delete;
    SetupQrDecoder& operator=(const SetupQrDecoder&) = delete;

    // Each frame converts little-endian RGB565 to 8-bit luma. `stride` is in bytes.
    // Returns true and sets `payload` to the first QR code that decodes. A frame with no
    // readable code returns false and is not an error; the caller tries the next frame.
    bool DecodeRgb565(const uint8_t* rgb565, int width, int height, int stride,
                      std::string& payload);
    bool DecodeGray(const uint8_t* gray, int width, int height, int stride, std::string& payload);

    // Where the decoded code sits in the image, for the camera calibration page.
    struct Location {
        // quirc corners: top-left, top-right, bottom-right, bottom-left of the code's own grid.
        int corner_x[4] = {};
        int corner_y[4] = {};
        // The code only decoded after quirc_flip, so the image is a mirror image of the code.
        bool mirrored = false;
    };
    // Same as DecodeGray, and also reports where the code was and whether it was mirrored.
    bool DecodeGray(const uint8_t* gray, int width, int height, int stride, std::string& payload,
                    Location& location);

    // Frees the quirc buffers; the next Decode* allocates them again.
    void Release();

private:
    uint8_t* Begin(int width, int height);
    bool Finish(std::string& payload, Location* location = nullptr);

    quirc* quirc_ = nullptr;
    std::unique_ptr<quirc_code> code_;
    std::unique_ptr<quirc_data> data_;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace rodakos

#pragma once

namespace rodakos {

// Place before the codec RAII owner, so close runs before this scope restores
// the task's allocator policy. Keep codec task_enable=false in this scope.
// Reviewed users: screen and Camera JPEG encoding, ImageLibrary JPEG decoding.
// tools/check_screen_jpeg_allocator.py rejects any other caller in the final ELF.
class ScreenJpegAllocationScope final {
public:
    ScreenJpegAllocationScope() noexcept;
    ~ScreenJpegAllocationScope() noexcept;
    ScreenJpegAllocationScope(const ScreenJpegAllocationScope&) = delete;
    ScreenJpegAllocationScope& operator=(const ScreenJpegAllocationScope&) = delete;
    ScreenJpegAllocationScope(ScreenJpegAllocationScope&&) = delete;
    ScreenJpegAllocationScope& operator=(ScreenJpegAllocationScope&&) = delete;

private:
    bool previous_;
};

}  // namespace rodakos

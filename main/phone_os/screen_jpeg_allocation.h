#pragma once

namespace rodakos {

// Place before the codec RAII owner, so close runs before this scope restores
// the task's allocator policy. Keep codec task_enable=false in this scope.
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

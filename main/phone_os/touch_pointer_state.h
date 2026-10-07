#pragma once

#include <cstdint>

#include <lvgl.h>

namespace rodakos {

class TouchPointerState {
public:
    // Returns true only when the previously delivered remote gesture was
    // cancelled. Call lv_indev_reset on the LVGL thread before processing data.
    bool Read(bool local_pressed, const lv_point_t& local_point,
              bool remote_pressed, const lv_point_t& remote_point,
              lv_indev_data_t& data, uint64_t remote_cancel_generation = 0) {
        // Touch polling publishes local_pressed before cancelling the remote
        // controller; source takeover must abort even with the previous epoch.
        const bool cancelled = remote_active_ &&
            (local_pressed || remote_cancel_generation != cancel_generation_);
        cancel_generation_ = remote_cancel_generation;
        if (cancelled) {
            data.point = point_;
            data.state = LV_INDEV_STATE_RELEASED;
            remote_active_ = false;
            return true;
        }
        if (local_pressed) {
            point_ = local_point;
        } else if (remote_pressed) {
            point_ = remote_point;
        }

        // LVGL 会用释放位置判断手势和点击阈值；输入源释放时保留最后
        // 一次有效坐标，不能回退到当前未按下输入源的空闲坐标。
        data.point = point_;
        data.state = local_pressed || remote_pressed
                         ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        remote_active_ = !local_pressed && remote_pressed;
        return cancelled;
    }

private:
    lv_point_t point_ = {0, 0};
    uint64_t cancel_generation_ = 0;
    bool remote_active_ = false;
};

}  // namespace rodakos

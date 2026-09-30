#pragma once

#include <lvgl.h>

namespace rodakos {

class TouchPointerState {
public:
    void Read(bool local_pressed, const lv_point_t& local_point,
              bool remote_pressed, const lv_point_t& remote_point,
              lv_indev_data_t& data) {
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
    }

private:
    lv_point_t point_ = {0, 0};
};

}  // namespace rodakos

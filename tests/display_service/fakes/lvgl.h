#pragma once
#include <cstdint>
using lv_event_code_t = int;
constexpr int LV_EVENT_ALL = 0, LV_EVENT_FLUSH_START = 1, LV_EVENT_FLUSH_FINISH = 2;
constexpr int LV_RESULT_OK = 0, LV_RESULT_INVALID = 1;
struct lv_area_t { int x1 = 0, y1 = 0, x2 = 0, y2 = 0; };
struct lv_draw_buf_t { struct { unsigned stride = 0; } header; uint8_t* data = nullptr; };
struct lv_obj_t {};
struct lv_event_t;
using lv_event_cb_t = void (*)(lv_event_t*);
struct lv_display_t {
    int width = 320, height = 240;
    lv_event_cb_t callback = nullptr;
    void* user_data = nullptr;
    lv_draw_buf_t draw_buf;
    bool flush_last = true;
    lv_obj_t object;
};
struct lv_event_t { lv_event_code_t code; void* user_data; lv_display_t* target; void* param; };
inline void* lv_event_get_user_data(lv_event_t* e) { return e->user_data; }
inline void* lv_event_get_current_target(lv_event_t* e) { return e->target; }
inline void* lv_event_get_param(lv_event_t* e) { return e->param; }
inline lv_event_code_t lv_event_get_code(lv_event_t* e) { return e->code; }
inline int lv_display_get_horizontal_resolution(lv_display_t* d) { return d->width; }
inline int lv_display_get_vertical_resolution(lv_display_t* d) { return d->height; }
void lv_display_add_event_cb(lv_display_t* d, lv_event_cb_t cb, int, void* data);
void lv_display_remove_event_cb_with_user_data(lv_display_t* d, lv_event_cb_t cb, void* data);
inline bool lv_display_flush_is_last(lv_display_t* d) { return d->flush_last; }
inline lv_draw_buf_t* lv_display_get_buf_active(lv_display_t* d) { return &d->draw_buf; }
inline int lv_area_get_width(lv_area_t* a) { return a->x2 - a->x1 + 1; }
inline int lv_area_get_height(lv_area_t* a) { return a->y2 - a->y1 + 1; }
inline lv_obj_t* lv_display_get_screen_active(lv_display_t* d) { return &d->object; }
inline lv_obj_t* lv_display_get_layer_bottom(lv_display_t* d) { return &d->object; }
inline lv_obj_t* lv_display_get_layer_top(lv_display_t* d) { return &d->object; }
inline lv_obj_t* lv_display_get_layer_sys(lv_display_t* d) { return &d->object; }
inline void lv_obj_invalidate(lv_obj_t*) {}
inline void lv_refr_now(lv_display_t*) {}
int lv_async_call(void (*callback)(void*), void* data);
void lv_async_call_cancel(void (*callback)(void*), void* data);

#include "host_heap.h"

// Home 用真实 LVGL 验证捕获镜像；分配策略与 codec 生命周期由专用服务目标验证。
void NoteRealCall(size_t, size_t, size_t, int) {}

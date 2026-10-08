#pragma once
#define EXTRA_FUNC_IMPLEMENT(name, extra_func)
static inline int esp_board_extra_func_get(const char* type, void** function) { (void)type; if (function) *function = 0; return -1; }

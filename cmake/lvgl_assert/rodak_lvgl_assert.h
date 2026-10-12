#pragma once

#include <assert.h>

/* lv_assert.h includes this header after lv_conf_internal.h set the default handler, which is
 * `while(1);`. That loop freezes the UI task and leaves the device without a display until a
 * power cycle. Trap instead, so the panic handler prints a backtrace and the device reboots. */
#undef LV_ASSERT_HANDLER
#define LV_ASSERT_HANDLER __builtin_trap();

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RODAK_CAMERA_TEARDOWN_MAGIC UINT32_C(0x43544447)
#define RODAK_CAMERA_TEARDOWN_VERSION UINT32_C(1)
#define RODAK_CAMERA_TEARDOWN_CAPACITY UINT32_C(32)

#define RODAK_CAMERA_TEARDOWN_IOCTL_ENTER UINT32_C(1)
#define RODAK_CAMERA_TEARDOWN_IOCTL_RETURNED UINT32_C(2)
#define RODAK_CAMERA_TEARDOWN_BEFORE_LOG UINT32_C(3)
#define RODAK_CAMERA_TEARDOWN_AFTER_LOG UINT32_C(4)
#define RODAK_CAMERA_TEARDOWN_SENSOR_ENTER UINT32_C(5)
#define RODAK_CAMERA_TEARDOWN_SENSOR_RETURNED UINT32_C(6)
#define RODAK_CAMERA_TEARDOWN_STOP_ENTER UINT32_C(7)
#define RODAK_CAMERA_TEARDOWN_STOP_RETURNED UINT32_C(8)
#define RODAK_CAMERA_TEARDOWN_DISABLE_ENTER UINT32_C(9)
#define RODAK_CAMERA_TEARDOWN_DISABLE_RETURNED UINT32_C(10)
#define RODAK_CAMERA_TEARDOWN_DEL_ENTER UINT32_C(11)
#define RODAK_CAMERA_TEARDOWN_DEL_RETURNED UINT32_C(12)
#define RODAK_CAMERA_TEARDOWN_DVP_TASK_DELETE_ENTER UINT32_C(13)
#define RODAK_CAMERA_TEARDOWN_DVP_TASK_DELETE_RETURNED UINT32_C(14)
#define RODAK_CAMERA_TEARDOWN_DVP_GPIO_DISABLE_ENTER UINT32_C(15)
#define RODAK_CAMERA_TEARDOWN_DVP_GPIO_DISABLE_RETURNED UINT32_C(16)
#define RODAK_CAMERA_TEARDOWN_DVP_CAPTURE_STOP_ENTER UINT32_C(17)
#define RODAK_CAMERA_TEARDOWN_DVP_CAPTURE_STOP_RETURNED UINT32_C(18)
#define RODAK_CAMERA_TEARDOWN_DVP_GPIO_REMOVE_ENTER UINT32_C(19)
#define RODAK_CAMERA_TEARDOWN_DVP_GPIO_REMOVE_RETURNED UINT32_C(20)
#define RODAK_CAMERA_TEARDOWN_DVP_GDMA_DISCONNECT_ENTER UINT32_C(21)
#define RODAK_CAMERA_TEARDOWN_DVP_GDMA_DISCONNECT_RETURNED UINT32_C(22)
#define RODAK_CAMERA_TEARDOWN_DVP_GDMA_DELETE_ENTER UINT32_C(23)
#define RODAK_CAMERA_TEARDOWN_DVP_GDMA_DELETE_RETURNED UINT32_C(24)

#define RODAK_CAMERA_TEARDOWN_SNAPSHOT_PENDING UINT32_C(1)
#define RODAK_CAMERA_TEARDOWN_SNAPSHOT_CHANGED UINT32_C(2)
#define RODAK_CAMERA_TEARDOWN_SNAPSHOT_DROPPED UINT32_C(4)
#define RODAK_CAMERA_TEARDOWN_SNAPSHOT_INVALID UINT32_C(8)

typedef struct RodakCameraTeardownRecord {
    uint32_t phase;
    uint32_t core;
    int32_t status;
    uint32_t commit_seq;
} RodakCameraTeardownRecord;

typedef struct RodakCameraTeardownDiagnostics {
    uint32_t magic;
    uint32_t version;
    uint32_t capacity;
    uint32_t next_index;
    uint32_t contention_drop_seen;
    uint32_t full_drop_seen;
    RodakCameraTeardownRecord records[RODAK_CAMERA_TEARDOWN_CAPACITY];
} RodakCameraTeardownDiagnostics;

typedef struct RodakCameraTeardownSnapshot {
    uint32_t claimed_begin;
    uint32_t claimed_end;
    uint32_t committed_records;
    uint32_t pending_records;
    uint32_t contention_drop_seen;
    uint32_t full_drop_seen;
    RodakCameraTeardownRecord records[RODAK_CAMERA_TEARDOWN_CAPACITY];
} RodakCameraTeardownSnapshot;

// Debugger ABI: inspect this symbol with the cores halted. Firmware readers must
// use snapshot, not ordinary concurrent reads of the control words or payloads.
// Storage is boot-local internal DRAM; never reset or reuse a claimed slot.
extern RodakCameraTeardownDiagnostics rodak_camera_teardown_diagnostics;

// One strong CAS only: return the claimed sequence (1..32), or zero on a drop.
// The caller supplies the current core and the original signed return code;
// entry/void-return markers use status 0. Sequence is claim, not completion order.
uint32_t rodak_camera_teardown_record(uint32_t phase, uint32_t core, int32_t status);

// One bounded scan, never waits for a writer. Only release-published slots are
// copied; pending and beyond-boundary slots have all-zero payloads. The flags can
// be combined. Even an unchanged snapshot is not an atomic global-time sample.
// The destination must be caller-owned and may not alias the diagnostics object.
uint32_t rodak_camera_teardown_snapshot(RodakCameraTeardownSnapshot* snapshot);

#ifdef __cplusplus
}
#endif

#include "camera-teardown-diagnostics.h"

#include <stddef.h>

_Static_assert(sizeof(RodakCameraTeardownRecord) == 16, "C record ABI");
_Static_assert(offsetof(RodakCameraTeardownDiagnostics, records) == 24, "C header ABI");
_Static_assert(sizeof(RodakCameraTeardownDiagnostics) == 536, "C object ABI");

int rodak_test_camera_teardown_c_api(void) {
    RodakCameraTeardownSnapshot snapshot;
    if (rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_IOCTL_RETURNED, 1, -1) != 1) {
        return 1;
    }
    if (rodak_camera_teardown_snapshot(&snapshot) != 0) {
        return 2;
    }
    return snapshot.committed_records == 1 && snapshot.records[0].status == -1 &&
        snapshot.records[0].core == 1 && snapshot.records[0].commit_seq == 1 ? 0 : 3;
}

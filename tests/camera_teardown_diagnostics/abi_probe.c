#include "camera-teardown-diagnostics.h"

#include <stddef.h>
#include <stdio.h>

int main(void) {
    const RodakCameraTeardownDiagnostics* diagnostics = &rodak_camera_teardown_diagnostics;
    if (sizeof(*diagnostics) != 536 || sizeof(RodakCameraTeardownRecord) != 16 ||
        diagnostics->magic != RODAK_CAMERA_TEARDOWN_MAGIC ||
        diagnostics->version != RODAK_CAMERA_TEARDOWN_VERSION ||
        diagnostics->capacity != RODAK_CAMERA_TEARDOWN_CAPACITY) {
        return 1;
    }
    printf("{\n"
           "  \"symbol\": \"rodak_camera_teardown_diagnostics\",\n"
           "  \"magic\": %u, \"version\": %u, \"capacity\": %u,\n"
           "  \"size\": %zu, \"record_size\": %zu,\n"
           "  \"offsets\": {\"magic\": %zu, \"version\": %zu, \"capacity\": %zu, "
           "\"next_index\": %zu, \"contention_drop_seen\": %zu, \"full_drop_seen\": %zu, "
           "\"records\": %zu},\n"
           "  \"record_offsets\": {\"phase\": %zu, \"core\": %zu, \"status\": %zu, "
           "\"commit_seq\": %zu}\n}\n",
           diagnostics->magic, diagnostics->version, diagnostics->capacity,
           sizeof(*diagnostics), sizeof(RodakCameraTeardownRecord),
           offsetof(RodakCameraTeardownDiagnostics, magic),
           offsetof(RodakCameraTeardownDiagnostics, version),
           offsetof(RodakCameraTeardownDiagnostics, capacity),
           offsetof(RodakCameraTeardownDiagnostics, next_index),
           offsetof(RodakCameraTeardownDiagnostics, contention_drop_seen),
           offsetof(RodakCameraTeardownDiagnostics, full_drop_seen),
           offsetof(RodakCameraTeardownDiagnostics, records),
           offsetof(RodakCameraTeardownRecord, phase),
           offsetof(RodakCameraTeardownRecord, core),
           offsetof(RodakCameraTeardownRecord, status),
           offsetof(RodakCameraTeardownRecord, commit_seq));
    return 0;
}

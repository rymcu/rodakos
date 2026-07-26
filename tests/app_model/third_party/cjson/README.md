# cJSON host-test copy

These files are copied from the resolved `espressif/cjson` managed component used by the ESP-IDF
6.0.2 project baseline. The exact package revision is recorded in `dependencies.lock`; after
dependency resolution its sources live under `managed_components/espressif__cjson/cJSON`. They are
compiled only by the host app-model tests so the tests use the same patched parser as the firmware
without requiring an active ESP-IDF environment.

When the resolved managed component changes, refresh `cJSON.c`, `cJSON.h`, and `LICENSE` together
from that component revision.

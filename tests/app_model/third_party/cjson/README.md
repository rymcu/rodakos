# cJSON host-test copy

These files are cJSON 1.7.19, matching the ESP-IDF 6.0.2 project baseline.
(`components/json/cJSON`). They are compiled only by the host app-model tests so the tests use the
same parser version as the firmware without requiring an ESP-IDF checkout on the host.

When the firmware baseline changes its cJSON version, refresh `cJSON.c`, `cJSON.h`, and `LICENSE`
together from that ESP-IDF release.

# esp_codec_dev volume and I2S failure corrections

The `1.5.7/set_out_vol.c` and `1.5.7/open.c` functions are derived from Espressif's
`esp_codec_dev.c`; `set_fs.c` derives from `platform/audio_codec_data_i2s.c`.
`i2s_format_fault.c` adds a port fault latch for failed DMA reconfiguration:
SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD;
SPDX-License-Identifier: Apache-2.0.

The original file's copyright/license header is retained in every generated full source copy.
`1.5.7/provenance.json` records the registry package, upstream repository commit and normalized
source hashes. The copied [LICENSE](LICENSE) supplies the upstream Apache-2.0 license.

The port latch deliberately survives codec/data-interface recreation and clears only on a
system restart. This prevents IDF 6.0.2's failed-allocation state from being enabled on a later
same-format attempt; it does not automatically rebuild channels or recover audio hardware.

See [dependency maintenance](../../docs/dependency-maintenance.md) for generation, validation,
upgrade/removal rules and the limits of the repair.

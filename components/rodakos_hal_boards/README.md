# RodakOS HAL Boards

[中文说明](README_CN.md)

RodakOS owns the board definitions in this component. It currently supports only
the ESP32-S3 RYMCU BigSmart. Board Manager remains responsible for generated
configuration tables, device registration and peripheral lifetime management.

```text
boards/
└── rymcu/
    └── rymcu_bigsmart/
        ├── board_info.yaml
        ├── board_devices.yaml
        ├── board_peripherals.yaml
        ├── sdkconfig.defaults.board
        ├── setup_device.c
        └── components/esp_io_expander_pca9557/
```

The component compiles BigSmart's `setup_device.c`. `gen_bmgr_codes` compiles the
generated tables and depends on this component; it must not compile the setup
file a second time. Keep the PCA9557 driver with the board until another board
needs to share it.

From the repository root, activate ESP-IDF 6.0.2 and run
`./generate_board_config.ps1`, then `idf.py build`. Generated configuration is
gitignored and must be regenerated after moving or changing board definitions.

Future board definitions belong under `boards/<vendor>/<board>/`. Adding a
directory alone does not enable a board: update explicit board selection in
this component's CMake, manifest and the project generation entry, as well as
setup-source ownership in Board Manager's generator. Validate the board's
configuration and hardware before claiming support. Current build entry points
continue to accept only `rymcu_bigsmart`.

## Source and license

This component was extracted from ESP-Brookesia's `hal/brookesia_hal_boards`
0.7.5, whose vendored metadata records upstream commit
`02d1db90d4cdb232850c50c3b179fefccfaba420`. The BigSmart definitions also contain
RodakOS adaptations. It is now maintained in RodakOS; the upstream package's
checksums and component metadata no longer describe this reduced component.

The migrated sources retain their copyright and SPDX notices. See
[license.txt](license.txt) for Apache-2.0 terms. The separate `esp_board_manager`
component retains its own license and provenance.

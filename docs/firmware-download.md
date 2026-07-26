# RodakOS Firmware Build And Flash

This guide covers building RodakOS, flashing it to an ESP32-S3 RYMCU BigSmart, and checking serial logs.

## Environment

- Project path: `D:\workspace\rodakos`
- Target: `esp32s3`
- Flash: 16MB
- Default port: `COM3`
- Current baseline: ESP-IDF 6.0.2 with its recommended Xtensa GCC toolchain

Activate the project-local script once per PowerShell session:

```powershell
. .\activate_idf.ps1                 # prefer the installed 6.0.2 baseline
. .\activate_idf.ps1 -Version v6.0.2 # pin the baseline explicitly
. .\activate_idf.ps1 -List           # show what's installed
```

The activator scans `C:\esp\<vX.Y.Z>\esp-idf\`, reads `C:\Espressif\tools\eim_idf.json`, and falls back to `$env:IDF_PATH`. It selects tool versions from the chosen IDF's `tools/tools.json`. After activation, `idf.py` is on PATH and `$env:IDF_PATH` is set, so the project scripts (`build_rodakos.ps1`, `flash_and_test.ps1`, etc.) work directly without the official Microsoft `*PowerShell_profile.ps1`.

Verify the environment:

```powershell
echo $env:IDF_PATH
idf.py --version
```

## First Build Or Board Regeneration

```powershell
cd D:\workspace\rodakos
. .\activate_idf.ps1
.\build_rodakos.ps1
```

The script:

1. Verifies the ESP-IDF 6.0.2 environment and required files.
2. Clears the main build directory.
3. Calls `generate_board_config.ps1` for cold bootstrap, path normalization, and reconfiguration.
4. Verifies the generated component and builds the project.

Manual equivalent:

```powershell
cd D:\workspace\rodakos
. .\activate_idf.ps1
.\generate_board_config.ps1
idf.py build
```

## Daily Build

For ordinary app or service changes:

```powershell
cd D:\workspace\rodakos
. .\activate_idf.ps1
idf.py build
```

This builds the main `ota_0` application. It is not a safe first-flash command for the Recovery
partition layout.

## Recovery And OTA Package

Build both images and create the handoff package with:

```powershell
. .\activate_idf.ps1 -Version v6.0.2
.\build_ota_bundle.ps1
```

The packaging script always regenerates the Board Manager component before building so a stale,
gitignored IDF 5 artifact cannot enter a release package.

Use the generated merged image for the first wired migration. Normal Rodak OTA releases upload only
the generated `rodakos.bin`, never the merged flash image.

Root `idf.py flash` and `idf.py app-flash` are unsafe for this layout because they select the small
factory partition. See [Rodak MQTT and SD Recovery OTA](mqtt-ota-sd-recovery.md).

For a quick incremental build:

```powershell
.\quick_build.ps1
```

The former `quick_build.ps1 -Flash` path is intentionally disabled because ESP-IDF selects the
factory partition for this project layout.

## Flash And Monitor

Before changing device state, verify whether the installed partition table and immutable Recovery
match the latest package:

```powershell
.\flash_and_test.ps1 -Port COM3 -VerifyOnly
```

This mode reads and hashes both regions, writes nothing, resets the device, and exits. A mismatch
requires the explicit first-migration `-Erase` path below.

```powershell
cd D:\workspace\rodakos
. .\activate_idf.ps1
.\flash_and_test.ps1
```

The default path is for a device that already has the current Recovery layout. It reads back and
verifies the installed partition table and immutable Recovery, then writes only the packaged
`ota_data_initial.bin` at `0xF000` and `rodakos.bin` at `0x2A0000`. Default NVS, the isolated OTA
journal, Recovery, and coredump are not touched.

For the first Recovery-layout migration, explicitly erase and write the complete 16 MiB image:

```powershell
.\flash_and_test.ps1 -Port COM3 -Erase -NoMonitor
```

Both paths flash with `--after no-reset`, open one serial connection before releasing reset, and
validate the Recovery-to-main handoff. The script starts the interactive monitor only after all boot
health markers and the local OTA confirmation marker are present. A failure or timeout exits
nonzero without causing a second reset.

For automated validation without an interactive monitor:

```powershell
.\flash_and_test.ps1 -Port COM3 -NoMonitor
```

`-CaptureSeconds` changes the first-boot timeout; its default is 45 seconds.

Choose another port:

```powershell
.\flash_and_test.ps1 -Port COM5
```

After a verified boot, attach the ESP-IDF monitor without resetting the device:

```powershell
idf.py -p COM3 monitor --no-reset
```

Exit monitor with:

```text
Ctrl+]
```

## Expected Boot Signals

Healthy boot logs should include messages like:

```text
RodakOS: Starting RodakOS with Board Manager HAL
Board manager initialized
Touch input registered with cached polling
LVGL port initialized
BacklightAdapter: Backlight adapter initialized
Audio services ready - focus, assistant, and playback open codec on demand
Web file system ready - start from Settings when needed
Camera service ready - camera opens on demand
PhoneSystem: Starting Phone OS
HomeApp: Phone desktop ready with N apps
OtaUpdate: Local boot confirmation complete
RodakOS: RodakOS started successfully
```

`N` depends on which apps are registered in `main/apps/built_in_apps.cc`.

## Firmware Artifacts

After a successful build:

- `build\bootloader\bootloader.bin`
- `build\partition_table\partition-table.bin`
- `build\rodakos.bin`
- `build\rodakos.elf`

Current observed IDF 6 `build\rodakos.bin` size is about 3.30 MiB. The main `ota_0` partition is
13.3125 MiB; the independently built Recovery must fit its 2.5 MiB factory partition.

## Direct Esptool Flash

For the legacy single-image layout, `idf.py -p COM3 flash` was sufficient. The first Recovery-layout
migration uses the merged package generated above:

```powershell
python -m esptool --chip esp32s3 -p COM3 -b 460800 `
  --before default-reset --after hard-reset write-flash `
  0x0 build\packages\ota\<timestamp>\rodakos_sd_recovery_merged.bin
```

On an already migrated device, the equivalent settings-preserving refresh writes only the OTA
selector and main slot after verifying the installed partition table and Recovery. Prefer
`flash_and_test.ps1` because it performs those checks and captures the complete first boot.

## Resetting Device State

To clear NVS settings, WiFi credentials, and OTA state, erase and restore the complete merged
image:

```powershell
.\flash_and_test.ps1 -Port COM3 -Erase
```

Use this only when you intentionally want to reset device state.

## Common Problems

See [TROUBLESHOOTING.md](../TROUBLESHOOTING.md) for Board Manager recovery, toolchain checks,
partition errors, touch polling notes, SD card errors, and runtime diagnostics.

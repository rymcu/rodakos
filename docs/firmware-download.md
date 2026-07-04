# RodakOS Firmware Build And Flash

This guide covers building RodakOS, flashing it to an ESP32-S3 RYMCU BigSmart, and checking serial logs.

## Environment

- Project path: `D:\workspace\rodakos`
- Target: `esp32s3`
- Flash: 16MB
- Default port: `COM3`
- Current local toolchain config: GCC on ESP-IDF 5.5.4

Activate the project-local script once per PowerShell session:

```powershell
. .\activate_idf.ps1                 # auto-detect installed ESP-IDF
. .\activate_idf.ps1 -Version v5.4.2 # pin a specific version
. .\activate_idf.ps1 -List           # show what's installed
```

The activator scans `C:\esp\<vX.Y.Z>\esp-idf\`, reads `C:\Espressif\tools\eim_idf.json`, and falls back to `$env:IDF_PATH`. After activation, `idf.py` is on PATH and `$env:IDF_PATH` is set, so the project scripts (`build_rodakos.ps1`, `flash_and_test.ps1`, etc.) work directly without the official Microsoft `*PowerShell_profile.ps1`.

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

1. Checks required files such as `partitions_16m.csv`.
2. Runs `idf.py bmgr -b rymcu_bigsmart`.
3. Runs the generated-path fix.
4. Reconfigures and builds the project.

Manual equivalent:

```powershell
cd D:\workspace\rodakos
. .\activate_idf.ps1
idf.py set-target esp32s3
idf.py bmgr -b rymcu_bigsmart
.\fix_gen_paths.ps1
idf.py build
```

## Daily Build

For ordinary app or service changes:

```powershell
cd D:\workspace\rodakos
. .\activate_idf.ps1
idf.py build
```

For a quick build with optional flash:

```powershell
.\quick_build.ps1
.\quick_build.ps1 -Flash -Port COM3
```

## Flash And Monitor

```powershell
cd D:\workspace\rodakos
. .\activate_idf.ps1
.\flash_and_test.ps1
```

Choose another port:

```powershell
.\flash_and_test.ps1 -Port COM5
```

Manual commands:

```powershell
idf.py -p COM3 flash
idf.py -p COM3 monitor
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
RodakOS: RodakOS started successfully
```

`N` depends on which apps are registered in `main/apps/built_in_apps.cc`.

## Firmware Artifacts

After a successful build:

- `build\bootloader\bootloader.bin`
- `build\partition_table\partition-table.bin`
- `build\rodakos.bin`
- `build\rodakos.elf`

Current observed `build\rodakos.bin` size is about 3.7MB. The factory partition is 8MB.

## Direct Esptool Flash

Prefer `idf.py -p COM3 flash`. If a production script needs raw offsets, use the generated `build\flasher_args.json`. Current offsets are:

```powershell
esptool.py --chip esp32s3 -p COM3 --before default_reset --after hard_reset write_flash `
  --flash_mode dio --flash_freq 80m --flash_size 16MB `
  0x0 build\bootloader\bootloader.bin `
  0x8000 build\partition_table\partition-table.bin `
  0x10000 build\rodakos.bin
```

## Resetting Device State

To clear NVS settings, WiFi credentials, and internal data:

```powershell
idf.py -p COM3 erase-flash
idf.py -p COM3 flash monitor
```

Use this only when you intentionally want to reset device state.

## Common Problems

See [TROUBLESHOOTING.md](../TROUBLESHOOTING.md) for Board Manager path fixes, Clang response-file issues, partition errors, touch polling notes, SD card errors, and runtime diagnostics.

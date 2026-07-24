# RodakOS Troubleshooting

This file is the single place for current build, flash, and runtime fixes. Old migration notes, quick-fix cards, and touch/Clang one-off reports have been merged here.

## ESP-IDF Environment Missing

Symptom:

```text
idf.py: command not found
```

Fix:

Use the project-local activator from the repository root, then verify the session:

```powershell
cd D:\workspace\rodakos
. .\activate_idf.ps1
echo $env:IDF_PATH
idf.py --version
```

If more than one ESP-IDF version is installed, list and select one explicitly:

```powershell
. .\activate_idf.ps1 -List
. .\activate_idf.ps1 -Version v5.5.4
```

## Board "rymcu_bigsmart" Not Found

Symptom:

```text
Board "rymcu_bigsmart" not found
```

Cause: `brookesia_hal_boards` keeps BigSmart under `boards/rymcu/rymcu_bigsmart`, while Board Manager may scan `components/esp_board_manager/boards/` directly.

Fix:

```powershell
.\setup_board.ps1
idf.py bmgr -b rymcu_bigsmart
.\fix_gen_paths.ps1
idf.py build
```

Manual junction:

```powershell
cd D:\workspace\rodakos\components\esp_board_manager\boards
New-Item -ItemType Junction -Path "rymcu_bigsmart" -Target "..\..\brookesia_hal_boards\boards\rymcu\rymcu_bigsmart"
```

## Generated Paths Point To managed_components

Symptoms include `override_path` errors or CMake failing to find `setup_device.c`.

Cause: `idf.py bmgr` can generate paths that refer to the old `managed_components` location.

Fix:

```powershell
idf.py bmgr -b rymcu_bigsmart
.\fix_gen_paths.ps1
idf.py build
```

Check these files if needed:

- `components/gen_bmgr_codes/idf_component.yml`
- `components/gen_bmgr_codes/CMakeLists.txt`

They should point to `../../components/brookesia_hal_boards`, not `managed_components`.

## Missing partitions_16m.csv

Symptom:

```text
FileNotFoundError: partitions_16m.csv
```

Fix: keep `partitions_16m.csv` in the project root. The current 16MB layout is:

```csv
# Name,    Type, SubType, Offset,   Size,     Flags
nvs,       data, nvs,     0x9000,   0x6000,
otadata,   data, ota,     0xf000,   0x2000,
phy_init,  data, phy,     0x11000,  0x1000,
ota_state, data, nvs,     0x12000,  0x6000,
recovery,  app,  factory, 0x20000,  0x280000,
app,       app,  ota_0,   0x2a0000, 0xd50000,
coredump,  data, coredump,0xff0000, 0x10000,
```

## Partition Table Exceeds 16MB

Symptom:

```text
Partitions table occupies ... which does not fit in configured flash size 16MB
```

Fix: use exact hexadecimal sizes, not shorthand `1M`/`15M`. The current Recovery partition is
2.5 MiB and the main `ota_0` partition is 13.3125 MiB.

## Recovery Rejects The Main Image Or The Screen Stays Blank

Symptoms include:

```text
RodakRecovery: Bootloader rejected the main image; refusing an automatic retry
PhoneAppRegistry: App identity 'voice' conflicts between 'recorder' and 'assistant'
PhoneSystem: App registry validation failed
RodakOS: PhoneSystem start failed
```

An `ESP_OTA_IMG_ABORTED` entry means the main image reached its first
`PENDING_VERIFY` boot but reset again before `OtaUpdateService::ConfirmRunningImage()` completed.
It does not by itself prove that the bytes in `ota_0` are corrupt. A blank screen can also mean LVGL
started but Registry, Shell, or Home initialization failed before the desktop was created.

When Registry validation fails, inspect the preceding `PhoneAppRegistry` message. App IDs, titles,
and aliases are normalized and checked globally; a collision prevents Home from being created. The
Recorder/Assistant blank-screen regression was caused by both descriptors claiming the `voice`
alias. Recorder now uses recording-specific aliases and Assistant retains `voice`. This is a
compile-time descriptor conflict, so erasing NVS does not fix it; rebuild and flash corrected
firmware.

Build the latest package and use the guarded refresh flow on a device that already has the Recovery
layout:

```powershell
. .\activate_idf.ps1 -Version v5.5.4
.\build_ota_bundle.ps1
.\flash_and_test.ps1 -Port COM3 -NoMonitor
```

The default refresh first verifies the installed partition table and Recovery, then updates only
`otadata` and `ota_0`, preserving NVS and the isolated OTA journal. Use `-Erase` only for the first
Recovery-layout migration or when clearing all device state is intentional. The script captures the
first boot without a log gap, requires the OTA confirmation marker, and will not open a monitor after
a failed or incomplete boot. Do not use a default `idf.py monitor` while an image may still be
pending verification; it resets the target on startup. Use `idf.py -p COM3 monitor --no-reset` only
after a healthy boot.

When diagnosing an existing aborted image, read back and hash the installed image before rewriting
it. If it matches the package, restore only the package's `ota_data_initial.bin`, then capture the
entire Recovery-to-main sequence. Preserve the first failure log; a later reset replaces the useful
startup error with the generic Recovery rejection.

## Bootloader Clang Response-File Error On Windows

Symptom:

```text
clang: error: no such file or directory: '@D:/workspace/rodakos/build/bootloader/toolchain/cflags'
```

Cause: ESP-IDF 5.5.4 with Clang on Windows can mishandle response files.

Preferred fixes:

1. Use GCC. Current `sdkconfig` sets:

```ini
CONFIG_IDF_TOOLCHAIN="gcc"
CONFIG_IDF_TOOLCHAIN_GCC=y
```

2. Clean and regenerate:

```powershell
idf.py fullclean
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force components/gen_bmgr_codes -ErrorAction SilentlyContinue
idf.py bmgr -b rymcu_bigsmart
.\fix_gen_paths.ps1
idf.py build
```

3. If command-line builds remain broken, use the VSCode ESP-IDF extension.

## LCD Config Field Errors

Symptom:

```text
dev_display_lcd_config_t has no member named x_max
dev_display_lcd_config_t has no member named y_max
```

Fix: use `lcd_width` and `lcd_height` from `dev_display_lcd_config_t`.

```cpp
static PhoneUi ui(lcd_cfg->lcd_width, lcd_cfg->lcd_height);
```

## Backlight LEDC API Errors

Symptom: missing `dev_ledc_ctrl_set_brightness_percent` or invalid cast errors.

Cause: the esp-brookesia LEDC device layer exposes handles; RodakOS controls brightness through ESP-IDF LEDC APIs.

Pattern:

```cpp
auto handle = static_cast<periph_ledc_handle_t*>(ledc_handle);
uint32_t duty = (brightness * 8191) / 100;
ledc_set_duty(handle->speed_mode, handle->channel, duty);
ledc_update_duty(handle->speed_mode, handle->channel);
```

## LVGL Lock Or Blank Screen

Checks:

- `esp_board_manager_init()` must run before display handle lookup.
- `lvgl_port_init()` and `lvgl_port_add_disp()` must run before `PhoneSystem::Start()`.
- Backlight should be initialized and restored after LVGL display setup.
- LVGL buffer should be at least `lcd_width * 40`.
- Display config currently uses RGB565 byte swapping through `.flags.swap_bytes = true`.

Expected order in `main.cc`:

```text
NVS
USB MSC boot gate
Board Manager
Theme and PhoneUi
LVGL port and display
Touch cached polling
Fonts
Backlight
Services
PhoneSystem
```

## Touch Not Responding

Current design: GT911 touch is handled by a cached polling bridge in `main.cc`, not by direct `lvgl_port_add_touch()`.

Why: earlier direct LVGL-task polling could hang the I2C bus on hardware without a GT911 interrupt pin.

Healthy log:

```text
Touch input registered with cached polling
```

If touch does not work:

- Check `lcd_touch` exists in generated Board Manager config.
- Check GT911 I2C address and bus health.
- Look for repeated `Touch read failed` warnings.
- Keep I2C reads out of LVGL callbacks; update the cached bridge instead.

## SD Card Or Photos Show Empty

Symptoms:

```text
sdmmc_init_ocr ... returned 0x107
Photos app shows no photos
```

Checks:

- Insert a FAT-formatted SD card.
- Put images under `/photos` or `/DCIM`; Photos also has a shallow fallback scan from root.
- Supported image formats: `.jpg`, `.jpeg`, `.png`, `.bmp`.
- FileService mounts the Board Manager `fs_sdcard` device on demand.
- USB MSC mode uses the early-boot path in `main/usb_msc_mode.cc`.

## Out Of Memory Loading Images

Checks:

- Confirm PSRAM is enabled.
- Reduce very large source images.
- Check `ImageLibrary::LoadImage()` return value.
- Prefer scanned FileService paths rather than hard-coded local paths.

## WiFi Does Not Auto-Connect

Checks:

- Settings must save credentials through `WiFiConfig`.
- Auto-connect starts after `PhoneSystem::Start()`, so early boot logs may show UI before WiFi connects.
- Clear credentials from Settings or erase flash if NVS is polluted during testing.

## Audio, Assistant, Or Camera Unavailable

These services intentionally open heavy hardware paths on demand. If an app says unavailable:

- Check Board Manager device names: `audio_dac`, `audio_adc`, `camera`, `fs_sdcard`.
- Check I2C errors from codec/camera setup.
- Confirm SD card files exist for Music and Photos.
- For Assistant wake/runtime, current code may report unavailable when no real wake runtime or recorder is configured.

## USB Disk Mode

USB disk mode is not a normal app runtime state. Settings requests a one-shot boot flag, then the next boot enters TinyUSB MSC before the normal UI and services start.

If it does not appear on the host:

- Reboot after enabling the mode.
- Check SD card presence.
- Confirm USB cable supports data.
- Use the board's MSC startup button path only if that hardware input is configured.

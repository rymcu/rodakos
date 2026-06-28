# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

RodakOS is an ESP32-S3 firmware implementing a "Phone OS" UI experiment for RYMCU BigSmart (320x240 touch device).

**Hardware**: ESP32-S3 (16MB flash, 8MB PSRAM), ST7789 LCD (320x240 SPI), GT911 touch (I2C), PCA9557 IO expander, LEDC backlight (GPIO42)

**Architecture**: Built on ESP-IDF 5.4+ (tested on 5.5.4), LVGL 9.3, and esp-brookesia HAL framework.

**Reference**: [Espressif esp-brookesia](https://github.com/espressif/esp-brookesia) HAL framework provides board management, device lifecycle, and standardized peripheral APIs.

## Build System

### Prerequisites

**Must run in ESP-IDF PowerShell environment**. All build/flash commands require `idf.py` to be available.

To activate ESP-IDF environment:
- Use ESP-IDF PowerShell shortcut from Start Menu, OR
- Run `export.ps1` from your esp-idf installation directory

Verify: `echo $env:IDF_PATH` should output your ESP-IDF path.

### Build Commands

```powershell
# Full automated build (recommended for first build or after switching boards)
.\build_rodakos.ps1

# Or manual steps
idf.py set-target esp32s3          # First time only
idf.py bmgr -b rymcu_bigsmart      # Generate board config
.\fix_gen_paths.ps1                 # Fix generated paths
idf.py build                        # Compile
```

**Important**: After running `idf.py bmgr`, always run `.\fix_gen_paths.ps1` to correct absolute paths in generated code before building.

### Flash and Monitor

```powershell
# Flash to device and open serial monitor
.\flash_and_test.ps1

# Or manually
idf.py -p COM3 flash monitor

# Exit monitor: Ctrl+]
```

### Common Build Issues

See `TROUBLESHOOTING.md` for detailed solutions to 7 common issues:

1. **override_path errors** - Run `fix_gen_paths.ps1`
2. **CMakeLists.txt path errors** - Run `fix_gen_paths.ps1`
3. **Missing partitions_16m.csv** - File should exist in project root
4. **Partition table exceeds 16MB** - Use hex values, not "1M"/"15M"
5. **LCD config field name errors** - Use `lcd_width/lcd_height`, not `x_max/y_max`
6. **LEDC API not found** - Use `periph_ledc_handle_t` + ESP-IDF LEDC driver
7. **LVGL not initialized** - LVGL must be initialized before PhoneSystem starts

## Project Structure

```
main/
├── rodakos_adapters/      # HAL adapters (backlight → esp-brookesia LEDC)
│   ├── backlight_adapter.h/cc
├── phone_os/              # App lifecycle, navigation, services
│   ├── phone_system.h/cc
│   ├── phone_app_registry.h/cc
│   ├── phone_app_host.h/cc
│   ├── phone_navigation.h/cc
│   ├── phone_services.h
├── phone_ui/              # LVGL integration, theme, components
│   ├── phone_ui.h/cc
│   ├── phone_theme.h/cc
│   ├── phone_screen.h/cc
│   ├── phone_components.h/cc
└── apps/                  # Built-in apps
    ├── home/home_app.h/cc
    └── settings/settings_app.h/cc

components/
├── brookesia_hal_*/       # esp-brookesia HAL (local copy)
├── esp_board_manager/     # Board Manager framework
└── gen_bmgr_codes/        # Auto-generated board config (gitignored)
```

## Architecture Details

### Hardware Abstraction Layer

RodakOS uses **esp-brookesia HAL** as a thin wrapper over ESP-IDF drivers:

**Device Layer** (`dev_*`):
- Provides `init()` / `deinit()` only
- Returns peripheral handles (e.g., `periph_ledc_handle_t*`)
- No control functions (like `set_brightness_percent`)

**Peripheral Layer** (`periph_*`):
- Provides hardware handles (e.g., `periph_ledc_handle_t`)
- Contains low-level parameters (speed_mode, channel)
- Used with ESP-IDF driver APIs directly

**Example - Backlight Control**:
```cpp
// 1. Get device handle from Board Manager
void* ledc_handle;
esp_board_manager_get_device_handle("lcd_brightness", &ledc_handle);

// 2. Cast to peripheral handle
auto handle = static_cast<periph_ledc_handle_t*>(ledc_handle);

// 3. Use ESP-IDF LEDC driver API
uint32_t duty = (brightness * 8191) / 100;  // 13-bit resolution
ledc_set_duty(handle->speed_mode, handle->channel, duty);
ledc_update_duty(handle->speed_mode, handle->channel);
```

### LVGL Initialization

LVGL must be initialized **after** Board Manager but **before** PhoneSystem starts:

```cpp
// 1. Initialize Board Manager
esp_board_manager_init();

// 2. Get device handles
void *lcd_handle, *touch_handle;
esp_board_manager_get_device_handle("display_lcd", &lcd_handle);
esp_board_manager_get_device_handle("lcd_touch", &touch_handle);

// 3. Initialize LVGL port
lvgl_port_init(&lvgl_cfg);
lvgl_port_add_disp(&disp_cfg);
lvgl_port_add_touch(&touch_cfg);

// 4. Now safe to start PhoneSystem
PhoneSystem system(ui, services);
system.Start();
```

### Board Configuration

Board-specific configuration is in YAML files under:
`components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart/`

- `board_devices.yaml` - Device definitions (LCD, touch, codecs, etc.)
- `board_peripherals.yaml` - Peripheral pins and parameters
- `setup_device.c` - Custom initialization hooks

The Board Manager generates code from these YAML files into `components/gen_bmgr_codes/` (gitignored).

## Development Workflow

### Adding a New App

1. Create `main/apps/myapp/myapp_app.h` and `myapp_app.cc`
2. Implement `PhoneApp` interface: `OnCreate()`, `OnDestroy()`, `OnResume()`, `OnPause()`
3. Register in `main/apps/built_in_apps.cc`:
   ```cpp
   registry.Register("myapp", "My App", "icon.png", 
       []() -> std::unique_ptr<PhoneApp> {
           return std::make_unique<MyAppApp>();
       });
   ```
4. Add to `main/CMakeLists.txt` sources

### Adding a New Hardware Adapter

1. Create adapter in `main/rodakos_adapters/`
2. Get device handle from Board Manager
3. Wrap esp-brookesia peripheral APIs
4. Expose high-level interface to PhoneServices
5. Document esp-brookesia API references in comments

### Modifying UI Components

- Theme colors: `main/phone_ui/phone_theme.h`
- Common components: `main/phone_ui/phone_components.h/cc`
- Lock UI before LVGL calls: `PhoneUiLock lock(ui);`

## Testing

### Expected Boot Log

```
RodakOS: Starting RodakOS with esp-brookesia HAL
Board manager: Initializing peripherals...
Board manager: Initializing devices...
RYMCU_BIGSMART_SETUP: Resetting ST7789 panel before enabling LCD power
BacklightAdapter: Backlight adapter initialized
LVGL port initialized
PhoneSystem: Starting Phone OS
HomeApp: Phone desktop ready with N apps
RodakOS: RodakOS started successfully
```

### Common Runtime Issues

**Assert failure in lvgl_port_lock**:
- LVGL not initialized before first UI call
- Check initialization order in `main.cc`

**Touch not responding**:
- Check GT911 I2C address (0x5D or 0x14)
- Verify touch handle added to LVGL port

**Blank screen**:
- Check backlight initialization
- Verify LCD panel handle is correct type
- Check LVGL buffer size (should be `lcd_width * 40` minimum)

## Automation Scripts

Three PowerShell scripts simplify common tasks (see `SCRIPTS_README.md` for details):

1. **`build_rodakos.ps1`** - One-command build (bmgr + fix paths + build)
2. **`fix_gen_paths.ps1`** - Fix generated code paths after `idf.py bmgr`
3. **`flash_and_test.ps1`** - Flash firmware and open serial monitor

All require ESP-IDF PowerShell environment to be active.

## Documentation

- **README.md** - Project overview and quick start
- **MIGRATION_SUMMARY.md** - esp-brookesia HAL integration summary
- **TROUBLESHOOTING.md** - Solutions to 7 common build/runtime issues
- **QUICK_FIX.md** - Quick reference for path fixes and API patterns
- **SCRIPTS_README.md** - Automation scripts usage guide
- **docs/firmware-download.md** - Detailed firmware build and flash guide

## Partition Table

Project uses custom 16MB partition table (`partitions_16m.csv`):

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x200000,
storage,  data, fat,     ,        0xDE0000,
```

**Important**: Use hex values, not "1M"/"15M" notation, to avoid partition size calculation errors.

- `nvs` (24KB) - WiFi config, Settings storage
- `phy_init` (4KB) - PHY calibration data
- `factory` (2MB) - Main firmware (current ~668KB)
- `storage` (~14MB) - FAT filesystem for SD card mount

## Code Style

- Match surrounding code: same comment density, naming conventions, idioms
- Use `ESP_LOGI/LOGE/LOGW` for logging with component TAGs
- LVGL calls must be protected with `PhoneUiLock`
- Prefer `std::unique_ptr` for app ownership
- Use `static_cast` for type conversions, avoid C-style casts

## Git Workflow

- Main development branch: `main`
- Commit message format: Short imperative sentence + optional body
- Don't commit `components/gen_bmgr_codes/` (gitignored)
- Don't commit `build/` directory (gitignored)

## Performance Notes

- Current firmware size: ~668KB (33% of 2MB factory partition)
- LVGL buffer: `lcd_width * 40` bytes
- PSRAM available: 7424KB for LVGL buffers and app data
- LVGL task priority: 4, stack: 6144 bytes

## Acknowledgments

Hardware abstraction based on [Espressif esp-brookesia](https://github.com/espressif/esp-brookesia) HAL framework (v0.7.5).

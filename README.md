# RodakOS

ESP32-S3 firmware implementing a "Phone OS" UI experiment for RYMCU BigSmart (320x240 touch device).

## Architecture

Built on:
- **ESP-IDF 5.4+** (tested on 5.5.4)
- **LVGL 9.3** for UI rendering
- **esp-brookesia HAL** for hardware abstraction (display/touch/backlight/audio)

Hardware abstraction is based on [Espressif esp-brookesia](https://github.com/espressif/esp-brookesia) HAL framework, providing:
- Board Manager for device lifecycle
- Standardized device APIs (LCD, touch, LEDC, audio codecs, etc.)
- YAML-based board configuration (see `components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart/`)

## Quick Start

```powershell
# In ESP-IDF PowerShell environment
.\build_brookesia.ps1      # Automated build
.\flash_and_test.ps1       # Flash to COM3 and monitor
```

See **SCRIPTS_README.md** for detailed usage.

## Project Structure

```
main/
├── rodakos_adapters/      # HAL adapters (backlight → esp-brookesia LEDC)
├── phone_os/              # App lifecycle, navigation, services
├── phone_ui/              # LVGL integration, theme, components
└── apps/                  # Built-in apps (Home, Settings)

components/
├── brookesia_hal_*/       # esp-brookesia HAL (local copy)
├── esp_board_manager/     # Board Manager framework
└── gen_bmgr_codes/        # Auto-generated board config (gitignored)
```

## Documentation

- **SCRIPTS_README.md** - Automation scripts usage guide
- **CLAUDE.md** - Architecture and development guide
- **MIGRATION_SUMMARY.md** - esp-brookesia HAL integration summary
- **TROUBLESHOOTING.md** - Common issues and solutions

## Hardware

- **MCU**: ESP32-S3 (16MB flash, 8MB PSRAM)
- **Display**: ST7789 320x240 (SPI)
- **Touch**: GT911 (I2C)
- **IO Expander**: PCA9557 (I2C)
- **Backlight**: LEDC PWM (GPIO42 via PCA9557)
- **Audio**: ES8311 DAC + ES7210 ADC (I2S)
- **Storage**: SD card (FAT)

Board configuration: `components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart/`

## License

See LICENSE file.

## Acknowledgments

Hardware abstraction based on [Espressif esp-brookesia](https://github.com/espressif/esp-brookesia) HAL framework.

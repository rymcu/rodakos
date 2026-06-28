# RodakOS - esp-brookesia HAL 迁移快速修复脚本

> **参考**: [Espressif esp-brookesia](https://github.com/espressif/esp-brookesia) HAL 框架

## 自动修复生成代码路径

运行 `idf.py bmgr -b rymcu_bigsmart` 后执行此脚本：

```powershell
# fix_gen_paths.ps1
$genDir = "components/gen_bmgr_codes"

Write-Host "修正生成代码中的路径..." -ForegroundColor Yellow

# 1. 修正 idf_component.yml
(Get-Content "$genDir/idf_component.yml") `
    -replace 'D:\\workspace\\rodakos\\managed_components\\espressif__brookesia_hal_boards', '../../components/brookesia_hal_boards' `
    | Set-Content "$genDir/idf_component.yml"

# 2. 修正 CMakeLists.txt
(Get-Content "$genDir/CMakeLists.txt") `
    -replace '../../managed_components/espressif__brookesia_hal_boards', '../../components/brookesia_hal_boards' `
    -replace 'D:/workspace/rodakos/managed_components/espressif__brookesia_hal_boards', '${CMAKE_SOURCE_DIR}/components/brookesia_hal_boards' `
    | Set-Content "$genDir/CMakeLists.txt"

Write-Host "✅ 路径修正完成" -ForegroundColor Green
```

## 使用方法

**前提**：在 ESP-IDF PowerShell 环境中运行（确保 `$env:IDF_PATH` 已设置）

```powershell
# 切换板卡或首次生成
idf.py bmgr -b rymcu_bigsmart

# 自动修正路径
.\fix_gen_paths.ps1

# 构建
idf.py build
```

**或使用自动化脚本**（包含 bmgr + 修正 + 构建）：
```powershell
.\build_rodakos.ps1
```

## 验证清单

运行构建前检查：

- [ ] `partitions_16m.csv` 存在（使用十六进制值）
- [ ] `components/gen_bmgr_codes/idf_component.yml` 路径是相对路径
- [ ] `components/gen_bmgr_codes/CMakeLists.txt` 路径是相对路径
- [ ] `main/main.cc` 使用 `lcd_cfg->lcd_width/lcd_height`
- [ ] `.gitignore` 包含 `components/gen_bmgr_codes/`

## 常见 API 字段名

### dev_display_lcd_config_t
```c
lcd_width      // 不是 x_max
lcd_height     // 不是 y_max
swap_xy
mirror_x
mirror_y
```

### dev_ledc_ctrl API
```c
dev_ledc_ctrl_set_brightness_percent(handle, percent);  // 0-100
dev_ledc_ctrl_get_brightness_percent(handle);
```

### esp_board_manager API
```c
esp_board_manager_init();
esp_board_manager_get_device_handle(name, &handle);
esp_board_manager_get_device_config(name, &config);
esp_board_manager_check_name(name);  // 检查设备是否存在
```

## 分区表模板

16MB flash (`partitions_16m.csv`):
```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x200000,
storage,  data, fat,     ,        0xDE0000,
```

**注意**：必须使用十六进制值，不要用 "1M"/"15M"！

## 回滚到旧架构

如果迁移失败需要回滚：

```powershell
git checkout main/board/
git checkout main/idf_component.yml
git checkout main/CMakeLists.txt
git checkout main/main.cc
git checkout main/phone_os/phone_services.h
git checkout main/apps/settings/settings_app.cc
Remove-Item -Recurse -Force components/brookesia_*
Remove-Item -Recurse -Force components/esp_board_manager
Remove-Item -Recurse -Force components/gen_bmgr_codes
Remove-Item partitions_16m.csv
idf.py fullclean
idf.py build
```

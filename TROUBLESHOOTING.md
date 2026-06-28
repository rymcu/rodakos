# esp-brookesia HAL 本地迁移 - 问题解决记录

> **参考**: [Espressif esp-brookesia](https://github.com/espressif/esp-brookesia)  
> **文档**: [Board Manager 开发指南](https://github.com/espressif/esp-brookesia/blob/main/docs/board_manager.md)

## 问题 1: override_path 指向已删除的 managed_components

**错误信息：**
```
ERROR: The "override_path" field in the manifest file
```

**原因：**
`idf.py bmgr -b rymcu_bigsmart` 生成的 `components/gen_bmgr_codes/idf_component.yml` 包含绝对路径，指向 `D:\workspace\rodakos\managed_components\...`，但我们已经将组件迁移到 `components/` 并删除了 `managed_components/`。

**解决方案：**
手动修正 `components/gen_bmgr_codes/idf_component.yml`：

```yaml
# 修正前
dependencies:
  esp_io_expander_pca9557:
    override_path: D:\workspace\rodakos\managed_components\espressif__brookesia_hal_boards\boards\rymcu\rymcu_bigsmart/components/esp_io_expander_pca9557

# 修正后
dependencies:
  esp_io_expander_pca9557:
    override_path: ../../components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart/components/esp_io_expander_pca9557
```

---

## 问题 2: CMakeLists.txt 路径硬编码

**错误信息：**
```
找不到 setup_device.c
```

**原因：**
`components/gen_bmgr_codes/CMakeLists.txt` 中的 `SRC_DIRS` 和 `INCLUDE_DIRS` 硬编码为 `../../managed_components/...`。

**解决方案：**
修正 `components/gen_bmgr_codes/CMakeLists.txt`：

```cmake
# 修正前
idf_component_register(
    SRC_DIRS "." "../../managed_components/espressif__brookesia_hal_boards/boards/rymcu/rymcu_bigsmart"
    INCLUDE_DIRS "." "../../managed_components/espressif__brookesia_hal_boards/boards/rymcu/rymcu_bigsmart"
    REQUIRES esp_board_manager
)

# 修正后
idf_component_register(
    SRC_DIRS "." "../../components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart"
    INCLUDE_DIRS "." "../../components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart"
    REQUIRES esp_board_manager
)
```

---

## 问题 3: 缺少 partitions_16m.csv 分区表

**错误信息：**
```
FileNotFoundError: [Errno 2] No such file or directory: 'D:/workspace/rodakos/partitions_16m.csv'
ninja: error: 'D:/workspace/rodakos/partitions_16m.csv', needed by 'partition_table/partition-table.bin', missing and no known rule to make it
```

**原因：**
BigSmart 板级配置的 `sdkconfig.defaults.board` 要求自定义分区表：
```
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_16m.csv"
```

但项目根目录没有这个文件。

**解决方案：**
创建 `partitions_16m.csv`（ESP32-S3 16MB flash 标准分区）：

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x200000,
storage,  data, fat,     ,        0xDE0000,
```

**分区说明：**
- `nvs` (24KB) - 存储 WiFi 配置、Settings 等
- `phy_init` (4KB) - PHY 校准数据
- `factory` (2MB = 0x200000) - 主固件（当前 ~668KB，留足够余量）
- `storage` (~14MB = 0xDE0000) - FAT 文件系统（SD 卡挂载点）

**重要：** 
- 16MB flash = 0x1000000 bytes
- 必须减去前面的 offset (0x10000) 和已分配空间
- 使用十六进制精确计算，避免 "1M"/"15M" 这种单位导致对齐问题
- 总计算：0x10000 + 0x200000 + 0xDE0000 = 0xFF0000 < 0x1000000 ✅

---

## 经验总结

### 1. 本地迁移的必要步骤

将 Brookesia HAL 从远端迁移到本地后，**必须手动修正生成代码中的路径**：

1. 运行 `idf.py bmgr -b rymcu_bigsmart`
2. 检查并修正 `components/gen_bmgr_codes/idf_component.yml`
3. 检查并修正 `components/gen_bmgr_codes/CMakeLists.txt`
4. 确保板级要求的文件存在（如分区表）

### 2. 自动化脚本（建议）

可以编写 PowerShell 脚本自动修正路径：

```powershell
# fix_gen_paths.ps1
$genDir = "components/gen_bmgr_codes"

# 修正 idf_component.yml
(Get-Content "$genDir/idf_component.yml") `
    -replace 'managed_components\\espressif__brookesia_hal_boards', 'components/brookesia_hal_boards' `
    | Set-Content "$genDir/idf_component.yml"

# 修正 CMakeLists.txt
(Get-Content "$genDir/CMakeLists.txt") `
    -replace '../../managed_components/espressif__brookesia_hal_boards', '../../components/brookesia_hal_boards' `
    | Set-Content "$genDir/CMakeLists.txt"

Write-Host "✅ Paths fixed in gen_bmgr_codes"
```

使用：
```powershell
idf.py bmgr -b rymcu_bigsmart
.\fix_gen_paths.ps1
idf.py build
```

### 3. gitignore 配置

确保 `.gitignore` 包含：
```
components/gen_bmgr_codes/
```

生成的代码不应提交，每次 `idf.py bmgr` 都会重新生成。

### 4. 分区表管理

BigSmart 使用 16MB flash，建议：
- `factory` 1-2MB（当前固件 ~668KB）
- `nvs` 24-32KB（足够存 WiFi/Settings）
- `storage` 剩余空间（SD 卡、音频文件）

如果需要 OTA，改用：
```csv
nvs,      data, nvs,     0x9000,  0x6000,
otadata,  data, ota,     0xf000,  0x2000,
phy_init, data, phy,     0x11000, 0x1000,
ota_0,    app,  ota_0,   0x20000, 2M,
ota_1,    app,  ota_1,   ,        2M,
storage,  data, fat,     ,        12M,
```

---

## 快速参考

### 完整构建流程

```powershell
# 首次 / 切换板卡后
idf.py bmgr -b rymcu_bigsmart

# 修正生成代码路径（手动或脚本）
# 1. components/gen_bmgr_codes/idf_component.yml
# 2. components/gen_bmgr_codes/CMakeLists.txt

# 确保分区表存在
# 项目根目录需要 partitions_16m.csv

# 构建
idf.py build

# 烧录
idf.py -p COM3 flash monitor
```

### 验证清单

- [ ] `components/gen_bmgr_codes/idf_component.yml` 路径正确
- [ ] `components/gen_bmgr_codes/CMakeLists.txt` 路径正确
- [ ] `partitions_16m.csv` 存在于项目根目录
- [ ] `.gitignore` 包含 `components/gen_bmgr_codes/`
- [ ] `managed_components/` 已删除
- [ ] `main/board/` 已删除

### 回滚方案

如果迁移失败，可以回滚到旧架构：

```powershell
git checkout main/board/
git checkout main/idf_component.yml
git checkout main/CMakeLists.txt
git checkout main/main.cc
rm -r components/brookesia_*
rm -r components/esp_board_manager
idf.py fullclean
idf.py build
```

---

## 问题 4: 分区表超出 flash 容量

**错误信息：**
```
Partitions tables occupies 16.1MB of flash (16842752 bytes) which does not fit in configured flash size 16MB.
```

**原因：**
使用 "1M" 和 "15M" 这种简写单位时，分区生成工具按 1024*1024 计算，但没有考虑前面的 offset (0x10000 = 64KB) 和对齐要求，导致总和超出 16MB。

**解决方案：**
使用十六进制精确指定大小：

```csv
# 错误的配置（会超出）
factory,  app,  factory, 0x10000, 1M,
storage,  data, fat,     ,        15M,

# 正确的配置（十六进制精确计算）
factory,  app,  factory, 0x10000, 0x200000,    # 2MB
storage,  data, fat,     ,        0xDE0000,    # ~14MB
```

**计算公式：**
- 16MB flash = 0x1000000 bytes
- 可用空间 = 0x1000000 - 0x10000 (bootloader/partition table 区域) = 0xFF0000
- factory (2MB) = 0x200000
- storage = 0xFF0000 - 0x200000 = 0xDF0000（实际用 0xDE0000 留余量）

---

## 问题 5: dev_display_lcd_config_t 字段名错误

**错误信息：**
```
error: 'dev_display_lcd_config_t' has no member named 'x_max'
error: 'dev_display_lcd_config_t' has no member named 'y_max'
```

**原因：**
Brookesia HAL 的 LCD 配置结构体字段名与预期不同。

**解决方案：**
使用正确的字段名：

```cpp
// 错误
static PhoneUi ui(lcd_cfg->x_max, lcd_cfg->y_max);

// 正确
static PhoneUi ui(lcd_cfg->lcd_width, lcd_cfg->lcd_height);
```

**完整结构体定义** (`components/esp_board_manager/devices/dev_display_lcd/dev_display_lcd.h`)：
```c
struct dev_display_lcd_config {
    const char *name;
    const char *chip;
    const char *sub_type;
    uint16_t lcd_width;     // 使用这个
    uint16_t lcd_height;    // 使用这个
    uint8_t swap_xy : 1;
    uint8_t mirror_x : 1;
    uint8_t mirror_y : 1;
    // ...
};
```

---

## 问题 6: dev_ledc_ctrl API 不存在

**错误信息：**
```
error: expected '(' before '*' token
static_cast<dev_ledc_ctrl_handles_t*>(ledc_handle_)
```

**原因：**
esp-brookesia HAL 的 dev_ledc_ctrl 只提供了 init/deinit 接口，没有提供 `dev_ledc_ctrl_set_brightness_percent` 等便捷函数。需要直接使用底层的 periph_ledc API。

**解决方案：**
使用 `periph_ledc_handle_t` 和 ESP-IDF 的 `ledc_set_duty` / `ledc_update_duty` API：

```cpp
// 错误的用法（不存在的 API）
dev_ledc_ctrl_set_brightness_percent(handle, brightness);

// 正确的用法
auto handle = static_cast<periph_ledc_handle_t*>(ledc_handle_);
uint32_t max_duty = (1 << 13) - 1;  // 13-bit resolution
uint32_t duty = (brightness * max_duty) / 100;
ledc_set_duty(handle->speed_mode, handle->channel, duty);
ledc_update_duty(handle->speed_mode, handle->channel);
```

**必要的 include**：
```cpp
#include <periph_ledc.h>
#include <driver/ledc.h>
```

---

## 问题 8: LCD 色彩异常（颜色错乱）

**现象：**
- 系统启动正常，LVGL 初始化成功
- 屏幕有显示，但颜色不对（例如：红蓝互换）

**原因：**
RGB565 格式的字节序设置不正确。ST7789 需要正确的 `swap_bytes` 配置。

**解决方案：**
在 `main.cc` 的 LVGL display 配置中调整 `swap_bytes` 标志：

```cpp
const lvgl_port_display_cfg_t disp_cfg = {
    // ... 其他配置 ...
    .flags = {
        .buff_dma = true,
        .swap_bytes = true,  // 改为 true 试试
    }
};
```

或者检查板级配置中的 LCD 设置：
```yaml
# board_devices.yaml 中的 display_lcd
flags:
  swap_bytes: true  # 根据实际屏幕调整
```

**调试步骤**：
1. 尝试切换 `swap_bytes` 为 `true`/`false`
2. 重新构建并烧录：`idf.py build flash`
3. 观察颜色是否正常


# Brookesia HAL 本地迁移方案

## 目标

将 esp-brookesia HAL 层和 BigSmart 板级配置迁移到项目本地 `components/` 目录，避免远端依赖。

## 方案对比

### ❌ 方案 A：直接依赖远端（已放弃）
```yaml
dependencies:
  espressif/esp_board_manager: "*"
  espressif/brookesia_hal_*: "*"
```
**问题**：
- 版本不可控
- 网络依赖（国内访问慢）
- 定制修改需要 fork 上游
- 构建时需联网下载

### ✅ 方案 B：迁移到本地（当前方案）
```
components/
├── brookesia_hal/          # 从 managed_components 拷贝核心文件
│   ├── interface/          # 设备抽象接口
│   ├── adaptor/            # display/audio 实现
│   └── board_bigsmart/     # BigSmart 板级配置（YAML + setup_device.c）
└── esp_board_manager/      # 板级管理器（可选：如果需要多板支持）
```

**优势**：
- 完全离线构建
- 版本锁定，可控
- 方便定制（如补充 camera/LED/sensor）
- 代码审查友好

## 迁移步骤

### Step 1: 提取已下载的组件

从 `managed_components/` 拷贝到 `components/`：

```powershell
# 核心 HAL 接口
cp -r managed_components/espressif__brookesia_hal_interface components/brookesia_hal_interface

# HAL 适配器实现（display/audio/storage）
cp -r managed_components/espressif__brookesia_hal_adaptor components/brookesia_hal_adaptor

# 板级配置（包含 rymcu_bigsmart）
cp -r managed_components/espressif__brookesia_hal_boards components/brookesia_hal_boards

# Board Manager（如需多板支持）
cp -r managed_components/espressif__esp_board_manager components/esp_board_manager

# 工具库（HAL 依赖）
cp -r managed_components/espressif__brookesia_lib_utils components/brookesia_lib_utils
cp -r managed_components/espressif__cmake_utilities components/cmake_utilities
```

### Step 2: 修改 idf_component.yml

移除远端依赖，改为本地 `override_path`（或直接删除，因为已在 components/ 下）：

```yaml
dependencies:
  # LVGL
  lvgl/lvgl: ~9.3.0
  esp_lvgl_port: ~2.6.0

  # Touch driver
  espressif/esp_lcd_touch_gt911: ~1.0.0

  idf:
    version: '>=5.4.0'

# Brookesia 组件已在 components/ 目录，ESP-IDF 会自动发现
```

### Step 3: 精简组件（可选）

只保留必需的部分：

#### 保留：
- `brookesia_hal_interface/` - 设备接口定义
- `brookesia_hal_adaptor/src/display/` - LCD/Touch/Backlight 实现
- `brookesia_hal_boards/boards/rymcu/rymcu_bigsmart/` - BigSmart 配置
- `esp_board_manager/` - 板级管理器核心

#### 删除（未使用）：
- `brookesia_hal_adaptor/src/audio/` （Phase 2 再加回）
- `brookesia_hal_adaptor/src/storage/` （Phase 2 再加回）
- `brookesia_hal_boards/boards/` 下其他板卡目录
- `brookesia_lib_utils/src/plugin_manager/` 等高级特性

### Step 4: 调整 BigSmart 板级配置

编辑 `components/brookesia_hal_boards/boards/rymcu/rymcu_bigsmart/board_devices.yaml`，补充缺失硬件：

```yaml
# 原有配置保持（audio_dac/adc, display_lcd, lcd_touch, fs_sdcard, gpio_expander, battery）

# 新增：摄像头
devices:
  - name: camera
    chip: gc0308
    type: camera
    sub_type: dvp
    config:
      xclk_freq_hz: 16000000
      frame_size: VGA
      pixel_format: YUV422
    peripherals:
      - name: dvp_camera
      - name: i2c_master
        i2c_addr: [0x42]

  # 新增：RGB LED
  - name: rgb_led
    chip: ws2812b
    type: led_strip
    sub_type: rmt
    config:
      led_count: 1
    peripherals:
      - name: rmt_led

  # 新增：IMU 传感器
  - name: imu_sensor
    chip: qmi8658
    type: sensor
    sub_type: i2c
    config:
      sample_rate_ms: 100
    peripherals:
      - name: i2c_master
        i2c_addr: [0x6A]

peripherals:
  # 新增：DVP 摄像头外设
  - name: dvp_camera
    type: camera
    role: dvp
    config:
      xclk_pin: 5
      pclk_pin: 7
      vsync_pin: 44
      href_pin: 46
      data_pins: [16, 18, 8, 17, 15, 6, 4, 9]

  # 新增：RMT LED 外设
  - name: rmt_led
    type: rmt
    role: tx
    config:
      gpio_num: 43
      resolution_hz: 10000000
```

### Step 5: 构建流程

```powershell
# 生成板级代码
idf.py bmgr -b rymcu_bigsmart

# 构建
idf.py build

# 烧录
idf.py -p COM3 flash monitor
```

## 简化方案（推荐用于当前阶段）

**如果觉得 Board Manager 太重**，可以更激进地简化：

### 方案 C：只提取 BigSmart 板级代码，手工集成

1. **不要** Board Manager 和 YAML 生成系统
2. **直接拷贝** `rymcu_bigsmart/setup_device.c` 的关键逻辑到 `BigSmartBoard::Initialize()`
3. **手动集成** PCA9557 驱动：`rymcu_bigsmart/components/esp_io_expander_pca9557/`

这样只需：
```
components/
└── esp_io_expander_pca9557/    # 从 Brookesia 提取
    ├── esp_io_expander_pca9557.c
    ├── include/
    └── idf_component.yml
```

在 `BigSmartBoard` 里直接调用 PCA9557 驱动，不走 Board Manager 框架。

**优势**：
- 最小侵入
- 无框架开销
- 保持现有架构
- 只复用 PCA9557 驱动这一个关键组件

**劣势**：
- Audio/WiFi/Battery 需要自己实现（不能复用 Brookesia Services）

---

## 推荐决策

**当前阶段（Phase 1）**：
- ✅ 采用方案 C（只提取 PCA9557 驱动）
- ✅ 保持 `BigSmartBoard` 架构，只替换 PCA9557 为官方驱动
- ✅ Phone OS/UI 层完全不动

**Phase 2（Audio/WiFi）时评估**：
- 如果决定用 Brookesia Services → 完整迁移方案 B
- 如果自己实现 → 继续当前轻量架构

---

## 立即行动

你倾向于哪个方案？

1. **方案 B（完整 HAL）**：迁移整个 Brookesia HAL 到本地 components/
2. **方案 C（最小提取）**：只提取 PCA9557 驱动，保持 BigSmartBoard 架构
3. **回滚**：放弃 Brookesia，继续用现有 board/ 层（最保守）

我个人推荐 **方案 C**，理由：
- BigSmart 的 board 层已经很清晰（199 行）
- 主要痛点只是 PCA9557 需要自己维护
- 提取官方 PCA9557 驱动就能解决这个痛点
- Audio/WiFi/Battery 等复杂能力可以 Phase 2 再决定是否引入 Brookesia Services

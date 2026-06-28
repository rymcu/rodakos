# RodakOS Brookesia 化迁移方案

## 目标架构

```
┌─────────────────────────────────────────────────────────────┐
│ Phone OS Layer (保持不变)                                    │
│ ├─ PhoneAppRegistry / PhoneAppHost / PhoneNavigation       │
│ ├─ PhoneAppDescriptor / PhoneApp lifecycle                 │
│ └─ Built-in Apps: Home, Settings, Music, Camera...         │
├─────────────────────────────────────────────────────────────┤
│ Phone UI Layer (保持不变)                                    │
│ ├─ PhoneScreen / PhoneTheme / PhoneComponents              │
│ └─ LVGL 9.3 + esp_lvgl_port                                │
├─────────────────────────────────────────────────────────────┤
│ PhoneServices (改造为 Brookesia Service 包装)               │
│ ├─ Backlight (包装 dev_ledc_ctrl)                          │
│ ├─ Audio (包装 brookesia_service_audio)                    │
│ ├─ WiFi (包装 brookesia_service_wifi)                      │
│ └─ Battery (包装 brookesia_hal_power)                      │
├─────────────────────────────────────────────────────────────┤
│ Brookesia Services Layer (新增)                             │
│ ├─ brookesia_service_audio                                 │
│ ├─ brookesia_service_wifi                                  │
│ ├─ brookesia_service_nvs (可选，替代现有 Settings)          │
│ └─ brookesia_service_manager                               │
├─────────────────────────────────────────────────────────────┤
│ Brookesia HAL Layer (替换 main/board/)                      │
│ ├─ brookesia_hal_interface (device 抽象接口)                │
│ ├─ brookesia_hal_adaptor (display/audio/storage 实现)       │
│ └─ brookesia_hal_boards (rymcu_bigsmart YAML)              │
├─────────────────────────────────────────────────────────────┤
│ ESP Board Manager (生成板级代码)                             │
│ └─ gen_bmgr_codes/ (自动生成)                               │
└─────────────────────────────────────────────────────────────┘
```

## Phase 1: HAL 层替换 (本阶段)

### 1.1 添加依赖

修改 `main/idf_component.yml`：

```yaml
dependencies:
  # Brookesia HAL
  espressif/esp_board_manager:
    version: "^1.0.0"
  espressif/brookesia_hal_interface:
    version: "^1.0.0"
  espressif/brookesia_hal_adaptor:
    version: "^1.0.0"
  espressif/brookesia_hal_boards:
    version: "^1.0.0"
  
  # LVGL (保持现有)
  lvgl/lvgl: ~9.3.0
  esp_lvgl_port: ~2.6.0
  
  # Touch driver (保持现有)
  espressif/esp_lcd_touch_gt911: ~1.0.0
  
  idf:
    version: '>=5.4.0'
```

### 1.2 生成板级配置

```powershell
# 安装 bmgr 助手 (一次性，在 IDF 环境)
pip install esp-bmgr-assist

# 下载依赖
idf.py reconfigure

# 生成 rymcu_bigsmart 板级代码
idf.py bmgr -b rymcu_bigsmart
```

**生成的文件**：`components/gen_bmgr_codes/`
- `gen_board_periph_config.c` / `gen_board_periph_handles.c`
- `gen_board_device_config.c` / `gen_board_device_handles.c`
- `gen_board_info.c`
- `gen_board_metadata.yaml`
- `board_manager.defaults` (sdkconfig 默认值)
- `CMakeLists.txt` / `idf_component.yml`

### 1.3 删除旧 board 层

```powershell
rm -r main/board/
```

从 `main/CMakeLists.txt` 删除：
```cmake
"board/backlight.cc"
"board/bigsmart_board.cc"
"board/i2c_device.cc"
"board/pca9557.cc"
```

### 1.4 适配器层设计

**目标**：保持 `PhoneServices` 接口不变，内部切换到 Brookesia HAL。

创建 `main/brookesia_adapters/` 目录：

#### `brookesia_adapters/backlight_adapter.h`
```cpp
#pragma once

#include <cstdint>

// 保持与旧 Backlight 类相同的接口，供 PhoneServices 使用
class BacklightAdapter {
public:
    BacklightAdapter();
    ~BacklightAdapter();
    
    void SetBrightness(uint8_t brightness, bool permanent = false);
    void RestoreBrightness();
    uint8_t GetBrightness() const { return brightness_; }

private:
    void* ledc_handle_ = nullptr;  // dev_ledc_ctrl_handles_t*
    uint8_t brightness_ = 75;
};
```

#### `brookesia_adapters/backlight_adapter.cc`
```cpp
#include "backlight_adapter.h"
#include "settings.h"

#include <esp_board_manager.h>
#include <dev_ledc_ctrl.h>
#include <esp_log.h>

namespace {
constexpr const char* TAG = "BacklightAdapter";
}

BacklightAdapter::BacklightAdapter() {
    esp_err_t ret = esp_board_manager_get_device_handle("lcd_brightness", &ledc_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get lcd_brightness handle: %s", esp_err_to_name(ret));
    }
}

BacklightAdapter::~BacklightAdapter() = default;

void BacklightAdapter::SetBrightness(uint8_t brightness, bool permanent) {
    if (brightness > 100) {
        brightness = 100;
    }
    brightness_ = brightness;
    
    if (permanent) {
        Settings settings("display", true);
        settings.SetInt("brightness", brightness_);
    }
    
    if (ledc_handle_ != nullptr) {
        dev_ledc_ctrl_set_brightness_percent(
            static_cast<dev_ledc_ctrl_handles_t*>(ledc_handle_), 
            brightness_
        );
    }
}

void BacklightAdapter::RestoreBrightness() {
    Settings settings("display", false);
    int brightness = settings.GetInt("brightness", 75);
    if (brightness < 5) {
        brightness = 75;
    }
    SetBrightness(static_cast<uint8_t>(brightness), false);
}
```

### 1.5 改造 main.cc

```cpp
#include "brookesia_adapters/backlight_adapter.h"
#include "phone_os/phone_system.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_ui.h"
#include "settings.h"

#include <esp_board_manager.h>
#include <dev_display_lcd.h>
#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

namespace {
constexpr const char* TAG = "RodakOS";
}

extern "C" void app_main(void) {
    // NVS 初始化
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Starting RodakOS with Brookesia HAL");

    // 初始化 Board Manager (替代 BigSmartBoard::Initialize)
    ESP_ERROR_CHECK(esp_board_manager_init());
    ESP_LOGI(TAG, "Board manager initialized");

    // 获取 LCD 配置（分辨率）
    dev_display_lcd_config_t *lcd_cfg = nullptr;
    ESP_ERROR_CHECK(esp_board_manager_get_device_config("display_lcd", 
        reinterpret_cast<void**>(&lcd_cfg)));
    
    // 初始化 Phone UI
    Settings display_settings("display", false);
    static PhoneUi ui(lcd_cfg->x_max, lcd_cfg->y_max);
    ui.SetThemeName(display_settings.GetString("theme", "dark"));

    // 初始化 PhoneServices（使用适配器）
    static BacklightAdapter backlight;
    static PhoneServices services;
    services.SetBacklight(&backlight);

    // 启动 Phone System
    static PhoneSystem system(ui, services);
    if (!system.Start()) {
        ESP_LOGE(TAG, "PhoneSystem start failed");
        return;
    }

    // 恢复亮度
    backlight.RestoreBrightness();
    
    ESP_LOGI(TAG, "RodakOS started successfully");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### 1.6 更新 PhoneServices 接口

修改 `main/phone_os/phone_services.h`：

```cpp
#pragma once

class BacklightAdapter;  // 前向声明

class PhoneServices {
public:
    void SetBacklight(BacklightAdapter* backlight) { backlight_ = backlight; }
    BacklightAdapter* backlight() { return backlight_; }

private:
    BacklightAdapter* backlight_ = nullptr;
};
```

### 1.7 更新 CMakeLists.txt

修改 `main/CMakeLists.txt`：

```cmake
set(SOURCES
    "main.cc"
    "settings.cc"
    "brookesia_adapters/backlight_adapter.cc"
    "phone_os/phone_app_registry.cc"
    "phone_os/phone_app_host.cc"
    "phone_os/phone_navigation.cc"
    "phone_os/phone_system.cc"
    "phone_ui/phone_ui.cc"
    "phone_ui/phone_theme.cc"
    "phone_ui/phone_screen.cc"
    "phone_ui/phone_components.cc"
    "apps/built_in_apps.cc"
    "apps/home/home_app.cc"
    "apps/settings/settings_app.cc"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "." "phone_os" "phone_ui" "apps" "brookesia_adapters"
)
```

### 1.8 验证构建和启动

```powershell
idf.py build
idf.py -p COM3 flash monitor
```

**预期启动日志**：
```
RodakOS: Starting RodakOS with Brookesia HAL
Board manager: Initializing peripherals...
Board manager: Initializing devices...
RYMCU_BIGSMART_SETUP: Resetting ST7789 panel before enabling LCD power
LVGL: Starting LVGL task
RodakOS: Board manager initialized
PhoneSystem: Starting Phone OS
HomeApp: Phone desktop ready with 2 apps
RodakOS: RodakOS started successfully
```

---

## Phase 2: Services 对接

### 2.1 Audio Service

#### 添加依赖 (`main/idf_component.yml`)
```yaml
espressif/brookesia_service_audio: "^1.0.0"
espressif/brookesia_service_manager: "^1.0.0"
```

#### 创建适配器 `brookesia_adapters/audio_adapter.h`
```cpp
#pragma once

#include <string>

class AudioAdapter {
public:
    AudioAdapter();
    ~AudioAdapter();
    
    bool Initialize();
    bool Play(const std::string& path);
    bool Stop();
    bool Pause();
    bool Resume();
    bool IsPlaying() const;
    int GetVolume() const;
    void SetVolume(int volume);

private:
    void* audio_service_ = nullptr;  // 内部持有 service handle
    bool initialized_ = false;
};
```

#### 在 PhoneServices 添加
```cpp
class PhoneServices {
public:
    void SetAudio(AudioAdapter* audio) { audio_ = audio; }
    AudioAdapter* audio() { return audio_; }
    // ...

private:
    AudioAdapter* audio_ = nullptr;
    // ...
};
```

### 2.2 WiFi Service

#### 创建 `brookesia_adapters/wifi_adapter.h`
```cpp
#pragma once

#include <string>
#include <vector>
#include <functional>

struct WiFiNetwork {
    std::string ssid;
    int rssi;
    bool is_open;
};

enum class WiFiState {
    kDisconnected,
    kConnecting,
    kConnected,
    kFailed
};

using WiFiStateCallback = std::function<void(WiFiState)>;

class WiFiAdapter {
public:
    WiFiAdapter();
    ~WiFiAdapter();
    
    bool Initialize();
    std::vector<WiFiNetwork> Scan();
    bool Connect(const std::string& ssid, const std::string& password);
    void Disconnect();
    WiFiState GetState() const;
    std::string GetIP() const;
    void SetStateCallback(WiFiStateCallback callback);

private:
    void* wifi_service_ = nullptr;
    WiFiStateCallback state_callback_;
    WiFiState state_ = WiFiState::kDisconnected;
};
```

### 2.3 Battery Service

#### 创建 `brookesia_adapters/battery_adapter.h`
```cpp
#pragma once

#include <cstdint>

class BatteryAdapter {
public:
    BatteryAdapter();
    ~BatteryAdapter();
    
    bool Initialize();
    int GetPercentage() const;      // 0-100
    int GetVoltage() const;         // mV
    bool IsCharging() const;
    bool IsLowBattery() const;      // < 20%
    bool IsCritical() const;        // < 10%

private:
    void* power_device_ = nullptr;
};
```

---

## Phase 3: 应用层扩展

### 3.1 Settings App WiFi 页面

在 `settings_app.cc` 的 `OnCreate` 添加 WiFi 配置选项卡。

### 3.2 Music App

创建 `main/apps/music/music_app.{h,cc}`，使用 `AudioAdapter` 播放 SD 卡音乐。

### 3.3 Status Bar

在 `PhoneScreen` 添加顶栏，显示：
- WiFi 图标（连接状态）
- 电池图标（电量百分比 + 充电状态）
- 时间（需要 SNTP service）

---

## Phase 4: 补充硬件配置

### 4.1 创建自定义 board overlay

如果官方 `rymcu_bigsmart` YAML 缺少 camera/LED/sensor，可以：

**方案 A**: 提 PR 到 esp-brookesia 上游  
**方案 B**: 使用 `--amend` 本地覆盖

创建 `boards/rymcu_bigsmart_full/board_amend.yaml`：

```yaml
devices:
  - name: camera
    chip: gc0308
    type: camera
    sub_type: dvp
    config:
      xclk_freq_hz: 16000000
      frame_size: VGA
      pixel_format: YUV422
      fb_count: 2
    peripherals:
      - name: dvp_camera
      - name: i2c_master
        i2c_addr: [0x42]  # GC0308 I2C 地址
  
  - name: rgb_led
    chip: ws2812b
    type: led_strip
    sub_type: rmt
    config:
      led_count: 1
    peripherals:
      - name: rmt_led
  
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
  - name: dvp_camera
    type: camera
    role: dvp
    config:
      xclk_pin: 5
      pclk_pin: 7
      vsync_pin: 44
      href_pin: 46
      data_pins: [16, 18, 8, 17, 15, 6, 4, 9]
  
  - name: rmt_led
    type: rmt
    role: tx
    config:
      gpio_num: 43
      resolution_hz: 10000000
```

使用：
```powershell
idf.py bmgr -b rymcu_bigsmart -a boards/rymcu_bigsmart_full/
```

---

## 构建流程变更

### 旧流程
```powershell
idf.py build
idf.py -p COM3 flash monitor
```

### 新流程
```powershell
# 首次 / 切板 / 清理后
idf.py bmgr -b rymcu_bigsmart

# 日常开发
idf.py build
idf.py -p COM3 flash monitor
```

---

## 迁移检查清单

- [ ] Phase 1.1: 添加 Brookesia HAL 依赖
- [ ] Phase 1.2: 生成 `rymcu_bigsmart` 板级代码
- [ ] Phase 1.3: 删除 `main/board/` 目录
- [ ] Phase 1.4: 实现 `BacklightAdapter`
- [ ] Phase 1.5: 改造 `main.cc` 使用 `esp_board_manager_init()`
- [ ] Phase 1.6: 更新 `PhoneServices` 接口
- [ ] Phase 1.7: 更新 `CMakeLists.txt`
- [ ] Phase 1.8: 验证 display/touch/backlight 工作正常
- [ ] Phase 2.1: 集成 `brookesia_service_audio`
- [ ] Phase 2.2: 集成 `brookesia_service_wifi`
- [ ] Phase 2.3: 集成 battery HAL
- [ ] Phase 3.1: Settings app 添加 WiFi 配置页
- [ ] Phase 3.2: 实现 Music app
- [ ] Phase 3.3: 添加 Status bar (WiFi/Battery/Time)
- [ ] Phase 4.1: 补充 camera/LED/sensor YAML 配置
- [ ] 更新 CLAUDE.md 文档
- [ ] 更新 README.md 构建说明

---

## 回滚方案

如果迁移过程中遇到阻塞问题，可以：

1. **保留 git 分支**：迁移在新分支 `feature/brookesia` 进行
2. **保留旧 board 层**：暂时不删除 `main/board/`，通过 Kconfig 选择
3. **渐进式验证**：每个 Phase 独立验证，失败不影响已完成部分

---

## 预期收益

- **代码减少**：~400 行 board 代码 → 0 行（YAML 配置 + 生成代码）
- **Audio 能力**：0 → 完整的播放/录音/混音 pipeline (~2000 行省下)
- **WiFi 能力**：0 → 状态机 + provisioning (~500 行省下)
- **Battery 管理**：0 → 电压/百分比/充电检测 (~300 行省下)
- **官方维护**：BigSmart YAML 跟随 esp-brookesia 更新，PCA9557/ES8311/ES7210 驱动由官方维护
- **生态对齐**：与 Espressif AIoT 生态统一架构，未来可对接 AI Agent 框架

---

## 参考资料

- [esp-brookesia GitHub](https://github.com/espressif/esp-brookesia)
- [esp-brookesia 文档](https://docs.espressif.com/projects/esp-brookesia/en/latest/)
- [esp-board-manager 文档](https://docs.espressif.com/projects/esp-board-manager/en/latest/)
- [BigSmart 官方仓库](https://github.com/rymcu/BigSmart-Open)
- [BigSmart 硬件配置](https://github.com/rymcu/BigSmart-Open/blob/main/docs/zh/hardware.md)

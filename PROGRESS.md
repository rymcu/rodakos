# Brookesia 化迁移进度

## ✅ 已完成 - Phase 1: HAL 层本地迁移

### 1. Brookesia 组件本地化
✅ 从 `managed_components/` 迁移到本地 `components/`：
- `brookesia_hal_interface` - 设备抽象接口
- `brookesia_hal_adaptor` - display/audio/storage 实现
- `brookesia_hal_boards` - 板级配置（含 rymcu_bigsmart）
- `esp_board_manager` - 板级管理器
- `brookesia_lib_utils` - 工具库
- `cmake_utilities` - CMake 工具

### 2. 依赖配置
✅ 更新 `main/idf_component.yml` - 移除远端依赖，使用本地组件
✅ ESP-IDF 自动发现 `components/` 下的组件

### 3. 适配器层
✅ `main/brookesia_adapters/backlight_adapter.{h,cc}` - 包装 Brookesia LEDC HAL
✅ 保持 `PhoneServices` 接口不变（`BacklightAdapter*`）

### 4. 代码改造
✅ `main/main.cc` - 使用 `esp_board_manager_init()`
✅ `main/phone_os/phone_services.h` - 接口改为 `BacklightAdapter*`
✅ `main/CMakeLists.txt` - 移除 board/*.cc，添加 backlight_adapter.cc
✅ `apps/settings/settings_app.cc` - 更新 include 路径

### 5. 板级配置生成
✅ 运行 `idf.py bmgr -b rymcu_bigsmart`
✅ 生成 `components/gen_bmgr_codes/`：
  - `gen_board_periph_config.c` / `gen_board_periph_handles.c`
  - `gen_board_device_config.c` / `gen_board_device_handles.c`
  - `gen_board_info.c`
  - `gen_board_metadata.yaml`
  - `board_manager.defaults` (sdkconfig)
  - `CMakeLists.txt` / `idf_component.yml`

### 6. 清理
✅ 删除 `managed_components/`（远端依赖）
✅ 删除 `main/board/`（旧 board 层）

## 🔄 进行中

🔄 **构建验证** - 后台任务 b3vr2nnw9
  - `idf.py build` 运行中
  - 预计时间: 3-5 分钟（首次构建）

## ⏳ 待完成

⏳ **Phase 1.8**: 烧录测试
  - `idf.py -p COM3 flash monitor`
  - 验证启动日志

⏳ **Phase 2**: Services 对接
  - Audio service (ES8311 + ES7210)
  - WiFi service
  - Battery service (ADC)

⏳ **Phase 3**: 应用扩展
  - Settings app WiFi 配置页
  - Music app
  - Status bar

⏳ **Phase 4**: 硬件补充
  - Camera (GC0308)
  - LED (WS2812B)
  - Sensor (QMI8658)

## 架构变化

### 迁移前
```
main/board/ (8 files, ~400 lines)
├── bigsmart_board.{cc,h}
├── backlight.{cc,h}
├── pca9557.{cc,h}
└── i2c_device.{cc,h}

dependencies.lock - 远端依赖
```

### 迁移后
```
components/ (本地组件)
├── brookesia_hal_interface/
├── brookesia_hal_adaptor/
├── brookesia_hal_boards/
│   └── boards/rymcu/rymcu_bigsmart/
│       ├── board_devices.yaml
│       ├── board_peripherals.yaml
│       ├── setup_device.c
│       └── components/esp_io_expander_pca9557/
├── esp_board_manager/
└── gen_bmgr_codes/ (自动生成)

main/brookesia_adapters/ (适配层)
└── backlight_adapter.{cc,h}
```

## 构建流程

### 新流程（已建立）
```powershell
# 首次 / 切板 / 清理后
idf.py bmgr -b rymcu_bigsmart

# 日常开发
idf.py build
idf.py -p COM3 flash monitor
```

## 预期收益

✅ **完全离线构建** - 无需联网下载依赖
✅ **版本锁定** - 本地组件版本可控
✅ **方便定制** - 直接修改 `components/` 下代码
✅ **官方维护** - PCA9557/ES8311/ES7210 驱动来自官方
✅ **扩展性** - 为 Audio/WiFi/Battery services 做好准备

## 文档

- `MIGRATION_PLAN.md` - 完整迁移方案
- `LOCAL_MIGRATION_PLAN.md` - 本地迁移决策过程
- `PROGRESS.md` - 本文件，迁移进度追踪

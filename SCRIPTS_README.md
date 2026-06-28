# RodakOS - esp-brookesia HAL 自动化脚本使用指南

> **基于**: [Espressif esp-brookesia](https://github.com/espressif/esp-brookesia) HAL 框架  
> **文档**: [Board Manager 开发指南](https://github.com/espressif/esp-brookesia/blob/main/docs/board_manager.md)

本目录包含 3 个 PowerShell 脚本，用于简化 esp-brookesia HAL 的构建、修正和烧录流程。

## 📋 前提条件

所有脚本必须在 **ESP-IDF PowerShell 环境** 中运行。

### 如何激活 ESP-IDF 环境？

**方法 1：使用 ESP-IDF PowerShell 快捷方式**
- Windows 开始菜单 → ESP-IDF → ESP-IDF 5.x PowerShell

**方法 2：手动激活（如果已安装 ESP-IDF）**
```powershell
# 查找你的 ESP-IDF 安装目录（例如 C:\esp\esp-idf）
cd C:\esp\esp-idf
.\export.ps1
```

**验证环境**：
```powershell
# 应该输出 ESP-IDF 路径
echo $env:IDF_PATH

# 应该能执行
idf.py --version
```

---

## 🛠️ 脚本说明

### 1. `fix_gen_paths.ps1` - 修正生成代码路径

**用途**：修正 `idf.py bmgr` 生成代码中的绝对路径为相对路径。

**使用场景**：
- 运行 `idf.py bmgr -b rymcu_bigsmart` 之后
- 切换板卡配置后

**用法**：
```powershell
idf.py bmgr -b rymcu_bigsmart
.\fix_gen_paths.ps1
```

**输出**：
```
修正生成代码中的路径...
  ✅ idf_component.yml 路径已修正
  ✅ CMakeLists.txt 路径已修正

✅ 所有路径修正完成！可以运行 idf.py build 了
```

---

### 2. `build_rodakos.ps1` - 完整自动化构建

**用途**：一键完成 bmgr 生成、路径修正、清理、重新配置和构建。

**使用场景**：
- 首次构建 esp-brookesia HAL 版本
- 切换板卡后重新构建
- 完全清理后重建

**用法**：
```powershell
.\build_rodakos.ps1
```

**流程**：
```
[1/5] 检查必要文件...
[2/5] 生成板级配置...
[3/5] 修正生成代码路径...
[4/5] 清理并重新配置...
[5/5] 构建项目...
```

**成功输出**：
```
========================================
构建完成！可以烧录了：
  idf.py -p COM3 flash monitor
========================================
```

---

### 3. `flash_and_test.ps1` - 烧录和测试

**用途**：烧录固件到设备并启动串口监视器查看启动日志。

**使用场景**：
- 构建成功后烧录到设备
- 验证 Brookesia HAL 是否正常工作

**用法**：
```powershell
# 使用默认端口 COM3
.\flash_and_test.ps1

# 指定端口
.\flash_and_test.ps1 -Port COM5
```

**流程**：
```
[1/3] 检查固件大小...
[2/3] 烧录到设备 (COM3)...
[3/3] 启动串口监视器...
```

**预期启动日志**：
```
RodakOS: Starting RodakOS with esp-brookesia HAL
Board manager: Initializing peripherals...
Board manager: Initializing devices...
RYMCU_BIGSMART_SETUP: Resetting ST7789 panel before enabling LCD power
BacklightAdapter: Backlight adapter initialized
PhoneSystem: Starting Phone OS
HomeApp: Phone desktop ready with 2 apps
RodakOS: RodakOS started successfully
```

---

## 🚀 完整工作流示例

### 首次构建 esp-brookesia HAL 版本

```powershell
# 1. 激活 ESP-IDF 环境（如果尚未激活）
# 打开 ESP-IDF PowerShell 快捷方式

# 2. 进入项目目录
cd D:\workspace\rodakos

# 3. 一键构建
.\build_rodakos.ps1

# 4. 烧录测试
.\flash_and_test.ps1
```

### 日常开发流程

```powershell
# 修改代码后
idf.py build

# 烧录测试
.\flash_and_test.ps1
```

### 切换板卡配置

```powershell
# 生成新板卡配置
idf.py bmgr -b <board_name>

# 修正路径
.\fix_gen_paths.ps1

# 构建
idf.py build
```

---

## ⚠️ 常见问题

### Q: 脚本报错 "未检测到 ESP-IDF 环境"

**A**: 请先激活 ESP-IDF 环境（见上文"前提条件"）。

### Q: 构建失败 "partitions_16m.csv 不存在"

**A**: 确保项目根目录有 `partitions_16m.csv` 文件（应该已存在）。

### Q: 烧录失败 "无法打开端口 COM3"

**A**: 
- 检查设备是否连接
- 使用 `-Port` 参数指定正确的端口
- 关闭占用端口的其他程序（如 Arduino IDE）

### Q: 启动日志中没有 "esp-brookesia HAL" 字样

**A**: 可能是旧固件，运行 `.\build_rodakos.ps1` 重新构建并烧录。

---

## 📚 相关文档

- **TROUBLESHOOTING.md** - 详细的问题解决方案（5 个常见问题）
- **QUICK_FIX.md** - 快速修复参考卡片
- **MIGRATION_PLAN.md** - 完整迁移方案（Phase 1-4）
- **MIGRATION_SUMMARY.md** - 迁移完成总结

---

## 🔄 回滚到旧架构

如果 Brookesia HAL 迁移出现问题，可以回滚到旧的 board 层：

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

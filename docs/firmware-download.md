# RodakOS 固件下载方法

本文档记录 RodakOS 固件编译、下载到 ESP32-S3 设备、串口监视和常见问题处理流程。

## 硬件与环境

- 项目路径：`D:\workspace\rodakos`
- 目标芯片：ESP32-S3
- 硬件基线：RYMCU BigSmart 风格 320x240 触摸设备，ST7789 + GT911 + PCA9557 + GPIO42 背光
- 串口：`COM3`
- 推荐环境：ESP-IDF 5.4+，当前本机已验证 ESP-IDF 5.5.4

如果当前 PowerShell 找不到 `idf.py`，先加载 Espressif PowerShell 环境：

```powershell
. C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1
```

## 首次配置目标芯片

首次拉起项目或清理过构建目录后，确认目标芯片是 `esp32s3`：

```powershell
cd D:\workspace\rodakos
idf.py set-target esp32s3
```

目标芯片设置会影响 `sdkconfig`、分区表、编译参数和烧录参数，不要把它切成普通 `esp32`。

## 编译固件

```powershell
cd D:\workspace\rodakos
idf.py build
```

成功后主要产物位于：

- `build\bootloader\bootloader.bin`
- `build\partition_table\partition-table.bin`
- `build\rodakos.bin`
- `build\rodakos.elf`

当前验证过的 app 固件大小约为 `668448` bytes，1MB app 分区仍有约 36% 空间。

## 下载到设备

常规下载命令：

```powershell
cd D:\workspace\rodakos
idf.py -p COM3 flash
```

下载完成后，ESP-IDF 会自动复位设备。也可以下载后直接打开串口监视：

```powershell
idf.py -p COM3 flash monitor
```

退出 monitor 使用：

```text
Ctrl+]
```

## 精确 esptool 下载参数

一般优先使用 `idf.py -p COM3 flash`。如果需要在产线脚本或独立工具里下载，可以按当前 `build\flasher_args.json` 使用以下地址：

```powershell
esptool.py --chip esp32s3 -p COM3 --before default_reset --after hard_reset write_flash `
  --flash_mode dio --flash_freq 80m --flash_size 16MB `
  0x0 build\bootloader\bootloader.bin `
  0x8000 build\partition_table\partition-table.bin `
  0x10000 build\rodakos.bin
```

这些 offset 来自 ESP-IDF 生成的烧录参数：

- `0x0`：bootloader
- `0x8000`：partition table
- `0x10000`：RodakOS app

## 串口日志确认

打开 monitor：

```powershell
cd D:\workspace\rodakos
idf.py -p COM3 monitor
```

正常启动时应能看到类似日志：

```text
RodakOS: Starting RodakOS
LVGL: Starting LVGL task
BigSmartBoard: GT911 touch initialized at 0x5D
HomeApp: Phone desktop ready with 1 apps
```

其中 `HomeApp: Phone desktop ready with 1 apps` 表示 Phone OS、Phone UI、Home 桌面和应用注册表已完成首屏拉起。

## 常见问题

### 找不到 COM3

1. 检查 USB 线是否支持数据传输。
2. 在 Windows 设备管理器里确认串口号是否仍是 `COM3`。
3. 如果串口号变化，临时改用实际端口，例如 `idf.py -p COM5 flash`。

### 串口被占用

关闭其它串口工具、旧的 `idf.py monitor`、Arduino Serial Monitor 或厂商下载工具，然后重新执行：

```powershell
idf.py -p COM3 flash
```

### 下载连接失败

如果自动进入下载模式失败，按住设备 `BOOT` 键，再点按 `RESET`，随后松开 `BOOT`，重新执行 flash 命令。

### 启动状态异常或 NVS 配置污染

设置 app 会把亮度、主题、语言偏好写入 NVS。需要清空整片 flash 时使用：

```powershell
idf.py -p COM3 erase-flash
idf.py -p COM3 flash monitor
```

`erase-flash` 会清掉所有 NVS 和应用数据，只在调试异常状态或确认需要重置设备时使用。

## 推荐开发循环

日常开发建议使用以下循环：

```powershell
. C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1
cd D:\workspace\rodakos
idf.py build
idf.py -p COM3 flash monitor
```

如果只是改 UI 布局、Home 桌面或 Settings app，通常不需要重新 `set-target`。

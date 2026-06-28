# RodakOS - esp-brookesia HAL 烧录和测试脚本
#
# 参考: Espressif esp-brookesia HAL (https://github.com/espressif/esp-brookesia)
# 功能: 检查固件大小、烧录到设备、启动串口监视器
#
param(
    [string]$Port = "COM3"
)

# 检查是否已在 ESP-IDF 环境中
if (-not $env:IDF_PATH) {
    Write-Host "❌ 未检测到 ESP-IDF 环境" -ForegroundColor Red
    Write-Host "请先激活 ESP-IDF 环境，例如：" -ForegroundColor Yellow
    Write-Host "  - 运行 ESP-IDF PowerShell 快捷方式" -ForegroundColor White
    Write-Host "  - 或执行 export.ps1 (通常在 esp-idf 目录下)" -ForegroundColor White
    exit 1
}

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "RodakOS 烧录和测试" -ForegroundColor Cyan
Write-Host "基于 esp-brookesia HAL" -ForegroundColor Gray
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 检查构建是否完成
if (-not (Test-Path "build/rodakos.bin")) {
    Write-Host "❌ 构建未完成，请先运行 idf.py build" -ForegroundColor Red
    exit 1
}

Write-Host "[1/3] 检查固件大小..." -ForegroundColor Yellow
$appSize = (Get-Item "build/rodakos.bin").Length
Write-Host "  固件大小: $([math]::Round($appSize/1KB, 2)) KB" -ForegroundColor White
if ($appSize -gt 2MB) {
    Write-Host "  ⚠️  警告: 固件超过 2MB，可能无法烧录到 factory 分区" -ForegroundColor Yellow
} else {
    Write-Host "  ✅ 固件大小正常" -ForegroundColor Green
}
Write-Host ""

Write-Host "[2/3] 烧录到设备 ($Port)..." -ForegroundColor Yellow
Write-Host "  按 Ctrl+C 可中断烧录" -ForegroundColor Gray
Write-Host ""

idf.py -p $Port flash
if ($LASTEXITCODE -ne 0) {
    Write-Host "  ❌ 烧录失败" -ForegroundColor Red
    exit 1
}
Write-Host "  ✅ 烧录完成" -ForegroundColor Green
Write-Host ""

Write-Host "[3/3] 启动串口监视器..." -ForegroundColor Yellow
Write-Host "  关键启动日志应包含：" -ForegroundColor Gray
Write-Host "    - 'RodakOS: Starting RodakOS with Brookesia HAL'" -ForegroundColor Gray
Write-Host "    - 'Board manager initialized'" -ForegroundColor Gray
Write-Host "    - 'BacklightAdapter: Backlight adapter initialized'" -ForegroundColor Gray
Write-Host "    - 'HomeApp: Phone desktop ready with N apps'" -ForegroundColor Gray
Write-Host ""
Write-Host "  按 Ctrl+] 退出监视器" -ForegroundColor Gray
Write-Host ""

Start-Sleep -Seconds 2

idf.py -p $Port monitor

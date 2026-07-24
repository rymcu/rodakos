# Quick Build Script for RodakOS
# 快速构建脚本

param(
    [switch]$Clean,
    [switch]$Flash,
    [string]$Port = "COM3"
)

Write-Host "🔨 RodakOS Quick Build" -ForegroundColor Cyan
Write-Host ""

if ($Clean) {
    Write-Host "🧹 Cleaning build..." -ForegroundColor Yellow
    idf.py fullclean
    if (Test-Path "build") {
        Remove-Item -Recurse -Force "build" -ErrorAction SilentlyContinue
    }
    Write-Host ""
}

# 构建
Write-Host "🏗️  Building firmware..." -ForegroundColor Yellow
$buildStart = Get-Date
idf.py build

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "❌ Build failed!" -ForegroundColor Red
    exit $LASTEXITCODE
}

$buildTime = ((Get-Date) - $buildStart).TotalSeconds
Write-Host ""
Write-Host "✅ Build successful! ($([math]::Round($buildTime, 1))s)" -ForegroundColor Green
Write-Host ""

# 显示固件信息
if (Test-Path "build\rodakos.bin") {
    $size = (Get-Item "build\rodakos.bin").Length
    $sizeKB = [math]::Round($size / 1KB, 1)
    $sizeMB = [math]::Round($size / 1MB, 2)
    Write-Host "📦 Firmware size: $sizeMB MB ($sizeKB KB)" -ForegroundColor Cyan
}

# Recovery 布局下根项目的 flash target 会把主应用写入较小的 factory 分区。
if ($Flash) {
    Write-Host ""
    Write-Host "❌ 已禁用 quick_build 的直接烧录" -ForegroundColor Red
    Write-Host "请先运行 .\build_ota_bundle.ps1，再使用 .\flash_and_test.ps1 -Port $Port -Erase 完成首次迁移。" -ForegroundColor Yellow
    Write-Host "日常 Rodak OTA 仅上传 build\rodakos.bin。" -ForegroundColor Yellow
    exit 1
} else {
    Write-Host ""
    Write-Host "首次迁移/工厂维护：" -ForegroundColor White
    Write-Host "  .\build_ota_bundle.ps1" -ForegroundColor Gray
    Write-Host "  .\flash_and_test.ps1 -Port COM3 -Erase" -ForegroundColor Gray
    Write-Host "日常 OTA：仅上传 build\rodakos.bin" -ForegroundColor White
}

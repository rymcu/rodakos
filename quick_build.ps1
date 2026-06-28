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

# 烧录
if ($Flash) {
    Write-Host ""
    Write-Host "⚡ Flashing to $Port..." -ForegroundColor Yellow
    idf.py -p $Port flash monitor
} else {
    Write-Host ""
    Write-Host "To flash:" -ForegroundColor White
    Write-Host "  .\quick_build.ps1 -Flash -Port COM3" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Or manually:" -ForegroundColor White
    Write-Host "  idf.py -p COM3 flash monitor" -ForegroundColor Gray
}

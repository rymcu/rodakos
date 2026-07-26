# RodakOS - esp-brookesia HAL 自动化构建脚本
#
# 参考: Espressif esp-brookesia HAL (https://github.com/espressif/esp-brookesia)
# 功能: 生成板级配置、修正路径、清理、配置并构建
#
# 检查是否已在 ESP-IDF 环境中
if (-not $env:IDF_PATH) {
    Write-Host "❌ 未检测到 ESP-IDF 环境" -ForegroundColor Red
    Write-Host "请先激活 ESP-IDF 环境，例如：" -ForegroundColor Yellow
    Write-Host "  - 运行 ESP-IDF PowerShell 快捷方式" -ForegroundColor White
    Write-Host "  - 或执行 export.ps1 (通常在 esp-idf 目录下)" -ForegroundColor White
    exit 1
}

$ErrorActionPreference = "Stop"
$repoRoot = $PSScriptRoot
& (Join-Path $repoRoot "assert_idf6_environment.ps1")

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "RodakOS 自动化构建脚本" -ForegroundColor Cyan
Write-Host "基于 esp-brookesia HAL" -ForegroundColor Gray
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 1. 检查必要文件
Write-Host "[1/5] 检查必要文件..." -ForegroundColor Yellow
if (-not (Test-Path "partitions_16m.csv")) {
    Write-Host "  ❌ 缺少 partitions_16m.csv" -ForegroundColor Red
    exit 1
}
Write-Host "  ✅ partitions_16m.csv 存在" -ForegroundColor Green

# 2. 清理旧构建目录
Write-Host "[2/5] 清理旧构建目录..." -ForegroundColor Yellow
if (Test-Path "build") {
    Remove-Item -Recurse -Force build
}
Write-Host "  ✅ 旧构建目录已清理" -ForegroundColor Green

# 3. 生成板级配置并重新配置
Write-Host "[3/5] 生成板级配置并重新配置..." -ForegroundColor Yellow
& (Join-Path $repoRoot "generate_board_config.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "  ✅ 板级配置生成完成" -ForegroundColor Green

# 4. 验证生成配置
Write-Host "[4/5] 验证生成配置..." -ForegroundColor Yellow
$generatedCmake = Join-Path $repoRoot "components/gen_bmgr_codes/CMakeLists.txt"
if (-not (Test-Path -LiteralPath $generatedCmake)) {
    Write-Host "  ❌ 缺少 Board Manager 生成组件" -ForegroundColor Red
    exit 1
}
Write-Host "  ✅ Board Manager 生成组件可用" -ForegroundColor Green

# 5. 构建项目
Write-Host "[5/5] 构建项目..." -ForegroundColor Yellow
idf.py build
if ($LASTEXITCODE -ne 0) {
    Write-Host "  ❌ 构建失败" -ForegroundColor Red
    exit 1
}
Write-Host "  ✅ 构建成功！" -ForegroundColor Green

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "构建完成！首次迁移请生成并烧录 merged 镜像：" -ForegroundColor Cyan
Write-Host "  .\build_ota_bundle.ps1" -ForegroundColor White
Write-Host "  .\flash_and_test.ps1 -Port COM3 -Erase" -ForegroundColor White
Write-Host "日常 Rodak OTA 仅上传 build\rodakos.bin。" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

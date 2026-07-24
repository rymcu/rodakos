# Setup Board Configuration Script
# 为 RodakOS 设置板级配置和构建环境

Write-Host "🔧 Setting up RodakOS board configuration..." -ForegroundColor Cyan
Write-Host ""

# 1. 创建板级配置软链接（如果不存在）
$linkPath = "D:\workspace\rodakos\components\esp_board_manager\boards\rymcu_bigsmart"
$targetPath = "D:\workspace\rodakos\components\brookesia_hal_boards\boards\rymcu\rymcu_bigsmart"

if (Test-Path $linkPath) {
    Write-Host "✅ Board link already exists: $linkPath" -ForegroundColor Green
} else {
    Write-Host "📋 Creating board configuration link..." -ForegroundColor Yellow
    New-Item -ItemType Junction -Path $linkPath -Target $targetPath -ErrorAction Stop
    Write-Host "✅ Created board link successfully" -ForegroundColor Green
}

Write-Host ""

# 2. 生成板级配置
Write-Host "📋 Generating board configuration..." -ForegroundColor Yellow
idf.py bmgr -b rymcu_bigsmart

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "❌ Board configuration generation failed" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "✅ Board configuration setup completed successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Run: idf.py build" -ForegroundColor White
Write-Host "  2. Package: .\build_ota_bundle.ps1" -ForegroundColor White
Write-Host "  3. First migration: .\flash_and_test.ps1 -Port COM3 -Erase" -ForegroundColor White
Write-Host ""

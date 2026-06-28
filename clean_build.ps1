# Clean Build Script - 彻底清理并重新构建
# 解决 Clang 编译器缓存问题

Write-Host "🧹 Cleaning build directories..." -ForegroundColor Cyan

# 删除 build 目录
if (Test-Path "build") {
    Remove-Item -Recurse -Force "build"
    Write-Host "✅ Deleted build/" -ForegroundColor Green
}

# 删除生成的 Board Manager 代码
if (Test-Path "components/gen_bmgr_codes") {
    Remove-Item -Recurse -Force "components/gen_bmgr_codes"
    Write-Host "✅ Deleted gen_bmgr_codes/" -ForegroundColor Green
}

Write-Host ""
Write-Host "🔧 Running full clean build..." -ForegroundColor Cyan
Write-Host ""

# 重新配置
idf.py fullclean 2>$null

# 生成板级配置
Write-Host "📋 Generating board configuration..." -ForegroundColor Yellow
idf.py bmgr -b rymcu_bigsmart

# 修正路径
Write-Host "🔧 Fixing generated paths..." -ForegroundColor Yellow
.\fix_gen_paths.ps1

# 构建
Write-Host ""
Write-Host "🏗️  Building firmware..." -ForegroundColor Yellow
idf.py build

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "✅ Build successful!" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "❌ Build failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}

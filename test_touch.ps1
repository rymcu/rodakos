# 快速测试脚本 - 烧录并监视触摸功能
# 使用方法：.\test_touch.ps1

Write-Host "🔥 烧录固件并启动监视器..." -ForegroundColor Cyan
Write-Host ""

idf.py -p COM3 flash monitor

# 监视器退出后的说明
Write-Host ""
Write-Host "📋 触摸测试检查清单：" -ForegroundColor Yellow
Write-Host "  ✅ 查找日志：'Touch input registered successfully'"
Write-Host "  ✅ 无 'Task watchdog' 错误"
Write-Host "  ✅ 无 'I2C deadlock' 警告"
Write-Host "  ✅ 尝试点击屏幕上的应用图标"
Write-Host ""

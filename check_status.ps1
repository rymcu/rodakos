# 检查系统状态 - 读取串口日志
# 使用方法：.\check_status.ps1

Write-Host "📡 读取设备串口日志..." -ForegroundColor Cyan
Write-Host "按 Ctrl+] 退出监视器" -ForegroundColor Yellow
Write-Host ""

idf.py -p COM3 monitor

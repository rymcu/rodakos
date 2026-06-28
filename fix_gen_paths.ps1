# RodakOS - esp-brookesia HAL 路径自动修正脚本
#
# 参考: Espressif esp-brookesia HAL (https://github.com/espressif/esp-brookesia)
# 用途: 在 idf.py bmgr 后修正生成代码中的绝对路径为相对路径
#
$ErrorActionPreference = "Stop"
$genDir = "components/gen_bmgr_codes"

Write-Host "修正生成代码中的路径..." -ForegroundColor Yellow

if (-not (Test-Path $genDir)) {
    Write-Host "❌ $genDir 不存在，请先运行 idf.py bmgr" -ForegroundColor Red
    exit 1
}

# 1. 修正 idf_component.yml
if (Test-Path "$genDir/idf_component.yml") {
    (Get-Content "$genDir/idf_component.yml") `
        -replace 'D:\\workspace\\rodakos\\managed_components\\espressif__brookesia_hal_boards', '../../components/brookesia_hal_boards' `
        | Set-Content "$genDir/idf_component.yml"
    Write-Host "  ✅ idf_component.yml 路径已修正" -ForegroundColor Green
}

# 2. 修正 CMakeLists.txt
if (Test-Path "$genDir/CMakeLists.txt") {
    (Get-Content "$genDir/CMakeLists.txt") `
        -replace '../../managed_components/espressif__brookesia_hal_boards', '../../components/brookesia_hal_boards' `
        -replace 'D:/workspace/rodakos/managed_components/espressif__brookesia_hal_boards', '${CMAKE_SOURCE_DIR}/components/brookesia_hal_boards' `
        | Set-Content "$genDir/CMakeLists.txt"
    Write-Host "  ✅ CMakeLists.txt 路径已修正" -ForegroundColor Green
}

Write-Host ""
Write-Host "✅ 所有路径修正完成！可以运行 idf.py build 了" -ForegroundColor Green

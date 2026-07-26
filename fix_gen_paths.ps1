# RodakOS - esp-brookesia HAL 路径自动修正脚本
#
# 参考: Espressif esp-brookesia HAL (https://github.com/espressif/esp-brookesia)
# 用途: 在 idf.py bmgr 后修正生成代码中的绝对路径为相对路径
#
$ErrorActionPreference = "Stop"
$repoRoot = $PSScriptRoot
$genDir = Join-Path $repoRoot "components/gen_bmgr_codes"
$repoForward = $repoRoot.Replace('\', '/')
$boardForward = "$repoForward/components/brookesia_hal_boards"
$boardBackward = $boardForward.Replace('/', '\')
$managedForward = "$repoForward/managed_components/espressif__brookesia_hal_boards"
$managedBackward = $managedForward.Replace('/', '\')

Write-Host "修正生成代码中的路径..." -ForegroundColor Yellow

if (-not (Test-Path $genDir)) {
    Write-Host "❌ $genDir 不存在，请先运行 idf.py bmgr" -ForegroundColor Red
    exit 1
}

$componentManifest = Join-Path $genDir "idf_component.yml"
$generatedCmake = Join-Path $genDir "CMakeLists.txt"
$portableFiles = @($componentManifest, $generatedCmake)
$missingPortableFiles = @(
    $portableFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) }
)
if ($missingPortableFiles.Count -gt 0) {
    throw "Board Manager 生成配置不完整，缺少文件：$($missingPortableFiles -join ', ')"
}

# 1. 修正 idf_component.yml
$content = Get-Content -Raw -LiteralPath $componentManifest
$content = $content.Replace($managedForward, '../../components/brookesia_hal_boards')
$content = $content.Replace($managedBackward, '..\..\components\brookesia_hal_boards')
$content = $content.Replace($boardForward, '../../components/brookesia_hal_boards')
$content = $content.Replace($boardBackward, '..\..\components\brookesia_hal_boards')
Set-Content -LiteralPath $componentManifest -Value $content -Encoding utf8 -NoNewline
Write-Host "  ✅ idf_component.yml 路径已修正" -ForegroundColor Green

# 2. 修正 CMakeLists.txt
$content = Get-Content -Raw -LiteralPath $generatedCmake
$content = $content.Replace('../../managed_components/espressif__brookesia_hal_boards',
                            '../../components/brookesia_hal_boards')
$content = $content.Replace($managedForward,
                            '${CMAKE_SOURCE_DIR}/components/brookesia_hal_boards')
$content = $content.Replace($boardForward,
                            '${CMAKE_SOURCE_DIR}/components/brookesia_hal_boards')
Set-Content -LiteralPath $generatedCmake -Value $content -Encoding utf8 -NoNewline
Write-Host "  ✅ CMakeLists.txt 路径已修正" -ForegroundColor Green

$absolutePathPatterns = @([regex]::Escape($repoRoot), [regex]::Escape($repoForward))
$remainingAbsolutePath = Select-String -LiteralPath $portableFiles `
    -Pattern $absolutePathPatterns | Select-Object -First 1
if ($remainingAbsolutePath) {
    throw "生成配置仍包含仓库绝对路径：$($remainingAbsolutePath.Path):$($remainingAbsolutePath.LineNumber)"
}

Write-Host ""
Write-Host "✅ 所有路径修正完成！可以运行 idf.py build 了" -ForegroundColor Green

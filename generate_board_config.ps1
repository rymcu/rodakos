[CmdletBinding()]
param(
    [string]$Board = "rymcu_bigsmart"
)

$ErrorActionPreference = "Stop"

if (-not $env:IDF_PATH) {
    throw "未检测到 ESP-IDF 环境，请先执行 . .\activate_idf.ps1"
}
if ($Board -ne "rymcu_bigsmart") {
    throw "RodakOS 仅支持 rymcu_bigsmart，不能生成板型：$Board"
}

$repoRoot = $PSScriptRoot
$boardManagerRoot = Join-Path $repoRoot "components/esp_board_manager"
$generatedCmake = Join-Path $repoRoot "components/gen_bmgr_codes/CMakeLists.txt"
$fixPaths = Join-Path $repoRoot "fix_gen_paths.ps1"
if (-not (Test-Path -LiteralPath $boardManagerRoot) -or
    -not (Test-Path -LiteralPath $fixPaths)) {
    throw "Board Manager 或生成路径修复脚本缺失"
}

$originalExtraActionsPath = $env:IDF_EXTRA_ACTIONS_PATH
$extraActionPaths = @($originalExtraActionsPath -split ';') |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
if ($extraActionPaths -notcontains $boardManagerRoot) {
    $env:IDF_EXTRA_ACTIONS_PATH = (@($boardManagerRoot) + $extraActionPaths) -join ';'
}
$passes = if (Test-Path -LiteralPath $generatedCmake) { 1 } else { 2 }

Push-Location $repoRoot
try {
    for ($pass = 1; $pass -le $passes; ++$pass) {
        Write-Host "Board Manager generation pass $pass/$passes..." -ForegroundColor Yellow
        & idf.py bmgr -b $Board
        if ($LASTEXITCODE -ne 0) {
            throw "Board Manager 配置生成失败（pass $pass/$passes）"
        }
        & $fixPaths
        if ($LASTEXITCODE -ne 0) {
            throw "Board Manager 生成路径修复失败（pass $pass/$passes）"
        }
    }

    & idf.py reconfigure
    if ($LASTEXITCODE -ne 0) {
        throw "Board Manager 生成后的项目重新配置失败"
    }
} finally {
    Pop-Location
    $env:IDF_EXTRA_ACTIONS_PATH = $originalExtraActionsPath
}

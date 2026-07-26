[CmdletBinding()]
param(
    [switch]$SkipCompiler
)

$ErrorActionPreference = "Stop"

if (-not $env:IDF_PATH -or -not (Test-Path -LiteralPath $env:IDF_PATH)) {
    throw "未检测到 ESP-IDF 环境，请先执行 . .\activate_idf.ps1"
}

$versionFile = Join-Path $env:IDF_PATH "tools/cmake/version.cmake"
$toolsManifest = Join-Path $env:IDF_PATH "tools/tools.json"
if (-not (Test-Path -LiteralPath $versionFile) -or
    -not (Test-Path -LiteralPath $toolsManifest)) {
    throw "当前 IDF_PATH 不是完整的 ESP-IDF 安装：$env:IDF_PATH"
}

$versionParts = @{}
foreach ($part in @('MAJOR', 'MINOR', 'PATCH')) {
    $line = Select-String -LiteralPath $versionFile `
        -Pattern "^set\(IDF_VERSION_${part}\s+(\d+)\)" | Select-Object -First 1
    if (-not $line) {
        throw "无法从 ESP-IDF version.cmake 读取 $part 版本号"
    }
    $versionParts[$part] = [int]$line.Matches[0].Groups[1].Value
}
$actualVersion = "$($versionParts.MAJOR).$($versionParts.MINOR).$($versionParts.PATCH)"
if ($actualVersion -ne '6.0.2') {
    throw "RodakOS 当前构建基线要求 ESP-IDF 6.0.2，实际为 $actualVersion：$env:IDF_PATH"
}

if (-not $SkipCompiler) {
    $manifest = Get-Content -Raw -LiteralPath $toolsManifest | ConvertFrom-Json
    $tool = $manifest.tools | Where-Object { $_.name -eq 'xtensa-esp-elf' } |
        Select-Object -First 1
    $recommended = $tool.versions | Where-Object { $_.status -eq 'recommended' } |
        Select-Object -First 1
    if (-not $recommended) {
        throw "无法从 ESP-IDF tools.json 读取推荐的 Xtensa GCC 版本"
    }
    $compiler = Get-Command xtensa-esp32s3-elf-gcc -ErrorAction SilentlyContinue
    $expectedRoot = Join-Path (Join-Path $env:IDF_TOOLS_PATH 'xtensa-esp-elf') $recommended.name
    if (-not $compiler -or
        -not $compiler.Source.StartsWith($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        $actual = if ($compiler) { $compiler.Source } else { '(not found)' }
        throw "Xtensa GCC 版本不匹配，期望 $expectedRoot，实际 $actual；请重新执行 . .\activate_idf.ps1"
    }
}

Write-Host "ESP-IDF 6.0.2 environment verified: $env:IDF_PATH" -ForegroundColor Green

# RodakOS - Local ESP-IDF environment activator
#
# Auto-detects installed ESP-IDF versions and activates the toolchain in the
# current PowerShell session. Mirrors the official `Microsoft.vX.Y.Z.PowerShell_profile.ps1`
# layout but scans the local machine for any installed version/tooling instead
# of hardcoding paths.
#
# Usage (from any PowerShell):
#   . .\activate_idf.ps1                 # use highest installed version
#   . .\activate_idf.ps1 -Version v5.4.2 # pin a specific version
#   . .\activate_idf.ps1 -List           # show available versions
#
# After activation, `idf.py` and friends are available, and `$env:IDF_PATH` is
# set so the project scripts (build_rodakos.ps1, flash_and_test.ps1, ...) work.
#
# Detection sources (first match wins):
#   1. -Version parameter
#   2. C:\Espressif\tools\eim_idf.json   (eim's installed-versions manifest)
#   3. C:\esp\<vX.Y.Z>\esp-idf\         (default ESP-IDF install root)
#   4. IDF_PATH environment variable    (already activated)

[CmdletBinding()]
param(
    [string]$Version,
    [switch]$List
)

$ErrorActionPreference = "Stop"

$EspRootCandidates = @("C:\esp", "D:\esp", "$env:USERPROFILE\esp")
$ToolsRootCandidates = @("C:\Espressif\tools", "$env:USERPROFILE\Espressif\tools")

# ---------- Version detection ----------

function Find-InstalledIdfVersions {
    foreach ($root in $EspRootCandidates) {
        if (-not (Test-Path $root)) { continue }
        Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^v\d+\.\d+\.\d+$' -and (Test-Path (Join-Path $_.FullName 'esp-idf')) } |
            ForEach-Object {
                [PSCustomObject]@{
                    Version = $_.Name
                    IdfPath = (Join-Path $_.FullName 'esp-idf')
                    Source  = "scan:$root"
                }
            }
    }
}

function Find-EimInstalledVersions {
    $eimJson = "C:\Espressif\tools\eim_idf.json"
    if (-not (Test-Path $eimJson)) { return @() }
    try {
        $data = Get-Content $eimJson -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        $selected = $data.idfSelectedId
        foreach ($idf in $data.idfInstalled) {
            [PSCustomObject]@{
                Version    = $idf.name
                IdfPath    = $idf.path
                PythonVenv = $idf.python
                ToolsPath  = $idf.idfToolsPath
                Selected   = ($idf.id -eq $selected)
                Source     = "eim"
            }
        }
    } catch { return @() }
}

function Resolve-PythonVenv {
    # Accepts either a venv root (contains Scripts\python.exe) or an explicit
    # python.exe path (eim's eim_idf.json `python` field). Always returns the
    # venv root directory.
    param([string]$Hint)
    if (-not $Hint) { return $null }
    if (Test-Path $Hint -PathType Leaf) { return Split-Path -Parent (Split-Path -Parent $Hint) }
    if (Test-Path (Join-Path $Hint "Scripts\python.exe")) { return $Hint }
    return $null
}

function Find-PythonVenvForVersion {
    param([string]$VersionName, [string]$ToolsRoot)
    $candidates = @(
        (Join-Path $ToolsRoot "python\$VersionName\venv"),
        (Join-Path $ToolsRoot "python\$VersionName")
    )
    foreach ($c in $candidates) {
        $pythonExe = Join-Path $c "Scripts\python.exe"
        if (Test-Path $pythonExe) { return $c }
    }
    return $null
}

function Resolve-ToolsRoot {
    param([string]$IdfPath)
    $parent = Split-Path -Parent $IdfPath
    $grandparent = Split-Path -Parent $parent
    # Default ESP-IDF layout: C:\Espressif\tools  /  C:\esp\vX.Y.Z\esp-idf
    # Tools live next to ESP-IDF's grandparent via the standard install.
    foreach ($root in $ToolsRootCandidates) {
        if (Test-Path $root) { return $root }
    }
    # Fallback: walk up from IDF_PATH looking for a sibling 'tools' directory.
    $candidate = Join-Path $grandparent "Espressif\tools"
    if (Test-Path $candidate) { return $candidate }
    return $null
}

function Resolve-EspRomElfDir {
    param([string]$ToolsRoot)
    if (-not (Test-Path $ToolsRoot)) { return $null }
    $dir = Get-ChildItem -Path (Join-Path $ToolsRoot "esp-rom-elfs") -Directory -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($dir) { return $dir.FullName }
    return $null
}

function Resolve-OpenOcdScripts {
    param([string]$ToolsRoot)
    if (-not (Test-Path $ToolsRoot)) { return $null }
    $openocdRoot = Get-ChildItem -Path (Join-Path $ToolsRoot "openocd-esp32") -Directory -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($openocdRoot) {
        $scripts = Join-Path $openocdRoot.FullName "share\openocd\scripts"
        if (Test-Path $scripts) { return $scripts }
        $bin = Join-Path $openocdRoot.FullName "bin"
        if (Test-Path $bin) { return $openocdRoot.FullName }
    }
    return $null
}

function Get-IdfVersionFromPath {
    param([string]$IdfPath)
    $versionFile = Join-Path $IdfPath "tools\cmake\version.cmake"
    if (-not (Test-Path $versionFile)) { return $null }
    $major = $null; $minor = $null; $patch = $null
    Get-Content $versionFile | ForEach-Object {
        if ($_ -match '^set\(IDF_VERSION_MAJOR\s+(\d+)\)') { $major = $matches[1] }
        elseif ($_ -match '^set\(IDF_VERSION_MINOR\s+(\d+)\)') { $minor = $matches[1] }
        elseif ($_ -match '^set\(IDF_VERSION_PATCH\s+(\d+)\)') { $patch = $matches[1] }
    }
    if ($major -and $minor -and $patch) { return "$major.$minor.$patch" }
    return $null
}

# ---------- Discovery ----------

$eimVersions = Find-EimInstalledVersions
$scanVersions = Find-InstalledIdfVersions

# Merge: prefer eim metadata (it has Python venv + tools root); backfill with scan.
$allVersions = @()
foreach ($v in $eimVersions) { $allVersions += $v }
foreach ($v in $scanVersions) {
    if (-not ($allVersions | Where-Object { $_.Version -eq $v.Version })) {
        $allVersions += $v
    }
}

# Sort by version descending.
$allVersions = $allVersions | Sort-Object { [version]($_.Version.TrimStart('v')) } -Descending

if ($List) {
    Write-Host "Detected ESP-IDF installations:" -ForegroundColor Cyan
    if ($allVersions.Count -eq 0) {
        Write-Host "  (none)" -ForegroundColor Yellow
    } else {
        foreach ($v in $allVersions) {
            $marker = if ($v.Selected) { " [selected by eim]" } else { "" }
            $exists = if (Test-Path $v.IdfPath) { "OK" } else { "MISSING" }
            Write-Host ("  {0,-10}  {1,-8}  {2}{3}" -f $v.Version, $exists, $v.IdfPath, $marker)
        }
    }
    return
}

# ---------- Selection ----------

$selected = $null
if ($Version) {
    $selected = $allVersions | Where-Object { $_.Version -eq $Version -or $_.Version -eq "v$Version" } | Select-Object -First 1
    if (-not $selected) {
        Write-Error "Requested ESP-IDF version '$Version' not found. Use -List to see installed versions."
    }
} elseif ($env:IDF_PATH -and (Test-Path $env:IDF_PATH)) {
    $selected = [PSCustomObject]@{
        Version = "v$(Get-IdfVersionFromPath $env:IDF_PATH)"
        IdfPath = $env:IDF_PATH
        PythonVenv = $null
        ToolsPath  = $null
        Source     = "env"
    }
} else {
    $selected = $allVersions | Where-Object { $_.Selected } | Select-Object -First 1
    if (-not $selected) { $selected = $allVersions | Select-Object -First 1 }
}

if (-not $selected) {
    Write-Host "❌ No ESP-IDF installation detected." -ForegroundColor Red
    Write-Host ""
    Write-Host "Searched:" -ForegroundColor Yellow
    foreach ($r in $EspRootCandidates) { Write-Host "  - $r" }
    Write-Host ""
    Write-Host "Install one of:" -ForegroundColor Yellow
    Write-Host "  - Via eim:  https://github.com/espressif/idf-importer" -ForegroundColor White
    Write-Host "  - Manually: clone esp-idf into C:\esp\vX.Y.Z\esp-idf" -ForegroundColor White
    Write-Host ""
    Write-Host "Or pass -Version after installing:" -ForegroundColor Yellow
    Write-Host "  . .\activate_idf.ps1 -Version v5.5.4" -ForegroundColor White
    return
}

# ---------- Path resolution ----------

$idfPath = $selected.IdfPath
if (-not (Test-Path $idfPath)) {
    Write-Error "ESP-IDF directory does not exist: $idfPath"
}

$versionActual = Get-IdfVersionFromPath $idfPath
if (-not $versionActual) {
    Write-Warning "Could not parse IDF version from $idfPath\tools\cmake\version.cmake"
    $versionActual = $selected.Version.TrimStart('v')
}

$toolsRoot = $selected.ToolsPath
if (-not $toolsRoot) { $toolsRoot = Resolve-ToolsRoot -IdfPath $idfPath }

$pythonVenv = Resolve-PythonVenv -Hint $selected.PythonVenv
if (-not $pythonVenv -and $toolsRoot) {
    $pythonVenv = Find-PythonVenvForVersion -VersionName $selected.Version -ToolsRoot $toolsRoot
}
if (-not $pythonVenv) {
    Write-Error "Could not locate Python venv for $($selected.Version). Check $toolsRoot\python\$($selected.Version)\venv\Scripts\python.exe"
}

$pythonExe = Join-Path $pythonVenv "Scripts\python.exe"
if (-not (Test-Path $pythonExe)) {
    Write-Error "Python not found at $pythonExe"
}

$espRomElfDir = Resolve-EspRomElfDir -ToolsRoot $toolsRoot
$openocdScripts = Resolve-OpenOcdScripts -ToolsRoot $toolsRoot

# ---------- Build PATH ----------

$pathParts = @()
if ($toolsRoot -and (Test-Path $toolsRoot)) {
    $toolDirs = @(
        "ccache", "cmake", "dfu-util", "esp-clang", "esp-rom-elfs",
        "esp32ulp-elf", "idf-exe", "ninja", "openocd-esp32",
        "qemu-xtensa", "riscv32-esp-elf", "xtensa-esp-elf", "xtensa-esp-elf-gdb",
        "esp-clang-libs"
    )
    foreach ($tool in $toolDirs) {
        $toolRootPath = Join-Path $toolsRoot $tool
        if (-not (Test-Path $toolRootPath)) { continue }
        # Each tool is a directory containing a versioned subdirectory.
        Get-ChildItem -Path $toolRootPath -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            $binDir = Join-Path $_.FullName "bin"
            if (Test-Path $binDir) { $pathParts += $binDir }
            # Some tools (e.g. esp32ulp-elf, xtensa-esp-elf) have nested bin dirs.
            Get-ChildItem -Path $_.FullName -Directory -ErrorAction SilentlyContinue | ForEach-Object {
                $nestedBin = Join-Path $_.FullName "bin"
                if (Test-Path $nestedBin) { $pathParts += $nestedBin }
            }
            # Top-level fallback (e.g. esp-rom-elfs has no bin but its dir is on PATH).
            if (-not (Test-Path $binDir) -and $tool -eq "esp-rom-elfs") {
                $pathParts += $_.FullName
            }
        }
    }
    $pathParts += (Join-Path $pythonVenv "Scripts")
}

$pathParts += $idfPath
$pathParts += $toolsRoot
$pathParts += $env:PATH

# ---------- Environment variables ----------

$env:IDF_PATH = $idfPath
$env:IDF_TOOLS_PATH = $toolsRoot
$env:IDF_PYTHON_ENV_PATH = $pythonVenv
$env:IDF_COMPONENT_LOCAL_STORAGE_URL = "file://$toolsRoot"
$env:ESP_IDF_VERSION = $versionActual
if ($espRomElfDir)    { $env:ESP_ROM_ELF_DIR    = $espRomElfDir }
if ($openocdScripts)  { $env:OPENOCD_SCRIPTS    = $openocdScripts }
$env:PATH = ($pathParts -join ";")

# ---------- Shim functions ----------

function global:Invoke-idfpy { & $pythonExe (Join-Path $idfPath "tools\idf.py") @args }
function global:esptool.py  { & $pythonExe (Join-Path $idfPath "components\esptool_py\esptool\esptool.py") @args }
function global:espefuse.py { & $pythonExe (Join-Path $idfPath "components\esptool_py\esptool\espefuse.py") @args }
function global:espsecure.py{ & $pythonExe (Join-Path $idfPath "components\esptool_py\esptool\espsecure.py") @args }
function global:otatool.py  { & $pythonExe (Join-Path $idfPath "components\app_update\otatool.py") @args }
function global:parttool.py { & $pythonExe (Join-Path $idfPath "components\partition_table\parttool.py") @args }

New-Alias -Name idf.py -Value Invoke-idfpy -Force -Scope Global

# ---------- Activate venv ----------

$activatePs1 = Join-Path $pythonVenv "Scripts\Activate.ps1"
if (Test-Path $activatePs1) {
    . $activatePs1
} else {
    $env:VIRTUAL_ENV = $pythonVenv
}

# ---------- Banner ----------

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "RodakOS ESP-IDF Environment" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "ESP-IDF version : $versionActual  ($($selected.Version))" -ForegroundColor White
Write-Host "IDF_PATH        : $env:IDF_PATH" -ForegroundColor White
Write-Host "IDF_TOOLS_PATH  : $env:IDF_TOOLS_PATH" -ForegroundColor White
Write-Host "Python venv     : $env:IDF_PYTHON_ENV_PATH" -ForegroundColor White
Write-Host "Source          : $($selected.Source)" -ForegroundColor Gray
Write-Host ""
Write-Host "Available commands: idf.py, esptool.py, espefuse.py, espsecure.py," -ForegroundColor White
Write-Host "                   otatool.py, parttool.py" -ForegroundColor White
Write-Host ""
Write-Host "You can now run project scripts (build_rodakos.ps1, flash_and_test.ps1, ...) directly." -ForegroundColor Green
Write-Host ""
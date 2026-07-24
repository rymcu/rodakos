# RodakOS - esp-brookesia HAL 烧录和测试脚本
#
# 参考: Espressif esp-brookesia HAL (https://github.com/espressif/esp-brookesia)
# 功能: 检查固件大小、烧录到设备、启动串口监视器
#
param(
    [string]$Port = "COM3",
    [string]$MergedImage = "",
    [switch]$Erase,
    [ValidateRange(5, 120)]
    [int]$CaptureSeconds = 45,
    [switch]$NoMonitor
)

# 检查是否已在 ESP-IDF 环境中
if (-not $env:IDF_PATH) {
    Write-Host "❌ 未检测到 ESP-IDF 环境" -ForegroundColor Red
    Write-Host "请先激活 ESP-IDF 环境，例如：" -ForegroundColor Yellow
    Write-Host "  - 运行 ESP-IDF PowerShell 快捷方式" -ForegroundColor White
    Write-Host "  - 或执行 export.ps1 (通常在 esp-idf 目录下)" -ForegroundColor White
    exit 1
}

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "RodakOS 烧录和测试" -ForegroundColor Cyan
Write-Host "基于 esp-brookesia HAL" -ForegroundColor Gray
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 查找 Recovery 合并镜像；禁止使用 idf.py flash 写入较小的 factory 分区。
$mergedImageWasExplicit = -not [string]::IsNullOrWhiteSpace($MergedImage)
if (-not $MergedImage) {
    $packageRoot = Join-Path $PSScriptRoot "build/packages/ota"
    $latestPackage = Get-ChildItem $packageRoot -Filter "rodakos_sd_recovery_merged.bin" -Recurse -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($latestPackage) {
        $MergedImage = $latestPackage.FullName
    }
}
if (-not $MergedImage -or -not (Test-Path -LiteralPath $MergedImage)) {
    Write-Host "❌ 未找到 Recovery 合并镜像，请先运行 .\build_ota_bundle.ps1" -ForegroundColor Red
    exit 1
}
$MergedImage = (Resolve-Path -LiteralPath $MergedImage).Path

$captureScript = Join-Path $PSScriptRoot "tools/capture_first_boot.py"
if (-not (Test-Path -LiteralPath $captureScript)) {
    Write-Host "❌ 缺少首次启动采集器：$captureScript" -ForegroundColor Red
    exit 1
}
python -c "import serial"
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ 当前 Python 环境缺少 pyserial，请先激活项目 ESP-IDF 环境" -ForegroundColor Red
    exit 1
}
$bootLogDirectory = Join-Path $PSScriptRoot "build/logs"
try {
    New-Item -ItemType Directory -Force -Path $bootLogDirectory | Out-Null
    $writeProbe = Join-Path $bootLogDirectory (".write-probe-{0}.tmp" -f $PID)
    [System.IO.File]::WriteAllText($writeProbe, "ok")
    Remove-Item -LiteralPath $writeProbe -Force
} catch {
    Write-Host "❌ 启动日志目录不可写：$bootLogDirectory" -ForegroundColor Red
    exit 1
}
$bootLog = Join-Path $bootLogDirectory ("first-boot-{0}.log" -f (Get-Date -Format "yyyyMMdd-HHmmss"))

Write-Host "[1/4] 检查固件大小..." -ForegroundColor Yellow
$packageDirectory = Split-Path -Parent $MergedImage
$imageSize = (Get-Item -LiteralPath $MergedImage).Length
Write-Host "  合并镜像: $MergedImage" -ForegroundColor White
Write-Host "  镜像大小: $([math]::Round($imageSize/1MB, 2)) MiB" -ForegroundColor White
if ($imageSize -ne 16MB) {
    Write-Host "  ❌ Recovery 合并镜像必须恰好为 16 MiB" -ForegroundColor Red
    exit 1
}
$manifestPath = Join-Path $packageDirectory "manifest.json"
if (-not (Test-Path -LiteralPath $manifestPath)) {
    Write-Host "  ❌ Recovery 合并镜像缺少同目录 manifest.json" -ForegroundColor Red
    exit 1
}
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$imageMetadata = $manifest.firstFlashImage
$mainMetadata = $manifest
$recoveryMetadata = $manifest.recoveryImage
$otaDataMetadata = $manifest.otaDataImage
$appPartitionMetadata = $manifest.appPartition
$recoveryPartitionMetadata = $manifest.recoveryPartition
$otaDataPartitionMetadata = $manifest.otaDataPartition
$partitionTableMetadata = $manifest.partitionTableChecksum
if ($manifest.protocolVersion -ne 2 -or $manifest.otaJournalSchemaVersion -ne 1 -or
    $null -eq $imageMetadata -or $null -eq $recoveryMetadata -or
    $null -eq $otaDataMetadata -or $null -eq $appPartitionMetadata -or
    $null -eq $recoveryPartitionMetadata -or $null -eq $otaDataPartitionMetadata -or
    $null -eq $partitionTableMetadata) {
    Write-Host "  ❌ manifest 缺少当前 Recovery 刷写流程所需的元数据" -ForegroundColor Red
    exit 1
}

$mainImage = Join-Path $packageDirectory ([string]$mainMetadata.fileName)
$recoveryImage = Join-Path $packageDirectory ([string]$recoveryMetadata.fileName)
$otaDataImage = Join-Path $packageDirectory ([string]$otaDataMetadata.fileName)
$partitionTableImage = Join-Path $packageDirectory "partition-table.bin"
foreach ($artifact in @($mainImage, $recoveryImage, $otaDataImage, $partitionTableImage)) {
    if (-not (Test-Path -LiteralPath $artifact)) {
        Write-Host "  ❌ 缺少包内刷写产物：$artifact" -ForegroundColor Red
        exit 1
    }
}

$expectedHash = [string]$imageMetadata.checksumValue
$actualHash = (Get-FileHash -LiteralPath $MergedImage -Algorithm SHA256).Hash.ToLowerInvariant()
if ([string]$imageMetadata.fileName -ne (Split-Path -Leaf $MergedImage) -or
    [long]$imageMetadata.fileSize -ne $imageSize -or
    [string]$imageMetadata.checksumType -ne "sha256" -or
    $expectedHash -notmatch "^[0-9a-fA-F]{64}$" -or
    $actualHash -ne $expectedHash.ToLowerInvariant()) {
    Write-Host "  ❌ Recovery 合并镜像与 manifest 不一致" -ForegroundColor Red
    exit 1
}

$mainSize = (Get-Item -LiteralPath $mainImage).Length
$mainHash = (Get-FileHash -LiteralPath $mainImage -Algorithm SHA256).Hash.ToLowerInvariant()
$recoverySize = (Get-Item -LiteralPath $recoveryImage).Length
$recoveryHash = (Get-FileHash -LiteralPath $recoveryImage -Algorithm SHA256).Hash.ToLowerInvariant()
$otaDataSize = (Get-Item -LiteralPath $otaDataImage).Length
$otaDataHash = (Get-FileHash -LiteralPath $otaDataImage -Algorithm SHA256).Hash.ToLowerInvariant()
$partitionTableHash = (Get-FileHash -LiteralPath $partitionTableImage -Algorithm SHA256).Hash.ToLowerInvariant()
if ($mainSize -ne [long]$mainMetadata.fileSize -or
    $mainHash -ne ([string]$mainMetadata.checksumValue).ToLowerInvariant() -or
    $recoverySize -ne [long]$recoveryMetadata.fileSize -or
    $recoveryHash -ne ([string]$recoveryMetadata.checksumValue).ToLowerInvariant() -or
    $otaDataSize -ne [long]$otaDataMetadata.fileSize -or
    $otaDataHash -ne ([string]$otaDataMetadata.checksumValue).ToLowerInvariant() -or
    $partitionTableHash -ne ([string]$partitionTableMetadata.checksumValue).ToLowerInvariant()) {
    Write-Host "  ❌ 包内独立镜像与 manifest 不一致" -ForegroundColor Red
    exit 1
}

$appOffset = [string]$appPartitionMetadata.offset
$appSizeText = [string]$appPartitionMetadata.size
$recoveryOffset = [string]$recoveryPartitionMetadata.offset
$recoverySizeText = [string]$recoveryPartitionMetadata.size
$otaDataOffset = [string]$otaDataPartitionMetadata.offset
$otaDataSizeText = [string]$otaDataPartitionMetadata.size
foreach ($hexValue in @($appOffset, $appSizeText, $recoveryOffset, $recoverySizeText,
                         $otaDataOffset, $otaDataSizeText)) {
    if ($hexValue -notmatch '^0x[0-9a-fA-F]+$') {
        Write-Host "  ❌ manifest 包含无效的分区值：$hexValue" -ForegroundColor Red
        exit 1
    }
}
$appPartitionSize = [Convert]::ToInt64($appSizeText.Substring(2), 16)
$otaDataPartitionSize = [Convert]::ToInt64($otaDataSizeText.Substring(2), 16)
if ([string]$appPartitionMetadata.label -ne "app" -or
    [string]$recoveryPartitionMetadata.label -ne "recovery" -or
    [string]$otaDataPartitionMetadata.label -ne "otadata" -or
    $appOffset -ne "0x2a0000" -or $appSizeText -ne "0xd50000" -or
    $recoveryOffset -ne "0x20000" -or $recoverySizeText -ne "0x280000" -or
    $otaDataOffset -ne "0xf000" -or $otaDataSizeText -ne "0x2000" -or
    $mainSize -gt $appPartitionSize -or $otaDataSize -ne $otaDataPartitionSize) {
    Write-Host "  ❌ 包内镜像与目标分区不兼容" -ForegroundColor Red
    exit 1
}

if (-not $mergedImageWasExplicit) {
    $currentBuildImage = Join-Path $PSScriptRoot "build/rodakos.bin"
    if (Test-Path -LiteralPath $currentBuildImage) {
        $currentBuildHash = (Get-FileHash -LiteralPath $currentBuildImage -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($currentBuildHash -ne $mainHash) {
            Write-Host "  ❌ 最新 OTA 包不对应当前 build/rodakos.bin，请重新运行 .\build_ota_bundle.ps1" -ForegroundColor Red
            exit 1
        }
    }
}

Write-Host "  合并镜像 SHA-256: $actualHash" -ForegroundColor White
Write-Host "  主应用 SHA-256: $mainHash" -ForegroundColor White
Write-Host ""

Write-Host "[2/4] 烧录到设备 ($Port)..." -ForegroundColor Yellow
Write-Host "  按 Ctrl+C 可中断烧录" -ForegroundColor Gray
Write-Host ""

if ($Erase) {
    Write-Host "  首次分区迁移：擦除整片 Flash，设备设置和 OTA 状态将被清空" -ForegroundColor Yellow
    python -m esptool --chip esp32s3 -p $Port --after no_reset erase_flash
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ❌ 擦除失败" -ForegroundColor Red
        exit 1
    }
    python -m esptool --chip esp32s3 -p $Port -b 460800 `
        --before no_reset --after no_reset write_flash 0x0 $MergedImage
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ❌ 烧录失败" -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "  增量刷新：保留默认 NVS、OTA journal、Recovery 和 coredump" -ForegroundColor Yellow
    Write-Host "  仅写入 $otaDataOffset (otadata) 与 $appOffset (ota_0)" -ForegroundColor White

    $devicePartitionDump = Join-Path $bootLogDirectory (".partition-{0}.bin" -f $PID)
    $deviceRecoveryDump = Join-Path $bootLogDirectory (".recovery-{0}.bin" -f $PID)
    $writeStarted = $false
    $incrementalError = $null
    try {
        $partitionTableSize = (Get-Item -LiteralPath $partitionTableImage).Length
        python -m esptool --chip esp32s3 -p $Port -b 460800 `
            --before default_reset --after no_reset read_flash `
            0x8000 $partitionTableSize $devicePartitionDump
        if ($LASTEXITCODE -ne 0) {
            throw "无法读取设备分区表"
        }

        python -m esptool --chip esp32s3 -p $Port -b 460800 `
            --before no_reset --after no_reset read_flash `
            $recoveryOffset $recoverySize $deviceRecoveryDump
        if ($LASTEXITCODE -ne 0) {
            throw "无法读取设备 Recovery"
        }

        $devicePartitionHash = (Get-FileHash -LiteralPath $devicePartitionDump -Algorithm SHA256).Hash.ToLowerInvariant()
        $deviceRecoveryHash = (Get-FileHash -LiteralPath $deviceRecoveryDump -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($devicePartitionHash -ne $partitionTableHash -or
            $deviceRecoveryHash -ne $recoveryHash) {
            throw "设备分区表或 Recovery 与当前包不一致；首次迁移请显式使用 -Erase"
        }

        $writeStarted = $true
        python -m esptool --chip esp32s3 -p $Port -b 460800 `
            --before no_reset --after no_reset write_flash `
            $otaDataOffset $otaDataImage $appOffset $mainImage
        if ($LASTEXITCODE -ne 0) {
            throw "增量烧录失败"
        }
    } catch {
        $incrementalError = $_
    } finally {
        Remove-Item -LiteralPath $devicePartitionDump, $deviceRecoveryDump `
            -Force -ErrorAction SilentlyContinue
    }

    if ($null -ne $incrementalError) {
        Write-Host "  ❌ $($incrementalError.Exception.Message)" -ForegroundColor Red
        if (-not $writeStarted) {
            python -m esptool --chip esp32s3 -p $Port `
                --before no_reset --after hard_reset flash_id | Out-Null
        }
        exit 1
    }
}
Write-Host "  ✅ 烧录完成" -ForegroundColor Green
Write-Host ""

Write-Host "[3/4] 采集并验证首次启动..." -ForegroundColor Yellow
Write-Host "  启动日志: $bootLog" -ForegroundColor White
python $captureScript --port $Port --baud 115200 --timeout $CaptureSeconds --log $bootLog
if ($LASTEXITCODE -ne 0) {
    Write-Host "  ❌ 首次启动验证失败；为保护 PENDING_VERIFY 镜像，不会启动 monitor 或再次复位" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "  ✅ Recovery、主系统、OTA 确认和 Home 启动均已验证" -ForegroundColor Green
Write-Host ""

if ($NoMonitor) {
    Write-Host "[4/4] 已跳过交互式监视器 (-NoMonitor)" -ForegroundColor Yellow
    exit 0
}

Write-Host "[4/4] 启动串口监视器（不复位设备）..." -ForegroundColor Yellow
Write-Host "  按 Ctrl+] 退出监视器" -ForegroundColor Gray
Write-Host ""
idf.py -p $Port monitor --no-reset

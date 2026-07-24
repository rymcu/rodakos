param(
    [string]$OutputRoot = "build/packages/ota",
    [switch]$RegenerateBoardConfig
)

$ErrorActionPreference = "Stop"

function Get-PartitionLayout {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PartTool,
        [Parameter(Mandatory = $true)]
        [string]$PartitionTable,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $output = & python $PartTool --partition-table-file $PartitionTable `
        get_partition_info --partition-name $Name --info offset size
    if ($LASTEXITCODE -ne 0) {
        throw "无法读取分区 $Name 的布局"
    }
    $values = (($output | Out-String).Trim() -split "\s+")
    if ($values.Count -ne 2 -or $values[0] -notmatch '^0x[0-9a-fA-F]+$' -or
        $values[1] -notmatch '^0x[0-9a-fA-F]+$') {
        throw "分区 $Name 的布局输出无效：$output"
    }
    return [pscustomobject]@{
        OffsetText = $values[0].ToLowerInvariant()
        SizeText = $values[1].ToLowerInvariant()
        Offset = [Convert]::ToInt64($values[0].Substring(2), 16)
        Size = [Convert]::ToInt64($values[1].Substring(2), 16)
    }
}

if (-not $env:IDF_PATH) {
    throw "未检测到 ESP-IDF 环境，请先执行 . .\activate_idf.ps1"
}

$repoRoot = $PSScriptRoot
$outputRootPath = Join-Path $repoRoot $OutputRoot
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$packageDir = Join-Path $outputRootPath $timestamp

Push-Location $repoRoot
try {
    $generatedBoardCmake = Join-Path $repoRoot "components/gen_bmgr_codes/CMakeLists.txt"
    if ($RegenerateBoardConfig -or -not (Test-Path -LiteralPath $generatedBoardCmake)) {
        & idf.py bmgr -b rymcu_bigsmart
        if ($LASTEXITCODE -ne 0) {
            throw "Board Manager 配置生成失败"
        }
        & (Join-Path $repoRoot "fix_gen_paths.ps1")
        if ($LASTEXITCODE -ne 0) {
            throw "Board Manager 生成路径修复失败"
        }
    } else {
        Write-Host "复用现有 Board Manager 生成配置；需要刷新时传入 -RegenerateBoardConfig"
    }

    & idf.py build
    if ($LASTEXITCODE -ne 0) {
        throw "RodakOS 主应用构建失败"
    }

    & idf.py -C recovery build
    if ($LASTEXITCODE -ne 0) {
        throw "RodakOS Recovery 构建失败"
    }

    $mainBin = Join-Path $repoRoot "build/rodakos.bin"
    $recoveryBin = Join-Path $repoRoot "recovery/build/rodakos_recovery.bin"
    $bootloaderBin = Join-Path $repoRoot "recovery/build/bootloader/bootloader.bin"
    $partitionBin = Join-Path $repoRoot "recovery/build/partition_table/partition-table.bin"
    $mainPartitionBin = Join-Path $repoRoot "build/partition_table/partition-table.bin"
    $otaDataBin = Join-Path $repoRoot "recovery/build/ota_data_initial.bin"
    $recoveryFlashArgsPath = Join-Path $repoRoot "recovery/build/flasher_args.json"
    $mainFlashArgsPath = Join-Path $repoRoot "build/flasher_args.json"
    $mainProjectDescriptionPath = Join-Path $repoRoot "build/project_description.json"
    $recoveryProjectDescriptionPath = Join-Path $repoRoot "recovery/build/project_description.json"
    $recoverySdkconfigPath = Join-Path $repoRoot "recovery/sdkconfig"
    $journalHeaderPath = Join-Path $repoRoot "components/rodak_ota_state/include/rodak_ota_state.h"
    $partTool = Join-Path $env:IDF_PATH "components/partition_table/parttool.py"

    $requiredFiles = @($mainBin, $recoveryBin, $bootloaderBin, $partitionBin,
                       $mainPartitionBin, $otaDataBin, $recoveryFlashArgsPath,
                       $mainFlashArgsPath, $mainProjectDescriptionPath,
                       $recoveryProjectDescriptionPath, $recoverySdkconfigPath,
                       $journalHeaderPath, $partTool)
    foreach ($file in $requiredFiles) {
        if (-not (Test-Path -LiteralPath $file)) {
            throw "缺少构建产物：$file"
        }
    }

    $appPartition = Get-PartitionLayout -PartTool $partTool `
        -PartitionTable $partitionBin -Name "app"
    $recoveryPartition = Get-PartitionLayout -PartTool $partTool `
        -PartitionTable $partitionBin -Name "recovery"
    $otaDataPartition = Get-PartitionLayout -PartTool $partTool `
        -PartitionTable $partitionBin -Name "otadata"

    $recoveryFlashArgs = Get-Content -Raw -LiteralPath $recoveryFlashArgsPath | ConvertFrom-Json
    $mainFlashArgs = Get-Content -Raw -LiteralPath $mainFlashArgsPath | ConvertFrom-Json
    $mainProject = Get-Content -Raw -LiteralPath $mainProjectDescriptionPath | ConvertFrom-Json
    $recoveryProject = Get-Content -Raw -LiteralPath $recoveryProjectDescriptionPath | ConvertFrom-Json
    if ($mainProject.target -ne "esp32s3" -or $recoveryProject.target -ne "esp32s3") {
        throw "主应用与 Recovery 必须都以 esp32s3 为目标"
    }
    foreach ($setting in @("flash_mode", "flash_freq", "flash_size")) {
        if ($mainFlashArgs.flash_settings.$setting -ne
            $recoveryFlashArgs.flash_settings.$setting) {
            throw "主应用与 Recovery 的 $setting 配置不一致"
        }
    }
    if ($recoveryFlashArgs.flash_settings.flash_size -ne "16MB") {
        throw "首次迁移包必须使用 16MB Flash 配置"
    }
    foreach ($sdkconfigPath in @((Join-Path $repoRoot "sdkconfig"), $recoverySdkconfigPath)) {
        if (-not (Select-String -LiteralPath $sdkconfigPath `
                -Pattern '^CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y$' -Quiet)) {
            throw "构建配置未启用 Bootloader app rollback：$sdkconfigPath"
        }
    }
    $schemaMatch = Select-String -LiteralPath $journalHeaderPath `
        -Pattern 'kOtaJournalSchemaVersion\s*=\s*(\d+)' | Select-Object -First 1
    if ($null -eq $schemaMatch -or $schemaMatch.Matches.Count -eq 0) {
        throw "无法读取 OTA journal schema 版本"
    }
    $journalSchemaVersion = [int]$schemaMatch.Matches[0].Groups[1].Value
    if ($journalSchemaVersion -ne 1) {
        throw "日常 OTA 仅兼容常驻 Recovery journal v1；升级 schema 必须设计新的有线迁移"
    }

    $mainSize = (Get-Item -LiteralPath $mainBin).Length
    $recoverySize = (Get-Item -LiteralPath $recoveryBin).Length
    $otaDataSize = (Get-Item -LiteralPath $otaDataBin).Length
    if ($mainSize -gt $appPartition.Size) {
        throw "主应用大小 $mainSize 超过 app 分区上限 $($appPartition.SizeText)"
    }
    if ($recoverySize -gt $recoveryPartition.Size) {
        throw "Recovery 大小 $recoverySize 超过 recovery 分区上限 $($recoveryPartition.SizeText)"
    }
    if ($otaDataSize -ne $otaDataPartition.Size) {
        throw "OTA data 初始镜像大小 $otaDataSize 与 otadata 分区 $($otaDataPartition.SizeText) 不一致"
    }
    $partitionSha = (Get-FileHash -LiteralPath $partitionBin -Algorithm SHA256).Hash
    $mainPartitionSha = (Get-FileHash -LiteralPath $mainPartitionBin -Algorithm SHA256).Hash
    if ($partitionSha -ne $mainPartitionSha) {
        throw "主应用与 Recovery 使用了不同的分区表"
    }

    & python "$env:IDF_PATH/components/partition_table/check_sizes.py" partition `
        --type app --subtype ota_0 $partitionBin $mainBin
    if ($LASTEXITCODE -ne 0) {
        throw "主应用 ota_0 容量校验失败"
    }
    & python "$env:IDF_PATH/components/partition_table/check_sizes.py" partition `
        --type app --subtype factory $partitionBin $recoveryBin
    if ($LASTEXITCODE -ne 0) {
        throw "Recovery factory 容量校验失败"
    }

    New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
    Copy-Item -LiteralPath $mainBin -Destination (Join-Path $packageDir "rodakos.bin")
    Copy-Item -LiteralPath $recoveryBin -Destination (Join-Path $packageDir "rodakos_recovery.bin")
    Copy-Item -LiteralPath $bootloaderBin -Destination (Join-Path $packageDir "bootloader.bin")
    Copy-Item -LiteralPath $partitionBin -Destination (Join-Path $packageDir "partition-table.bin")
    Copy-Item -LiteralPath $otaDataBin -Destination (Join-Path $packageDir "ota_data_initial.bin")

    $flashMode = [string]$recoveryFlashArgs.flash_settings.flash_mode
    $flashFrequency = [string]$recoveryFlashArgs.flash_settings.flash_freq
    $flashSize = [string]$recoveryFlashArgs.flash_settings.flash_size
    $bootloaderOffset = [string]$recoveryFlashArgs.bootloader.offset
    $partitionTableOffset = [string]$recoveryFlashArgs.'partition-table'.offset

    $mergedBin = Join-Path $packageDir "rodakos_sd_recovery_merged.bin"
    & python -m esptool --chip esp32s3 merge_bin `
        --flash_mode $flashMode `
        --flash_freq $flashFrequency `
        --flash_size $flashSize `
        --fill-flash-size $flashSize `
        -o $mergedBin `
        $bootloaderOffset $bootloaderBin `
        $partitionTableOffset $partitionBin `
        $($otaDataPartition.OffsetText) $otaDataBin `
        $($recoveryPartition.OffsetText) $recoveryBin `
        $($appPartition.OffsetText) $mainBin
    if ($LASTEXITCODE -ne 0) {
        throw "合并首次烧录镜像失败"
    }

    $mergedSize = (Get-Item -LiteralPath $mergedBin).Length
    if ($mergedSize -ne 16MB) {
        throw "首次烧录镜像必须恰好为 16 MiB，实际为 $mergedSize 字节"
    }
    $mainSha256 = (Get-FileHash -LiteralPath $mainBin -Algorithm SHA256).Hash.ToLowerInvariant()
    $recoverySha256 = (Get-FileHash -LiteralPath $recoveryBin -Algorithm SHA256).Hash.ToLowerInvariant()
    $otaDataSha256 = (Get-FileHash -LiteralPath $otaDataBin -Algorithm SHA256).Hash.ToLowerInvariant()
    $mergedSha256 = (Get-FileHash -LiteralPath $mergedBin -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifest = [ordered]@{
        protocolVersion = 2
        otaJournalSchemaVersion = $journalSchemaVersion
        imageType = "app"
        fileName = "rodakos.bin"
        fileSize = $mainSize
        checksumType = "sha256"
        checksumValue = $mainSha256
        appPartition = [ordered]@{
            label = "app"
            offset = $appPartition.OffsetText
            size = $appPartition.SizeText
        }
        recoveryPartition = [ordered]@{
            label = "recovery"
            offset = $recoveryPartition.OffsetText
            size = $recoveryPartition.SizeText
        }
        otaDataPartition = [ordered]@{
            label = "otadata"
            offset = $otaDataPartition.OffsetText
            size = $otaDataPartition.SizeText
        }
        otaDataImage = [ordered]@{
            fileName = "ota_data_initial.bin"
            fileSize = $otaDataSize
            checksumType = "sha256"
            checksumValue = $otaDataSha256
        }
        recoveryImage = [ordered]@{
            fileName = "rodakos_recovery.bin"
            fileSize = $recoverySize
            checksumType = "sha256"
            checksumValue = $recoverySha256
        }
        firstFlashImage = [ordered]@{
            fileName = "rodakos_sd_recovery_merged.bin"
            fileSize = $mergedSize
            checksumType = "sha256"
            checksumValue = $mergedSha256
        }
        partitionTableChecksum = [ordered]@{
            checksumType = "sha256"
            checksumValue = $partitionSha.ToLowerInvariant()
        }
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $packageDir "manifest.json") -Encoding utf8
    @(
        "首次烧录："
        "python -m esptool --chip esp32s3 -p COM3 erase_flash"
        "python -m esptool --chip esp32s3 -p COM3 -b 460800 --before default_reset --after hard_reset write_flash 0x0 rodakos_sd_recovery_merged.bin"
        ""
        "Rodak OTA：仅上传 rodakos.bin，并使用 manifest.json 中的 fileSize/checksumValue。"
    ) | Set-Content -LiteralPath (Join-Path $packageDir "flash_args.txt") -Encoding utf8

    $zipPath = "$packageDir.zip"
    Compress-Archive -Path (Join-Path $packageDir "*") -DestinationPath $zipPath -Force
    Write-Host "OTA 包已生成：$packageDir" -ForegroundColor Green
    Write-Host "首次烧录镜像：$mergedBin" -ForegroundColor Green
    Write-Host "Rodak OTA 应用镜像：$(Join-Path $packageDir 'rodakos.bin')" -ForegroundColor Green
} finally {
    Pop-Location
}

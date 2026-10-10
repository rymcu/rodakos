param(
    [string]$OutputRoot = "build/packages/ota",
    [string]$ImmutableRecoveryPackage = "",
    [switch]$SkipBuild,
    [switch]$AllowHomeHardwareTestPopulation,
    [string]$SigningKeyPath = "",
    [string]$VerificationKeyPath = "",
    [string]$SigningTaskNo = "",
    [string]$SigningVersion = "",
    [switch]$DevelopmentPackage,
    [switch]$AllowReleaseFaultInjection
)

$ErrorActionPreference = "Stop"

foreach ($value in @($SigningKeyPath, $VerificationKeyPath, $SigningTaskNo, $SigningVersion)) {
    if ([string]::IsNullOrWhiteSpace($value)) {
        throw "Signed packages require SigningKeyPath, VerificationKeyPath, SigningTaskNo and SigningVersion"
    }
}
$SigningKeyPath = (Resolve-Path -LiteralPath $SigningKeyPath).Path
$VerificationKeyPath = (Resolve-Path -LiteralPath $VerificationKeyPath).Path

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
$environmentCheck = Join-Path $repoRoot "assert_idf6_environment.ps1"
& $environmentCheck
$outputRootPath = Join-Path $repoRoot $OutputRoot
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$packageDir = Join-Path $outputRootPath $timestamp
$immutableRecoveryPackagePath = $null
if (-not [string]::IsNullOrWhiteSpace($ImmutableRecoveryPackage)) {
    if (-not (Test-Path -LiteralPath $ImmutableRecoveryPackage -PathType Container)) {
        throw "Immutable Recovery 包目录不存在：$ImmutableRecoveryPackage"
    }
    $immutableRecoveryPackagePath = (Resolve-Path -LiteralPath $ImmutableRecoveryPackage).Path
}

function Test-BinaryRegionMatches {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ContainerPath,
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [long]$Offset
    )

    $sourceBytes = [System.IO.File]::ReadAllBytes($FilePath)
    $regionBytes = [byte[]]::new($sourceBytes.Length)
    $stream = [System.IO.File]::OpenRead($ContainerPath)
    try {
        if ($Offset -lt 0 -or $Offset + $sourceBytes.Length -gt $stream.Length) {
            return $false
        }
        $stream.Position = $Offset
        $readTotal = 0
        while ($readTotal -lt $regionBytes.Length) {
            $read = $stream.Read($regionBytes, $readTotal, $regionBytes.Length - $readTotal)
            if ($read -le 0) {
                return $false
            }
            $readTotal += $read
        }
    } finally {
        $stream.Dispose()
    }

    return [System.Linq.Enumerable]::SequenceEqual[byte]($sourceBytes, $regionBytes)
}

function Test-BinaryContainsAscii {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    $binaryText = [System.Text.Encoding]::ASCII.GetString(
        [System.IO.File]::ReadAllBytes($FilePath))
    return $binaryText.IndexOf($Text, [StringComparison]::Ordinal) -ge 0
}

Push-Location $repoRoot
try {
    if ($SkipBuild) {
        Write-Host "跳过构建，使用现有且已验证的主应用与 Recovery 构建产物" -ForegroundColor Yellow
    } else {
        & (Join-Path $repoRoot "generate_board_config.ps1")
        if ($LASTEXITCODE -ne 0) {
            throw "Board Manager 配置生成失败"
        }

        & idf.py "-DRODAK_OTA_PUBLIC_KEY=$VerificationKeyPath" build
        if ($LASTEXITCODE -ne 0) {
            throw "RodakOS 主应用构建失败"
        }

        & idf.py -C recovery "-DRODAK_OTA_PUBLIC_KEY=$VerificationKeyPath" build
        if ($LASTEXITCODE -ne 0) {
            throw "RodakOS Recovery 构建失败"
        }
    }

    $mainBin = Join-Path $repoRoot "build/rodakos.bin"
    $recoveryBin = if ($null -eq $immutableRecoveryPackagePath) {
        Join-Path $repoRoot "recovery/build/rodakos_recovery.bin"
    } else {
        Join-Path $immutableRecoveryPackagePath "rodakos_recovery.bin"
    }
    $bootloaderBin = if ($null -eq $immutableRecoveryPackagePath) {
        Join-Path $repoRoot "recovery/build/bootloader/bootloader.bin"
    } else {
        Join-Path $immutableRecoveryPackagePath "bootloader.bin"
    }
    $partitionBin = if ($null -eq $immutableRecoveryPackagePath) {
        Join-Path $repoRoot "recovery/build/partition_table/partition-table.bin"
    } else {
        Join-Path $immutableRecoveryPackagePath "partition-table.bin"
    }
    $mainPartitionBin = Join-Path $repoRoot "build/partition_table/partition-table.bin"
    $otaDataBin = if ($null -eq $immutableRecoveryPackagePath) {
        Join-Path $repoRoot "recovery/build/ota_data_initial.bin"
    } else {
        Join-Path $immutableRecoveryPackagePath "ota_data_initial.bin"
    }
    $recoveryFlashArgsPath = Join-Path $repoRoot "recovery/build/flasher_args.json"
    $mainFlashArgsPath = Join-Path $repoRoot "build/flasher_args.json"
    $mainProjectDescriptionPath = Join-Path $repoRoot "build/project_description.json"
    $recoveryProjectDescriptionPath = Join-Path $repoRoot "recovery/build/project_description.json"
    $recoverySdkconfigPath = Join-Path $repoRoot "recovery/sdkconfig"
    $journalHeaderPath = Join-Path $repoRoot "components/rodak_ota_state/include/rodak_ota_state.h"
    $partTool = Join-Path $env:IDF_PATH "components/partition_table/parttool.py"

    $requiredFiles = @($mainBin, $recoveryBin, $bootloaderBin, $partitionBin,
                       $mainPartitionBin, $otaDataBin, $mainFlashArgsPath,
                       $mainProjectDescriptionPath, $journalHeaderPath, $partTool)
    $requiredFiles += @($recoveryFlashArgsPath, $recoveryProjectDescriptionPath,
                        $recoverySdkconfigPath)
    if ($null -ne $immutableRecoveryPackagePath) {
        $requiredFiles += @(
            (Join-Path $immutableRecoveryPackagePath "manifest.json"),
            (Join-Path $immutableRecoveryPackagePath "rodakos_sd_recovery_merged.bin")
        )
    }
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

    $mainFlashArgs = Get-Content -Raw -LiteralPath $mainFlashArgsPath | ConvertFrom-Json
    $mainProject = Get-Content -Raw -LiteralPath $mainProjectDescriptionPath | ConvertFrom-Json
    if ($SigningVersion -ne [string]$mainProject.project_version) {
        throw "SigningVersion must match the compiled app version $($mainProject.project_version)"
    }
    $recoveryFlashArgs = Get-Content -Raw -LiteralPath $recoveryFlashArgsPath | ConvertFrom-Json
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
    if (-not (Select-String -LiteralPath $recoverySdkconfigPath `
            -Pattern '^CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y$' -Quiet)) {
        throw "Recovery 构建配置未启用 Bootloader app rollback：$recoverySdkconfigPath"
    }

    $immutableManifest = $null
    if ($null -ne $immutableRecoveryPackagePath) {
        $immutableManifestPath = Join-Path $immutableRecoveryPackagePath "manifest.json"
        $immutableManifest = Get-Content -Raw -LiteralPath $immutableManifestPath | ConvertFrom-Json
        $immutableMerged = Join-Path $immutableRecoveryPackagePath "rodakos_sd_recovery_merged.bin"
        $immutableMergedSize = (Get-Item -LiteralPath $immutableMerged).Length
        $immutableMergedHash =
            (Get-FileHash -LiteralPath $immutableMerged -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($immutableManifest.protocolVersion -ne 2 -or
            $immutableManifest.manifestVersion -ne 2 -or
            $immutableManifest.otaJournalSchemaVersion -ne 1 -or
            [string]$immutableManifest.firstFlashImage.fileName -ne
                "rodakos_sd_recovery_merged.bin" -or
            [string]$immutableManifest.firstFlashImage.checksumType -ne "sha256" -or
            [long]$immutableManifest.firstFlashImage.fileSize -ne 16MB -or
            $immutableMergedSize -ne 16MB -or
            $immutableMergedHash -ne
                ([string]$immutableManifest.firstFlashImage.checksumValue).ToLowerInvariant()) {
            throw "Immutable Recovery 包 manifest 或合并镜像校验失败"
        }
    }
    if ($mainFlashArgs.flash_settings.flash_size -ne "16MB") {
        throw "首次迁移包必须使用 16MB Flash 配置"
    }
    $mainSdkconfigPath = Join-Path $repoRoot "sdkconfig"
    if (-not (Select-String -LiteralPath $mainSdkconfigPath `
            -Pattern '^CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y$' -Quiet)) {
        throw "主应用构建配置未启用 Bootloader app rollback：$mainSdkconfigPath"
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

    $testBuildMarker = "RODAKOS_HOME_HARDWARE_TEST_POPULATION_ACTIVE"
    $homeHardwareTestPopulation = Test-BinaryContainsAscii -FilePath $mainBin -Text $testBuildMarker
    $cmakeCache = Join-Path $repoRoot "build/CMakeCache.txt"
    $cacheRequestsHardwareTestPopulation = $false
    if (Test-Path -LiteralPath $cmakeCache) {
        $cacheRequestsHardwareTestPopulation = $null -ne (Select-String -LiteralPath $cmakeCache `
            -Pattern '^RODAKOS_HOME_HARDWARE_TEST_POPULATION:BOOL=(ON|TRUE|1)$' |
            Select-Object -First 1)
        $faultInjectionCache = Select-String -LiteralPath $cmakeCache `
            -Pattern '^RODAK_OTA_FAULT_INJECTION_PHASE:STRING=.+$' |
            Select-Object -First 1
        $cameraDmaFaultInjectionCache = Select-String -LiteralPath $cmakeCache `
            -Pattern '^RODAKOS_CAMERA_DMA_FORCE_4096:BOOL=(ON|TRUE|1)$' |
            Select-Object -First 1
        $cameraTestPatternCache = Select-String -LiteralPath $cmakeCache `
            -Pattern '^RODAKOS_CAMERA_TEST_PATTERN:BOOL=(ON|TRUE|1)$' |
            Select-Object -First 1
        $cameraSensorDiagnosticsCache = Select-String -LiteralPath $cmakeCache `
            -Pattern '^RODAKOS_CAMERA_SENSOR_DIAGNOSTICS:BOOL=(ON|TRUE|1)$' |
            Select-Object -First 1
        $cameraSignalDiagnosticsCache = Select-String -LiteralPath $cmakeCache `
            -Pattern '^RODAKOS_CAMERA_SIGNAL_DIAGNOSTICS:BOOL=(ON|TRUE|1)$' |
            Select-Object -First 1
        if (($null -ne $faultInjectionCache -or $null -ne $cameraDmaFaultInjectionCache -or
             $null -ne $cameraTestPatternCache -or $null -ne $cameraSensorDiagnosticsCache -or
             $null -ne $cameraSignalDiagnosticsCache) -and
            -not $AllowReleaseFaultInjection) {
            throw "故障注入配置仍启用；关闭测试开关或显式允许测试包后才能打包"
        }
    }
    if ($cacheRequestsHardwareTestPopulation -ne $homeHardwareTestPopulation) {
        throw "Home 硬件测试配置与 rodakos.bin 不一致；请在切换 flavor 后重新运行 idf.py build"
    }
    $recoveryCmakeCachePath = Join-Path $repoRoot "recovery/build/CMakeCache.txt"
    if (Test-Path -LiteralPath $recoveryCmakeCachePath) {
        $recoveryFaultInjectionCache = Select-String -LiteralPath $recoveryCmakeCachePath `
            -Pattern '^RODAK_OTA_FAULT_INJECTION_PHASE:STRING=.+$' |
            Select-Object -First 1
        if ($null -ne $recoveryFaultInjectionCache -and -not $AllowReleaseFaultInjection) {
            throw "Recovery OTA 故障注入配置仍启用；清空 RODAK_OTA_FAULT_INJECTION_PHASE 后才能打包"
        }
    }
    if ($homeHardwareTestPopulation -and -not $AllowHomeHardwareTestPopulation) {
        throw "Home 硬件测试固件需要显式传入 -AllowHomeHardwareTestPopulation 才能打包"
    }
    $buildFlavor = if ($homeHardwareTestPopulation) { "home-hardware-test" } else { "production" }
    $releaseFaultInjection = (Test-BinaryContainsAscii -FilePath $mainBin `
        -Text "RODAKOS_RELEASE_FAULT_INJECTION_ACTIVE") -or `
        (Test-BinaryContainsAscii -FilePath $recoveryBin -Text "RODAKOS_RELEASE_FAULT_INJECTION_ACTIVE")
    if ($releaseFaultInjection) {
        if (-not $DevelopmentPackage -or -not $AllowReleaseFaultInjection) {
            throw "Fault-injection packages require DevelopmentPackage and AllowReleaseFaultInjection"
        }
        $buildFlavor = "release-fault-test"
    }
    $mainImageType = if ($homeHardwareTestPopulation) { "hardware-test" } else { "app" }
    $mainPackageName = if ($homeHardwareTestPopulation) {
        "rodakos_home_hardware_test.bin"
    } else {
        "rodakos.bin"
    }
    if ($releaseFaultInjection) {
        $mainImageType = "hardware-test"
        $mainPackageName = "rodakos_release_test.bin"
    }
    $partitionSha = (Get-FileHash -LiteralPath $partitionBin -Algorithm SHA256).Hash
    $mainPartitionSha = (Get-FileHash -LiteralPath $mainPartitionBin -Algorithm SHA256).Hash
    if ($partitionSha -ne $mainPartitionSha) {
        throw "主应用与 Recovery 使用了不同的分区表"
    }
    if ($null -ne $immutableManifest) {
        $recoveryHash =
            (Get-FileHash -LiteralPath $recoveryBin -Algorithm SHA256).Hash.ToLowerInvariant()
        $otaDataHash =
            (Get-FileHash -LiteralPath $otaDataBin -Algorithm SHA256).Hash.ToLowerInvariant()
        if ([string]$immutableManifest.recoveryImage.fileName -ne "rodakos_recovery.bin" -or
            [long]$immutableManifest.recoveryImage.fileSize -ne $recoverySize -or
            [string]$immutableManifest.recoveryImage.checksumType -ne "sha256" -or
            $recoveryHash -ne
                ([string]$immutableManifest.recoveryImage.checksumValue).ToLowerInvariant() -or
            [string]$immutableManifest.otaDataImage.fileName -ne "ota_data_initial.bin" -or
            [long]$immutableManifest.otaDataImage.fileSize -ne $otaDataSize -or
            [string]$immutableManifest.otaDataImage.checksumType -ne "sha256" -or
            $otaDataHash -ne
                ([string]$immutableManifest.otaDataImage.checksumValue).ToLowerInvariant() -or
            $partitionSha.ToLowerInvariant() -ne
                ([string]$immutableManifest.partitionTableChecksum.checksumValue).ToLowerInvariant() -or
            [string]$immutableManifest.partitionTableChecksum.checksumType -ne "sha256" -or
            [string]$immutableManifest.appPartition.label -ne "app" -or
            [string]$immutableManifest.appPartition.offset -ne $appPartition.OffsetText -or
            [string]$immutableManifest.appPartition.size -ne $appPartition.SizeText -or
            [string]$immutableManifest.recoveryPartition.label -ne "recovery" -or
            [string]$immutableManifest.recoveryPartition.offset -ne
                $recoveryPartition.OffsetText -or
            [string]$immutableManifest.recoveryPartition.size -ne
                $recoveryPartition.SizeText -or
            [string]$immutableManifest.otaDataPartition.label -ne "otadata" -or
            [string]$immutableManifest.otaDataPartition.offset -ne
                $otaDataPartition.OffsetText -or
            [string]$immutableManifest.otaDataPartition.size -ne
                $otaDataPartition.SizeText) {
            throw "Immutable Recovery 包内资产或分区布局与 manifest 不一致"
        }
        if (-not (Test-BinaryRegionMatches -ContainerPath $immutableMerged `
                -FilePath $bootloaderBin -Offset 0) -or
            -not (Test-BinaryRegionMatches -ContainerPath $immutableMerged `
                -FilePath $partitionBin -Offset 0x8000) -or
            -not (Test-BinaryRegionMatches -ContainerPath $immutableMerged `
                -FilePath $otaDataBin -Offset $otaDataPartition.Offset) -or
            -not (Test-BinaryRegionMatches -ContainerPath $immutableMerged `
                -FilePath $recoveryBin -Offset $recoveryPartition.Offset)) {
            throw "Immutable Recovery 包资产与已校验的合并镜像不一致"
        }
        Write-Host "复用已验证的 immutable Recovery 包：$immutableRecoveryPackagePath" `
            -ForegroundColor Yellow
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
    Copy-Item -LiteralPath $mainBin -Destination (Join-Path $packageDir $mainPackageName)
    Copy-Item -LiteralPath $recoveryBin -Destination (Join-Path $packageDir "rodakos_recovery.bin")
    Copy-Item -LiteralPath $bootloaderBin -Destination (Join-Path $packageDir "bootloader.bin")
    Copy-Item -LiteralPath $partitionBin -Destination (Join-Path $packageDir "partition-table.bin")
    Copy-Item -LiteralPath $otaDataBin -Destination (Join-Path $packageDir "ota_data_initial.bin")

    if ($null -eq $immutableManifest) {
        $flashMode = [string]$recoveryFlashArgs.flash_settings.flash_mode
        $flashFrequency = [string]$recoveryFlashArgs.flash_settings.flash_freq
        $flashHeaderSize = [string]$recoveryFlashArgs.flash_settings.flash_size
        $bootloaderOffset = [string]$recoveryFlashArgs.bootloader.offset
        $partitionTableOffset = [string]$recoveryFlashArgs.'partition-table'.offset
    } else {
        $flashMode = "keep"
        $flashFrequency = "keep"
        $flashHeaderSize = "keep"
        $bootloaderOffset = "0x0"
        $partitionTableOffset = "0x8000"
    }
    $paddedFlashSize = "16MB"

    $mergedBin = Join-Path $packageDir "rodakos_sd_recovery_merged.bin"
    & python -m esptool --chip esp32s3 merge-bin `
        --flash-mode $flashMode `
        --flash-freq $flashFrequency `
        --flash-size $flashHeaderSize `
        --pad-to-size $paddedFlashSize `
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
    if ($null -ne $immutableManifest -and
        (-not (Test-BinaryRegionMatches -ContainerPath $mergedBin `
                -FilePath $bootloaderBin -Offset 0) -or
         -not (Test-BinaryRegionMatches -ContainerPath $mergedBin `
                -FilePath $partitionBin -Offset 0x8000) -or
         -not (Test-BinaryRegionMatches -ContainerPath $mergedBin `
                -FilePath $otaDataBin -Offset $otaDataPartition.Offset) -or
         -not (Test-BinaryRegionMatches -ContainerPath $mergedBin `
                -FilePath $recoveryBin -Offset $recoveryPartition.Offset) -or
         -not (Test-BinaryRegionMatches -ContainerPath $mergedBin `
                -FilePath $mainBin -Offset $appPartition.Offset))) {
        throw "新合并镜像未完整保留 immutable 基线资产或当前主应用"
    }
    $bootloaderSize = (Get-Item -LiteralPath $bootloaderBin).Length
    $bootloaderSha256 =
        (Get-FileHash -LiteralPath $bootloaderBin -Algorithm SHA256).Hash.ToLowerInvariant()
    $mainSha256 = (Get-FileHash -LiteralPath $mainBin -Algorithm SHA256).Hash.ToLowerInvariant()
    $recoverySha256 = (Get-FileHash -LiteralPath $recoveryBin -Algorithm SHA256).Hash.ToLowerInvariant()
    $otaDataSha256 = (Get-FileHash -LiteralPath $otaDataBin -Algorithm SHA256).Hash.ToLowerInvariant()
    $mergedSha256 = (Get-FileHash -LiteralPath $mergedBin -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifest = [ordered]@{
        protocolVersion = 2
        manifestVersion = 2
        developmentPackage = [bool]$DevelopmentPackage
        releaseFaultInjection = $releaseFaultInjection
        otaJournalSchemaVersion = $journalSchemaVersion
        buildFlavor = $buildFlavor
        homeHardwareTestPopulation = $homeHardwareTestPopulation
        imageType = $mainImageType
        fileName = $mainPackageName
        fileSize = $mainSize
        checksumType = "sha256"
        checksumValue = $mainSha256
        bootloaderImage = [ordered]@{
            fileName = "bootloader.bin"
            fileSize = $bootloaderSize
            checksumType = "sha256"
            checksumValue = $bootloaderSha256
            offset = $bootloaderOffset
        }
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
    $manifestPath = Join-Path $packageDir "manifest.json"
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    if (-not [string]::IsNullOrWhiteSpace($SigningKeyPath)) {
        if ([string]::IsNullOrWhiteSpace($SigningTaskNo) -or
            [string]::IsNullOrWhiteSpace($SigningVersion)) {
            throw "提供 SigningKeyPath 时必须同时提供 SigningTaskNo 和 SigningVersion"
        }
        & (Join-Path $repoRoot "tools/sign_ota_manifest.ps1") `
            -ManifestPath $manifestPath `
            -SigningKeyPath $SigningKeyPath `
            -VerificationKeyPath $VerificationKeyPath `
            -TaskNo $SigningTaskNo `
            -Version $SigningVersion `
            -ImagePath (Join-Path $packageDir $mainPackageName)
        if ($LASTEXITCODE -ne 0) {
            throw "OTA manifest 签名失败"
        }
    }
    Copy-Item -LiteralPath $VerificationKeyPath -Destination (Join-Path $packageDir "ota-public.pem")
    $verifyOptions = @()
    if ($AllowReleaseFaultInjection) { $verifyOptions += "--allow-faults" }
    & python (Join-Path $repoRoot "tools/ota_security.py") verify-package --directory $packageDir @verifyOptions
    if ($LASTEXITCODE -ne 0) { throw "Package authentication or build-flavor verification failed" }
    if ($releaseFaultInjection) {
        $flashInstructions = @(
            "Release fault-injection test package. Never distribute through production OTA."
            "Use flash_and_test.ps1 -AllowDevelopmentPackage -AllowReleaseFaultInjection."
            "After testing, rebuild both images with all test switches disabled."
        )
    } elseif ($homeHardwareTestPopulation) {
        $flashInstructions = @(
            "Home 硬件测试包："
            "仅使用 flash_and_test.ps1 -AllowHomeHardwareTestPopulation 刷写。"
            "不要使用裸 esptool 绕过 flavor 门禁。"
            "仅用于本地 Home 硬件门禁；禁止上传 Rodak OTA 或作为生产固件分发。"
        )
    } else {
        $flashInstructions = @(
            "首次烧录："
            "Use the repository flash_and_test.ps1 with -Port and -MergedImage pointing to this package."
            "Verify the installed Recovery first; wired migration replaces Recovery and needs a preserved flash backup."
            ""
            "Rodak OTA: upload rodakos.bin and preserve all signed manifest fields including taskNo and version."
        )
    }
    $flashInstructions | Set-Content -LiteralPath (Join-Path $packageDir "flash_args.txt") -Encoding utf8

    $zipPath = "$packageDir.zip"
    Compress-Archive -Path (Join-Path $packageDir "*") -DestinationPath $zipPath -Force
    Write-Host "OTA 包已生成：$packageDir" -ForegroundColor Green
    Write-Host "首次烧录镜像：$mergedBin" -ForegroundColor Green
    if ($releaseFaultInjection) {
        Write-Host "Release 故障注入测试镜像：$(Join-Path $packageDir $mainPackageName)" -ForegroundColor Yellow
        Write-Host "此包仅供本地测试，不能用于生产 OTA" -ForegroundColor Yellow
    } elseif ($homeHardwareTestPopulation) {
        Write-Host "Home 硬件测试镜像：$(Join-Path $packageDir $mainPackageName)" -ForegroundColor Yellow
        Write-Host "禁止将此测试 flavor 上传到 Rodak OTA" -ForegroundColor Yellow
    } else {
        Write-Host "Rodak OTA 应用镜像：$(Join-Path $packageDir $mainPackageName)" -ForegroundColor Green
    }
} finally {
    Pop-Location
}

param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [Parameter(Mandatory = $true)][string]$SigningKeyPath,
    [Parameter(Mandatory = $true)][string]$VerificationKeyPath,
    [Parameter(Mandatory = $true)][string]$TaskNo,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$ImagePath
)

$ErrorActionPreference = "Stop"
& python (Join-Path $PSScriptRoot "ota_security.py") sign `
    --manifest $ManifestPath --private-key $SigningKeyPath --public-key $VerificationKeyPath `
    --task $TaskNo --version $Version --image $ImagePath
if ($LASTEXITCODE -ne 0) { throw "OTA manifest signing or verification failed" }

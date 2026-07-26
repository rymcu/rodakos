# Clean Build Script - use the canonical IDF 6 build workflow.

$ErrorActionPreference = "Stop"

Push-Location $PSScriptRoot
try {
    & (Join-Path $PSScriptRoot "build_rodakos.ps1")
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}

# Compatibility wrapper for the canonical Board Manager workflow.

$ErrorActionPreference = "Stop"

Push-Location $PSScriptRoot
try {
    & (Join-Path $PSScriptRoot "generate_board_config.ps1")
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}

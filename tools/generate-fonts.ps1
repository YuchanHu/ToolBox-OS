$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    & python tools/generate-fonts.py
    if ($LASTEXITCODE -ne 0) { throw 'MiSans conversion failed' }
} finally {
    Pop-Location
}

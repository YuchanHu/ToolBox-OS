param([string]$FontDirectory)

$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    if ($FontDirectory) {
        & python tools/generate-fonts.py --font-dir $FontDirectory
    } else {
        & python tools/generate-fonts.py
    }
    if ($LASTEXITCODE -ne 0) { throw 'MiSans conversion failed' }
} finally {
    Pop-Location
}

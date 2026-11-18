param(
    [string]$BuildDirectory = 'build-toolbox',
    [string]$IdfPath = $env:IDF_PATH
)

$ErrorActionPreference = 'Stop'
if (!$IdfPath -or !(Test-Path -LiteralPath (Join-Path $IdfPath 'tools/idf.py'))) {
    throw 'ESP-IDF not found. Export the ESP-IDF environment or pass -IdfPath.'
}
$IdfPath = (Resolve-Path -LiteralPath $IdfPath).Path
$exportNeeded = $IdfPath -ne $env:IDF_PATH -or !$env:IDF_PYTHON_ENV_PATH -or !(Get-Command ninja -ErrorAction SilentlyContinue)
if ($exportNeeded) {
    . (Join-Path $IdfPath 'export.ps1')
}
if (!$env:IDF_PYTHON_ENV_PATH) {
    throw 'ESP-IDF tools are not initialized. Install the ESP-IDF tools and run export.ps1.'
}
$idfPython = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe'
if (!(Test-Path -LiteralPath $idfPython -PathType Leaf)) {
    throw 'ESP-IDF Python environment is missing. Run the selected ESP-IDF installation and export scripts.'
}
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    & $idfPython (Join-Path $IdfPath 'tools/idf.py') -B $BuildDirectory build
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}

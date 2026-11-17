param([string]$BuildDirectory = 'build-toolbox')
$ErrorActionPreference = 'Stop'
$env:IDF_PATH = 'F:\esp\.espressif\v5.5.5\esp-idf'
$env:ESP_IDF_VERSION = '5.5.5'
$env:IDF_PYTHON_ENV_PATH = 'C:\Users\Yuchan\.espressif\python_env\idf5.5_py3.10_env'
$env:PATH = 'C:\Users\Yuchan\.espressif\tools\cmake\3.30.2\bin;C:\Users\Yuchan\.espressif\tools\ninja\1.12.1;C:\Users\Yuchan\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;C:\Users\Yuchan\.espressif\tools\ccache\4.11.2\ccache-4.11.2-windows-x86_64;' + $env:IDF_PYTHON_ENV_PATH + '\Scripts;' + $env:PATH
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    & "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" "$env:IDF_PATH\tools\idf.py" -B $BuildDirectory -D 'CMAKE_MAKE_PROGRAM=C:/Users/Yuchan/.espressif/tools/ninja/1.12.1/ninja.exe' build
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}

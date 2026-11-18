@echo off
setlocal
set "root=%~dp0..\.."
set "ui_build=%~1"
if not defined ui_build set "ui_build=%root%\build-ui-render"
if not defined VSCMD_ARG_TGT_ARCH (
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
        for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            call "%%I\VC\Auxiliary\Build\vcvars64.bat" >nul
        )
    )
)
where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake is missing. Add it to PATH.
    exit /b 1
)
where ninja >nul 2>nul
if errorlevel 1 (
    echo Ninja is missing. Add it to PATH.
    exit /b 1
)
set "VSLANG=1033"
if "%~2"=="" (
    cmake -S "%root%\tools\ui-render" -B "%ui_build%" -G Ninja -DCMAKE_BUILD_TYPE=Release
) else (
    cmake -S "%root%\tools\ui-render" -B "%ui_build%" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCJSON_SOURCE_DIR=%~2"
)
if errorlevel 1 exit /b 1
cmake --build "%ui_build%" --parallel
exit /b %errorlevel%

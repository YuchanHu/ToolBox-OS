@echo off
set VSLANG=1033
call "D:\DevTools\Microsoft\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set VSLANG=1033
"C:\Users\Yuchan\.espressif\tools\cmake\3.30.2\bin\cmake.exe" -S tools/ui-render -B build-ui-render -G Ninja -DCMAKE_MAKE_PROGRAM=C:/Users/Yuchan/.espressif/tools/ninja/1.12.1/ninja.exe -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
"C:\Users\Yuchan\.espressif\tools\cmake\3.30.2\bin\cmake.exe" --build build-ui-render -j 8
exit /b %errorlevel%

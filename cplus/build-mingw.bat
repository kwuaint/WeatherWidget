@echo off
chcp 65001 >nul 2>&1
echo ========================================
echo   WeatherWidget C++ Build (MinGW)
echo ========================================
echo.

:: Locate g++ - fall back to the portable toolchain installed under D:\Program Files\agent_related
set "W64DEVKIT=D:\Program Files\agent_related\w64devkit\w64devkit"
where g++ >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    if exist "%W64DEVKIT%\bin\g++.exe" (
        set "PATH=%W64DEVKIT%\bin;%PATH%"
        echo [OK] Using bundled MinGW-w64: %W64DEVKIT%
        echo.
    ) else (
        echo [ERROR] g++ not found in PATH.
        echo         Please install MinGW-w64 and add it to your PATH.
        echo         Download: https://github.com/niXman/mingw-builds-binaries/releases
        goto :exit
    )
)

echo [OK] Using compiler:
g++ --version | head -1
echo.

:: Create output directory
if not exist bin mkdir bin

echo [STEP 1] Compiling WeatherWidget.cpp...
g++ -std=c++17 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX ^
    -fms-extensions ^
    -I sdk/include ^
    -o bin/WeatherWidget.exe ^
    WeatherWidget.cpp ^
    -luser32 -lgdi32 -lws2_32 -lwinhttp -lcrypt32 -ldwmapi -lshlwapi -lwinmm -lole32 -loleaut32 -luuid ^
    -mwindows -static -static-libgcc -static-libstdc++ ^
    -finput-charset=UTF-8 -fexec-charset=UTF-8

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compilation failed.
    goto :exit
)

echo [OK] Compiled successfully.
echo.

echo [STEP 2] Copying runtime files...
copy /Y "..\weather-widget.html" "bin\weather-widget.html" >nul
copy /Y "sdk\lib\x64\WebView2Loader.dll" "bin\WebView2Loader.dll" >nul

echo.
echo ========================================
echo   Build successful!
echo   Output: bin\WeatherWidget.exe
echo   Run:    bin\WeatherWidget.exe
echo ========================================

:exit
pause

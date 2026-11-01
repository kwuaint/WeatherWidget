@echo off
chcp 65001 >nul 2>&1
echo ========================================
echo   WeatherWidget C++ Build Script
echo ========================================
echo.

:: Check for Visual Studio Developer Command Prompt
where cl.exe >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [INFO] cl.exe not found in PATH. Trying to locate Visual Studio...
    
    :: Try VS 2022
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
        goto :found_compiler
    )
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64
        goto :found_compiler
    )
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
        goto :found_compiler
    )
    
    :: Try VS 2019
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
        goto :found_compiler
    )
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64
        goto :found_compiler
    )
    
    :: Try Build Tools
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
        goto :found_compiler
    )
    
    echo [ERROR] Visual Studio not found. Please install Visual Studio with C++ desktop development workload.
    echo         Or run this script from a "x64 Native Tools Command Prompt for VS".
    goto :exit
)

:found_compiler
echo [OK] Using MSVC compiler: 
where cl.exe
echo.

:: Create build directory
if not exist build mkdir build
cd build

:: Configure with CMake
echo [STEP 1] Configuring with CMake...
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake configuration failed.
    goto :exit
)

:: Build
echo.
echo [STEP 2] Building...
cmake --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed.
    goto :exit
)

echo.
echo ========================================
echo   Build successful!
echo   Output: build\Release\WeatherWidget.exe
echo ========================================

:: Copy runtime files
copy /Y "..\..\weather-widget.html" "Release\weather-widget.html" >nul
copy /Y "..\sdk\lib\x64\WebView2Loader.dll" "Release\WebView2Loader.dll" >nul

echo.
echo   Run: build\Release\WeatherWidget.exe
echo   Or:  launch.bat
echo ========================================

cd ..
:exit
pause

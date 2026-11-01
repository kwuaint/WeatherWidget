@echo off
chcp 65001 >nul 2>&1
title Weather Widget (C++)

:: Try build output first
if exist "%~dp0bin\WeatherWidget.exe" (
    start "" "%~dp0bin\WeatherWidget.exe"
    exit
)

:: Try cmake build output
if exist "%~dp0build\Release\WeatherWidget.exe" (
    start "" "%~dp0build\Release\WeatherWidget.exe"
    exit
)

echo WeatherWidget.exe not found.
echo Please build the project first:
echo   - MSVC:  run build.bat
echo   - MinGW: run build-mingw.bat
pause

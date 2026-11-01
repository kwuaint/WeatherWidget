@echo off
chcp 65001 >nul 2>&1
title Weather Widget (Python)
cd /d "%~dp0"
python WeatherWidget.py
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Failed to start. Make sure pywebview is installed:
    echo   pip install pywebview
    echo.
    pause
)

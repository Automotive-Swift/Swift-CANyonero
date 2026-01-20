@echo off
:: ECUconnect J2534 Driver Uninstaller
:: Run as Administrator

:: Check for admin rights
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This script requires Administrator privileges.
    echo Right-click and select "Run as administrator"
    pause
    exit /b 1
)

:: Run PowerShell uninstaller
powershell -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Uninstall

pause

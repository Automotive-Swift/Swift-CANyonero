@echo off
REM Build the L2CAP driver test program
REM Requires Visual Studio or Build Tools with C++ workload

where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: cl.exe not found. Run this from a Developer Command Prompt.
    echo Or run: "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    exit /b 1
)

echo Building test_l2cap_driver.exe...
cl /nologo /W3 /EHsc /Fe:test_l2cap_driver.exe test_l2cap_driver.cpp setupapi.lib

if %errorlevel% equ 0 (
    echo.
    echo Build successful!
    echo Run: test_l2cap_driver.exe
    echo  or: test_l2cap_driver.exe AA:BB:CC:DD:EE:FF
) else (
    echo Build failed.
)

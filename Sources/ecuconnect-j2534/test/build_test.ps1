#Requires -Version 5.1
<#
.SYNOPSIS
    Build and optionally run the L2CAP driver test program

.PARAMETER Run
    Run the test after building

.PARAMETER Address
    Bluetooth address to connect to (AA:BB:CC:DD:EE:FF)

.EXAMPLE
    .\build_test.ps1
    .\build_test.ps1 -Run
    .\build_test.ps1 -Run -Address "AA:BB:CC:DD:EE:FF"
#>

param(
    [switch]$Run,
    [string]$Address
)

$ErrorActionPreference = 'Stop'
$TestDir = $PSScriptRoot
$SourceFile = Join-Path $TestDir 'test_l2cap_driver.cpp'
$OutputFile = Join-Path $TestDir 'test_l2cap_driver.exe'

# Find Visual Studio
function Find-VsDevCmd {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -latest -products * -property installationPath 2>$null
        if ($vsPath) {
            $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $vcvars) { return $vcvars }
        }
    }

    # Try common locations
    $locations = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    )

    foreach ($loc in $locations) {
        if (Test-Path $loc) { return $loc }
    }

    return $null
}

Write-Host "=== Building L2CAP Driver Test ===" -ForegroundColor Cyan

# Find vcvars
$vcvars = Find-VsDevCmd
if (-not $vcvars) {
    Write-Host "ERROR: Visual Studio or Build Tools not found." -ForegroundColor Red
    exit 1
}

Write-Host "Using: $vcvars"

# Build using cmd to run vcvars and cl
$buildCmd = @"
call "$vcvars" >nul 2>&1
cd /d "$TestDir"
cl /nologo /W3 /EHsc /Fe:"$OutputFile" "$SourceFile" setupapi.lib
"@

$result = cmd /c $buildCmd 2>&1
Write-Host $result

if (-not (Test-Path $OutputFile)) {
    Write-Host "`nBuild FAILED" -ForegroundColor Red
    exit 1
}

$fileInfo = Get-Item $OutputFile
Write-Host "`nBuild successful!" -ForegroundColor Green
Write-Host "  Output: $OutputFile"
Write-Host "  Size:   $($fileInfo.Length) bytes"

# Run if requested
if ($Run) {
    Write-Host "`n=== Running Test ===" -ForegroundColor Cyan
    Push-Location $TestDir
    try {
        if ($Address) {
            & $OutputFile $Address
        } else {
            & $OutputFile
        }
    } finally {
        Pop-Location
    }
}

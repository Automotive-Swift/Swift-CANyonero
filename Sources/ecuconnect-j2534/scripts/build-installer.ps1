<#
.SYNOPSIS
    Build the ECUconnect J2534 Driver installer

.DESCRIPTION
    Builds both DLLs and creates an NSIS installer package.
    Requires NSIS 3.x to be installed (https://nsis.sourceforge.io)

.PARAMETER SkipBuild
    Skip building DLLs, use existing ones

.PARAMETER NsisPath
    Path to makensis.exe (auto-detected if not specified)

.EXAMPLE
    .\build-installer.ps1
    Build everything and create installer

.EXAMPLE
    .\build-installer.ps1 -SkipBuild
    Create installer using existing DLLs
#>

param(
    [switch]$SkipBuild,
    [string]$NsisPath
)

$ErrorActionPreference = "Stop"

$ScriptDir = $PSScriptRoot
$ProjectDir = Split-Path -Parent $ScriptDir
$DistDir = Join-Path $ProjectDir "dist"

function Write-Header {
    param([string]$Text)
    Write-Host ""
    Write-Host "=== $Text ===" -ForegroundColor Cyan
}

function Write-Step {
    param([string]$Text)
    Write-Host "  $Text"
}

function Find-NSIS {
    # Check if path provided
    if ($NsisPath -and (Test-Path $NsisPath)) {
        return $NsisPath
    }

    # Common NSIS installation locations
    $searchPaths = @(
        "$env:ProgramFiles\NSIS\makensis.exe",
        "$env:ProgramFiles(x86)\NSIS\makensis.exe",
        "C:\NSIS\makensis.exe",
        "$env:LOCALAPPDATA\NSIS\makensis.exe"
    )

    # Check PATH
    $inPath = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
    if ($inPath) {
        return $inPath.Source
    }

    # Check common locations
    foreach ($path in $searchPaths) {
        if (Test-Path $path) {
            return $path
        }
    }

    return $null
}

# ============================================================================
# Main
# ============================================================================

Write-Header "ECUconnect J2534 Installer Builder"

# Find NSIS
$makensis = Find-NSIS
if (-not $makensis) {
    Write-Host ""
    Write-Host "ERROR: NSIS not found!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please install NSIS 3.x from: https://nsis.sourceforge.io" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Or specify the path: .\build-installer.ps1 -NsisPath 'C:\path\to\makensis.exe'"
    Write-Host ""
    exit 1
}

Write-Step "Found NSIS: $makensis"

# Build DLLs if needed
if (-not $SkipBuild) {
    Write-Header "Building DLLs"

    Push-Location $ProjectDir
    try {
        # Build 32-bit
        Write-Step "Building 32-bit..."
        & cmake -S . -B build32 -A Win32 -DDLL_NAME=ecuconnect32 2>$null
        if ($LASTEXITCODE -ne 0) {
            & cmake -S . -B build32 -G "MinGW Makefiles" -DDLL_NAME=ecuconnect32
        }
        & cmake --build build32 --config Release
        if ($LASTEXITCODE -ne 0) { throw "32-bit build failed" }

        # Build 64-bit
        Write-Step "Building 64-bit..."
        & cmake -S . -B build64 -A x64 -DDLL_NAME=ecuconnect64 2>$null
        if ($LASTEXITCODE -ne 0) {
            & cmake -S . -B build64 -G "MinGW Makefiles" -DDLL_NAME=ecuconnect64
        }
        & cmake --build build64 --config Release
        if ($LASTEXITCODE -ne 0) { throw "64-bit build failed" }
    }
    finally {
        Pop-Location
    }
}

# Verify DLLs exist
$dll32 = Join-Path $ProjectDir "build32\Release\ecuconnect32.dll"
$dll64 = Join-Path $ProjectDir "build64\Release\ecuconnect64.dll"

# Also check non-Release paths (MinGW)
if (-not (Test-Path $dll32)) {
    $dll32 = Join-Path $ProjectDir "build32\ecuconnect32.dll"
}
if (-not (Test-Path $dll64)) {
    $dll64 = Join-Path $ProjectDir "build64\ecuconnect64.dll"
}

if (-not (Test-Path $dll32)) {
    throw "32-bit DLL not found: $dll32"
}
if (-not (Test-Path $dll64)) {
    throw "64-bit DLL not found: $dll64"
}

Write-Step "Found 32-bit: $dll32"
Write-Step "Found 64-bit: $dll64"

# Create dist directory
if (-not (Test-Path $DistDir)) {
    New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
    Write-Step "Created: $DistDir"
}

# Build installer
Write-Header "Building Installer"

$nsiScript = Join-Path $ScriptDir "installer.nsi"

Write-Step "Running NSIS..."
& $makensis /V2 $nsiScript

if ($LASTEXITCODE -ne 0) {
    throw "NSIS build failed"
}

$installerPath = Join-Path $DistDir "ECUconnect-J2534-Setup.exe"

if (Test-Path $installerPath) {
    $fileInfo = Get-Item $installerPath
    $sizeKB = [math]::Round($fileInfo.Length / 1024, 1)

    Write-Header "Build Complete"
    Write-Host ""
    Write-Host "Installer created:" -ForegroundColor Green
    Write-Host "  $installerPath"
    Write-Host "  Size: $sizeKB KB"
    Write-Host ""
    Write-Host "To install silently: ECUconnect-J2534-Setup.exe /S" -ForegroundColor Yellow
    Write-Host ""
} else {
    throw "Installer not created"
}

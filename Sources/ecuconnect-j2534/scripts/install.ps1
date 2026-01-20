<#
.SYNOPSIS
    Install ECUconnect J2534 Driver (32-bit and 64-bit)

.NOTES
    Requires Administrator privileges for installation.

.DESCRIPTION
    Builds, installs, and registers both 32-bit and 64-bit versions of
    the ECUconnect J2534 driver on Windows.

.PARAMETER Uninstall
    Uninstall the driver instead of installing

.PARAMETER InstallDir
    Installation directory (default: C:\Program Files\ECUconnect\J2534)

.PARAMETER SkipBuild
    Skip the build step, use existing DLLs

.PARAMETER Only32
    Only install/build 32-bit version

.PARAMETER Only64
    Only install/build 64-bit version

.EXAMPLE
    .\install.ps1
    Build and install both 32-bit and 64-bit drivers

.EXAMPLE
    .\install.ps1 -Uninstall
    Uninstall the driver

.EXAMPLE
    .\install.ps1 -Only32
    Only build and install 32-bit driver
#>

param(
    [switch]$Uninstall,
    [string]$InstallDir = "$env:ProgramFiles\ECUconnect\J2534",
    [switch]$SkipBuild,
    [switch]$Only32,
    [switch]$Only64
)

$ErrorActionPreference = "Stop"

# Check for Administrator privileges
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host ""
    Write-Host "ERROR: This script requires Administrator privileges." -ForegroundColor Red
    Write-Host ""
    Write-Host "Please run from an elevated PowerShell or Command Prompt:" -ForegroundColor Yellow
    Write-Host "  - Right-click PowerShell/CMD -> 'Run as Administrator'"
    Write-Host "  - Or run: Start-Process powershell -Verb RunAs"
    Write-Host ""
    exit 1
}

# Configuration
$DriverName = "ECUconnect"
$DllName32 = "ecuconnect32.dll"
$DllName64 = "ecuconnect64.dll"
$Vendor = "ECUconnect"

# Registry paths
$RegParent32 = "HKLM:\SOFTWARE\PassThruSupport.04.04"
$RegParentWow = "HKLM:\SOFTWARE\WOW6432Node\PassThruSupport.04.04"
$RegPath32 = "$RegParent32\$DriverName"
$RegPathWow64 = "$RegParentWow\$DriverName"

function Write-Header {
    param([string]$Text)
    Write-Host ""
    Write-Host "=== $Text ===" -ForegroundColor Cyan
}

function Write-Step {
    param([string]$Text)
    Write-Host "  $Text"
}

function Set-J2534Registry {
    param(
        [string]$Path,
        [string]$ParentPath,
        [string]$DllFullPath
    )

    # Create parent if needed
    if (-not (Test-Path $ParentPath)) {
        New-Item -Path $ParentPath -Force | Out-Null
        Write-Step "Created parent: $ParentPath"
    }

    # Create driver key
    New-Item -Path $Path -Force | Out-Null

    # Set all required values
    $properties = @{
        "Name"              = $DriverName
        "Vendor"            = $Vendor
        "FunctionLibrary"   = $DllFullPath
        "ConfigApplication" = ""
    }

    $capabilities = @{
        "CAN"       = 1
        "ISO15765"  = 0
        "ISO9141"   = 0
        "ISO14230"  = 0
        "J1850VPW"  = 0
        "J1850PWM"  = 0
    }

    foreach ($prop in $properties.GetEnumerator()) {
        Set-ItemProperty -Path $Path -Name $prop.Key -Value $prop.Value -Type String
    }

    foreach ($cap in $capabilities.GetEnumerator()) {
        Set-ItemProperty -Path $Path -Name $cap.Key -Value $cap.Value -Type DWord
    }

    Write-Step "Registered: $Path"
    Write-Step "  -> $DllFullPath"
}

function Remove-J2534Registry {
    param([string]$Path)

    if (Test-Path $Path) {
        Remove-Item -Path $Path -Recurse -Force
        Write-Step "Removed: $Path"
        return $true
    } else {
        Write-Step "Not found: $Path"
        return $false
    }
}

# ============================================================================
# Uninstall
# ============================================================================

if ($Uninstall) {
    Write-Header "Uninstalling ECUconnect J2534 Driver"

    Write-Host ""
    Write-Host "Removing registry entries..." -ForegroundColor Yellow
    Remove-J2534Registry -Path $RegPath32
    Remove-J2534Registry -Path $RegPathWow64

    Write-Host ""
    Write-Host "Removing DLLs..." -ForegroundColor Yellow
    $dll32 = Join-Path $InstallDir $DllName32
    $dll64 = Join-Path $InstallDir $DllName64

    if (Test-Path $dll32) {
        Remove-Item $dll32 -Force
        Write-Step "Removed: $dll32"
    }
    if (Test-Path $dll64) {
        Remove-Item $dll64 -Force
        Write-Step "Removed: $dll64"
    }

    # Remove directory if empty
    if (Test-Path $InstallDir) {
        $remaining = Get-ChildItem $InstallDir -ErrorAction SilentlyContinue | Measure-Object
        if ($remaining.Count -eq 0) {
            Remove-Item $InstallDir -Force
            Write-Step "Removed: $InstallDir"
        }
    }

    Write-Header "Uninstallation Complete"
    exit 0
}

# ============================================================================
# Install
# ============================================================================

Write-Header "Installing ECUconnect J2534 Driver"

$ScriptDir = Split-Path -Parent $PSScriptRoot
$BuildDir32 = Join-Path $ScriptDir "build32"
$BuildDir64 = Join-Path $ScriptDir "build64"

$Build32 = -not $Only64
$Build64 = -not $Only32

# Build if needed
if (-not $SkipBuild) {
    Push-Location $ScriptDir
    try {
        if ($Build32) {
            Write-Header "Building 32-bit"

            # Configure
            Write-Step "Configuring CMake..."
            $cmakeArgs = @("-S", ".", "-B", "build32", "-DDLL_NAME=ecuconnect32")

            # Try Visual Studio first
            try {
                & cmake @cmakeArgs -A Win32 2>$null
                if ($LASTEXITCODE -ne 0) { throw }
            } catch {
                & cmake @cmakeArgs -G "MinGW Makefiles"
            }

            # Build
            Write-Step "Building..."
            & cmake --build build32 --config Release
            if ($LASTEXITCODE -ne 0) {
                throw "32-bit build failed"
            }
        }

        if ($Build64) {
            Write-Header "Building 64-bit"

            # Configure
            Write-Step "Configuring CMake..."
            $cmakeArgs = @("-S", ".", "-B", "build64", "-DDLL_NAME=ecuconnect64")

            # Try Visual Studio first
            try {
                & cmake @cmakeArgs -A x64 2>$null
                if ($LASTEXITCODE -ne 0) { throw }
            } catch {
                & cmake @cmakeArgs -G "MinGW Makefiles"
            }

            # Build
            Write-Step "Building..."
            & cmake --build build64 --config Release
            if ($LASTEXITCODE -ne 0) {
                throw "64-bit build failed"
            }
        }
    }
    finally {
        Pop-Location
    }
}

# Determine DLL paths
$dll32Src = Join-Path $BuildDir32 "Release\$DllName32"
if (-not (Test-Path $dll32Src)) {
    $dll32Src = Join-Path $BuildDir32 $DllName32
}

$dll64Src = Join-Path $BuildDir64 "Release\$DllName64"
if (-not (Test-Path $dll64Src)) {
    $dll64Src = Join-Path $BuildDir64 $DllName64
}

# Verify at least one DLL exists
$have32 = $Build32 -and (Test-Path $dll32Src)
$have64 = $Build64 -and (Test-Path $dll64Src)

if (-not $have32 -and -not $have64) {
    throw "No DLLs found to install. Build may have failed."
}

# Create install directory
Write-Header "Installing Files"

if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    Write-Step "Created: $InstallDir"
}

$dll32Dst = Join-Path $InstallDir $DllName32
$dll64Dst = Join-Path $InstallDir $DllName64

if ($have32) {
    Copy-Item $dll32Src -Destination $dll32Dst -Force
    Write-Step "Installed: $dll32Dst"
} else {
    Write-Step "Skipped 32-bit (not built)"
}

if ($have64) {
    Copy-Item $dll64Src -Destination $dll64Dst -Force
    Write-Step "Installed: $dll64Dst"
} else {
    Write-Step "Skipped 64-bit (not built)"
}

# Register in registry
Write-Header "Registering Driver"

# 64-bit apps read from SOFTWARE\PassThruSupport.04.04 -> use 64-bit DLL
if ($have64) {
    Set-J2534Registry -Path $RegPath32 -ParentPath $RegParent32 -DllFullPath $dll64Dst
}

# 32-bit apps on 64-bit Windows read from WOW6432Node -> use 32-bit DLL
if ($have32) {
    Set-J2534Registry -Path $RegPathWow64 -ParentPath $RegParentWow -DllFullPath $dll32Dst
}

# Summary
Write-Header "Installation Complete"

Write-Host ""
Write-Host "Installed files:" -ForegroundColor Green
if ($have32) { Write-Host "  32-bit: $dll32Dst" }
if ($have64) { Write-Host "  64-bit: $dll64Dst" }

Write-Host ""
Write-Host "Registry entries:" -ForegroundColor Green
if ($have64) { Write-Host "  64-bit apps: $RegPath32" }
if ($have32) { Write-Host "  32-bit apps: $RegPathWow64" }

Write-Host ""
Write-Host "The driver is now available to J2534 applications." -ForegroundColor Yellow
Write-Host ""

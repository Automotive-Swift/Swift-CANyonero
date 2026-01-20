<#
.SYNOPSIS
    J2534 Driver Utility Script

.PARAMETER Action
    Action to perform: status, list, register, unregister, dev-register

.PARAMETER InstallDir
    Installation directory (default: C:\Program Files\ECUconnect\J2534)

.PARAMETER BuildDir32
    32-bit build directory (for dev-register)

.PARAMETER BuildDir64
    64-bit build directory (for dev-register)
#>

param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("status", "list", "register", "unregister", "dev-register")]
    [string]$Action,

    [string]$InstallDir = "$env:ProgramFiles\ECUconnect\J2534",
    [string]$BuildDir32 = "build32",
    [string]$BuildDir64 = "build64"
)

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

function Set-J2534Registry {
    param(
        [string]$Path,
        [string]$ParentPath,
        [string]$DllFullPath,
        [string]$DisplayName = $DriverName
    )

    # Create parent if needed
    if (-not (Test-Path $ParentPath)) {
        New-Item -Path $ParentPath -Force | Out-Null
    }

    # Create driver key
    New-Item -Path $Path -Force | Out-Null

    # Set values
    Set-ItemProperty -Path $Path -Name "Name" -Value $DisplayName -Type String
    Set-ItemProperty -Path $Path -Name "Vendor" -Value $Vendor -Type String
    Set-ItemProperty -Path $Path -Name "FunctionLibrary" -Value $DllFullPath -Type String
    Set-ItemProperty -Path $Path -Name "ConfigApplication" -Value "" -Type String
    Set-ItemProperty -Path $Path -Name "CAN" -Value 1 -Type DWord
    Set-ItemProperty -Path $Path -Name "ISO15765" -Value 0 -Type DWord
    Set-ItemProperty -Path $Path -Name "ISO9141" -Value 0 -Type DWord
    Set-ItemProperty -Path $Path -Name "ISO14230" -Value 0 -Type DWord
    Set-ItemProperty -Path $Path -Name "J1850VPW" -Value 0 -Type DWord
    Set-ItemProperty -Path $Path -Name "J1850PWM" -Value 0 -Type DWord

    Write-Host "  Registered: $Path"
    Write-Host "    -> $DllFullPath"
}

switch ($Action) {
    "status" {
        Write-Host ""
        Write-Host "=== ECUconnect J2534 Driver Status ===" -ForegroundColor Cyan
        Write-Host ""

        Write-Host "Installed Files:" -ForegroundColor Yellow
        $dll32 = Join-Path $InstallDir $DllName32
        $dll64 = Join-Path $InstallDir $DllName64
        Write-Host "  32-bit: $(if (Test-Path $dll32) { $dll32 } else { 'NOT INSTALLED' })"
        Write-Host "  64-bit: $(if (Test-Path $dll64) { $dll64 } else { 'NOT INSTALLED' })"
        Write-Host ""

        Write-Host "Registry (64-bit apps):" -ForegroundColor Yellow
        if (Test-Path $RegPath32) {
            $props = Get-ItemProperty -Path $RegPath32
            Write-Host "  Path: $RegPath32"
            Write-Host "  DLL:  $($props.FunctionLibrary)"
            Write-Host "  CAN:  $($props.CAN)  ISO15765: $($props.ISO15765)"
        } else {
            Write-Host "  NOT REGISTERED" -ForegroundColor Red
        }
        Write-Host ""

        Write-Host "Registry (32-bit apps / WOW64):" -ForegroundColor Yellow
        if (Test-Path $RegPathWow64) {
            $props = Get-ItemProperty -Path $RegPathWow64
            Write-Host "  Path: $RegPathWow64"
            Write-Host "  DLL:  $($props.FunctionLibrary)"
            Write-Host "  CAN:  $($props.CAN)  ISO15765: $($props.ISO15765)"
        } else {
            Write-Host "  NOT REGISTERED" -ForegroundColor Red
        }
        Write-Host ""
    }

    "list" {
        Write-Host ""
        Write-Host "=== Installed J2534 04.04 Drivers ===" -ForegroundColor Cyan

        $paths = @(
            @{ Name = "64-bit applications"; Path = $RegParent32 },
            @{ Name = "32-bit applications (WOW64)"; Path = $RegParentWow }
        )

        foreach ($item in $paths) {
            Write-Host ""
            Write-Host "$($item.Name):" -ForegroundColor Yellow
            if (Test-Path $item.Path) {
                $drivers = Get-ChildItem -Path $item.Path -ErrorAction SilentlyContinue
                if ($drivers) {
                    foreach ($driver in $drivers) {
                        $props = Get-ItemProperty -Path $driver.PSPath
                        Write-Host "  $($driver.PSChildName)" -ForegroundColor Green
                        Write-Host "    DLL: $($props.FunctionLibrary)"
                        $caps = @()
                        if ($props.CAN -eq 1) { $caps += "CAN" }
                        if ($props.ISO15765 -eq 1) { $caps += "ISO15765" }
                        if ($props.ISO9141 -eq 1) { $caps += "ISO9141" }
                        if ($props.ISO14230 -eq 1) { $caps += "ISO14230" }
                        Write-Host "    Protocols: $($caps -join ', ')"
                    }
                } else {
                    Write-Host "  (none)" -ForegroundColor DarkGray
                }
            } else {
                Write-Host "  (registry path not found)" -ForegroundColor DarkGray
            }
        }
        Write-Host ""
    }

    "register" {
        Write-Host ""
        Write-Host "Registering J2534 driver..." -ForegroundColor Cyan

        $dll32 = Join-Path $InstallDir $DllName32
        $dll64 = Join-Path $InstallDir $DllName64

        if (-not (Test-Path $dll32) -and -not (Test-Path $dll64)) {
            Write-Host "ERROR: No DLLs found in install directory" -ForegroundColor Red
            Write-Host "  Expected: $InstallDir"
            exit 1
        }

        if (Test-Path $dll64) {
            Set-J2534Registry -Path $RegPath32 -ParentPath $RegParent32 -DllFullPath $dll64
        }
        if (Test-Path $dll32) {
            Set-J2534Registry -Path $RegPathWow64 -ParentPath $RegParentWow -DllFullPath $dll32
        }

        Write-Host ""
        Write-Host "Registration complete." -ForegroundColor Green
    }

    "unregister" {
        Write-Host ""
        Write-Host "Removing J2534 registry entries..." -ForegroundColor Cyan

        if (Test-Path $RegPath32) {
            Remove-Item -Path $RegPath32 -Recurse -Force
            Write-Host "  Removed: $RegPath32"
        } else {
            Write-Host "  Not found: $RegPath32"
        }

        if (Test-Path $RegPathWow64) {
            Remove-Item -Path $RegPathWow64 -Recurse -Force
            Write-Host "  Removed: $RegPathWow64"
        } else {
            Write-Host "  Not found: $RegPathWow64"
        }

        Write-Host ""
        Write-Host "Unregistration complete." -ForegroundColor Green
    }

    "dev-register" {
        Write-Host ""
        Write-Host "Registering from build directories (development mode)..." -ForegroundColor Cyan

        $dll32 = Join-Path $BuildDir32 "Release/$DllName32"
        $dll64 = Join-Path $BuildDir64 "Release/$DllName64"

        if (-not (Test-Path $dll32) -and -not (Test-Path $dll64)) {
            Write-Host "ERROR: No DLLs found in build directories" -ForegroundColor Red
            Write-Host "  Checked: $dll32"
            Write-Host "  Checked: $dll64"
            exit 1
        }

        if (Test-Path $dll64) {
            $fullPath = (Resolve-Path $dll64).Path
            Set-J2534Registry -Path $RegPath32 -ParentPath $RegParent32 -DllFullPath $fullPath -DisplayName "$DriverName (Dev)"
        }
        if (Test-Path $dll32) {
            $fullPath = (Resolve-Path $dll32).Path
            Set-J2534Registry -Path $RegPathWow64 -ParentPath $RegParentWow -DllFullPath $fullPath -DisplayName "$DriverName (Dev)"
        }

        Write-Host ""
        Write-Host "Development registration complete." -ForegroundColor Green
    }
}

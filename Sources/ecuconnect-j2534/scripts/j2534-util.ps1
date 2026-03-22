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
    [string]$BuildDir64 = "build64",
    [string]$BuildDir050032 = "../ecuconnect-j2534-0500/build32",
    [string]$BuildDir050064 = "../ecuconnect-j2534-0500/build64"
)

$ErrorActionPreference = "Stop"

# Configuration
$DriverName = "ECUconnect"
$DllName32 = "ecuconnect32.dll"
$DllName64 = "ecuconnect64.dll"
$DllName050032 = "ecuconnect050032.dll"
$DllName050064 = "ecuconnect050064.dll"
$Vendor = "ECUconnect"

# Registry paths
$RegParent32 = "HKLM:\SOFTWARE\PassThruSupport.04.04"
$RegParentWow = "HKLM:\SOFTWARE\WOW6432Node\PassThruSupport.04.04"
$RegPath32 = "$RegParent32\$DriverName"
$RegPathWow64 = "$RegParentWow\$DriverName"
$RegParent0500 = "HKLM:\SOFTWARE\PassThruSupport.05.00"
$RegParent0500Wow = "HKLM:\SOFTWARE\WOW6432Node\PassThruSupport.05.00"
$RegPath050064 = "$RegParent0500\$DriverName"
$RegPath050032 = "$RegParent0500Wow\$DriverName"

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Assert-RegistryWriteAccess {
    param(
        [string]$ActionName
    )

    if (-not (Test-IsAdministrator)) {
        throw "$ActionName requires an elevated PowerShell session because it writes to HKLM. Re-run from an Administrator shell."
    }
}

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
    Set-ItemProperty -Path $Path -Name "ISO9141" -Value 1 -Type DWord
    Set-ItemProperty -Path $Path -Name "ISO14230" -Value 1 -Type DWord
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
        $dll050032 = Join-Path $InstallDir $DllName050032
        $dll050064 = Join-Path $InstallDir $DllName050064
        Write-Host "  32-bit: $(if (Test-Path $dll32) { $dll32 } else { 'NOT INSTALLED' })"
        Write-Host "  64-bit: $(if (Test-Path $dll64) { $dll64 } else { 'NOT INSTALLED' })"
        Write-Host "  05.00 32-bit: $(if (Test-Path $dll050032) { $dll050032 } else { 'NOT INSTALLED' })"
        Write-Host "  05.00 64-bit: $(if (Test-Path $dll050064) { $dll050064 } else { 'NOT INSTALLED' })"
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

        Write-Host "Registry 05.00 (64-bit apps):" -ForegroundColor Yellow
        if (Test-Path $RegPath050064) {
            $props = Get-ItemProperty -Path $RegPath050064
            Write-Host "  Path: $RegPath050064"
            Write-Host "  DLL:  $($props.FunctionLibrary)"
            Write-Host "  CAN:  $($props.CAN)  ISO15765: $($props.ISO15765)  ISO9141: $($props.ISO9141)  ISO14230: $($props.ISO14230)"
        } else {
            Write-Host "  NOT REGISTERED" -ForegroundColor Red
        }
        Write-Host ""

        Write-Host "Registry 05.00 (32-bit apps / WOW64):" -ForegroundColor Yellow
        if (Test-Path $RegPath050032) {
            $props = Get-ItemProperty -Path $RegPath050032
            Write-Host "  Path: $RegPath050032"
            Write-Host "  DLL:  $($props.FunctionLibrary)"
            Write-Host "  CAN:  $($props.CAN)  ISO15765: $($props.ISO15765)  ISO9141: $($props.ISO9141)  ISO14230: $($props.ISO14230)"
        } else {
            Write-Host "  NOT REGISTERED" -ForegroundColor Red
        }
        Write-Host ""
    }

    "list" {
        Write-Host ""
        Write-Host "=== Installed J2534 Drivers ===" -ForegroundColor Cyan

        $paths = @(
            @{ Name = "64-bit applications"; Path = $RegParent32 },
            @{ Name = "32-bit applications (WOW64)"; Path = $RegParentWow },
            @{ Name = "64-bit applications (05.00)"; Path = $RegParent0500 },
            @{ Name = "32-bit applications (WOW64, 05.00)"; Path = $RegParent0500Wow }
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
        Assert-RegistryWriteAccess -ActionName "register"
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
        $dll050032 = Join-Path $InstallDir $DllName050032
        $dll050064 = Join-Path $InstallDir $DllName050064
        if (Test-Path $dll050064) {
            Set-J2534Registry -Path $RegPath050064 -ParentPath $RegParent0500 -DllFullPath $dll050064
        }
        if (Test-Path $dll050032) {
            Set-J2534Registry -Path $RegPath050032 -ParentPath $RegParent0500Wow -DllFullPath $dll050032
        }

        Write-Host ""
        Write-Host "Registration complete." -ForegroundColor Green
    }

    "unregister" {
        Assert-RegistryWriteAccess -ActionName "unregister"
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
        if (Test-Path $RegPath050064) {
            Remove-Item -Path $RegPath050064 -Recurse -Force
            Write-Host "  Removed: $RegPath050064"
        } else {
            Write-Host "  Not found: $RegPath050064"
        }
        if (Test-Path $RegPath050032) {
            Remove-Item -Path $RegPath050032 -Recurse -Force
            Write-Host "  Removed: $RegPath050032"
        } else {
            Write-Host "  Not found: $RegPath050032"
        }

        Write-Host ""
        Write-Host "Unregistration complete." -ForegroundColor Green
    }

    "dev-register" {
        Assert-RegistryWriteAccess -ActionName "dev-register"
        Write-Host ""
        Write-Host "Registering from build directories (development mode)..." -ForegroundColor Cyan

        $dll32 = Join-Path $BuildDir32 "Release/$DllName32"
        $dll64 = Join-Path $BuildDir64 "Release/$DllName64"
        $dll050032 = Join-Path $BuildDir050032 "Release/$DllName050032"
        $dll050064 = Join-Path $BuildDir050064 "Release/$DllName050064"

        if (-not (Test-Path $dll32) -and -not (Test-Path $dll64) -and -not (Test-Path $dll050032) -and -not (Test-Path $dll050064)) {
            Write-Host "ERROR: No DLLs found in build directories" -ForegroundColor Red
            Write-Host "  Checked: $dll32"
            Write-Host "  Checked: $dll64"
            Write-Host "  Checked: $dll050032"
            Write-Host "  Checked: $dll050064"
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
        if (Test-Path $dll050064) {
            $fullPath = (Resolve-Path $dll050064).Path
            Set-J2534Registry -Path $RegPath050064 -ParentPath $RegParent0500 -DllFullPath $fullPath -DisplayName "$DriverName 05.00 (Dev)"
        }
        if (Test-Path $dll050032) {
            $fullPath = (Resolve-Path $dll050032).Path
            Set-J2534Registry -Path $RegPath050032 -ParentPath $RegParent0500Wow -DllFullPath $fullPath -DisplayName "$DriverName 05.00 (Dev)"
        }

        Write-Host ""
        Write-Host "Development registration complete." -ForegroundColor Green
    }
}

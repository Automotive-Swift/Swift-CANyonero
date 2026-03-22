#Requires -Version 5.1
<#
.SYNOPSIS
    ECUconnect L2CAP Driver Build and Installation Utility

.DESCRIPTION
    Builds, installs, uninstalls, and manages the ECUconnect L2CAP kernel driver.
    Supports self-signing for systems with Secure Boot enabled.

.PARAMETER Action
    The action to perform: build, install, uninstall, status, clean, sign, create-cert

.EXAMPLE
    .\driver-util.ps1 -Action build
    .\driver-util.ps1 -Action sign
    .\driver-util.ps1 -Action install
    .\driver-util.ps1 -Action status
#>

param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('build', 'install', 'uninstall', 'status', 'clean', 'sign', 'create-cert', 'reinstall')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'

# Configuration
$DriverName = 'ecuconnect_l2cap'
$DriverDir = Join-Path $PSScriptRoot '..\driver'
$ProjectFile = Join-Path $DriverDir "$DriverName.vcxproj"
$InfFile = Join-Path $DriverDir "$DriverName.inf"
$BuildDir = Join-Path $DriverDir 'x64\Release'
$SysFile = Join-Path $BuildDir "$DriverName.sys"
$CatFile = Join-Path $BuildDir "$DriverName.cat"

# Certificate configuration
$CertSubject = 'CN=ECUconnect Test Signing'
$CertStorePath = 'Cert:\CurrentUser\My'

function Find-MSBuild {
    # Try vswhere first (includes -products * to find BuildTools)
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -latest -products * -property installationPath 2>$null
        if ($vsPath) {
            $msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path $msbuild) { return $msbuild }

            $msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\amd64\MSBuild.exe'
            if (Test-Path $msbuild) { return $msbuild }
        }
    }

    # Try PATH
    $msbuild = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($msbuild) { return $msbuild.Source }

    # Try common locations (including BuildTools)
    $locations = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
    )

    foreach ($loc in $locations) {
        if (Test-Path $loc) { return $loc }
    }

    return $null
}

function Test-WDKInstalled {
    # Check for WDK include directory
    $wdkPaths = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\Include",
        "${env:ProgramFiles}\Windows Kits\10\Include"
    )

    foreach ($path in $wdkPaths) {
        if (Test-Path $path) {
            $versions = Get-ChildItem $path -Directory | Where-Object { $_.Name -match '^\d+\.\d+' }
            if ($versions) { return $true }
        }
    }
    return $false
}

function Find-SignTool {
    # Check Windows Kits
    $kitPaths = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin",
        "${env:ProgramFiles}\Windows Kits\10\bin"
    )

    foreach ($kitPath in $kitPaths) {
        if (Test-Path $kitPath) {
            # Find latest version
            $versions = Get-ChildItem $kitPath -Directory | Where-Object { $_.Name -match '^\d+\.\d+' } | Sort-Object Name -Descending
            foreach ($ver in $versions) {
                $signtool = Join-Path $ver.FullName 'x64\signtool.exe'
                if (Test-Path $signtool) { return $signtool }
            }
        }
    }

    # Try PATH
    $signtool = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($signtool) { return $signtool.Source }

    return $null
}

function Find-Inf2Cat {
    # Check Windows Kits
    $kitPaths = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin",
        "${env:ProgramFiles}\Windows Kits\10\bin"
    )

    foreach ($kitPath in $kitPaths) {
        if (Test-Path $kitPath) {
            $versions = Get-ChildItem $kitPath -Directory | Where-Object { $_.Name -match '^\d+\.\d+' } | Sort-Object Name -Descending
            foreach ($ver in $versions) {
                $inf2cat = Join-Path $ver.FullName 'x86\inf2cat.exe'
                if (Test-Path $inf2cat) { return $inf2cat }
            }
        }
    }

    return $null
}

function Get-TestCertificate {
    # Look for existing certificate
    $cert = Get-ChildItem $CertStorePath -ErrorAction SilentlyContinue | Where-Object { $_.Subject -eq $CertSubject } | Select-Object -First 1
    return $cert
}

function Invoke-CreateCertificate {
    Write-Host "`n=== Creating Test Signing Certificate ===" -ForegroundColor Cyan

    # Check admin for installing to machine stores
    $isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

    # Check for existing cert
    $existingCert = Get-TestCertificate
    if ($existingCert) {
        Write-Host "Certificate already exists:" -ForegroundColor Yellow
        Write-Host "  Subject: $($existingCert.Subject)"
        Write-Host "  Thumbprint: $($existingCert.Thumbprint)"
        Write-Host "  Expires: $($existingCert.NotAfter)"

        $response = Read-Host "Delete and recreate? (y/N)"
        if ($response -ne 'y' -and $response -ne 'Y') {
            return $existingCert
        }

        # Remove old cert
        Remove-Item $existingCert.PSPath -Force
    }

    Write-Host "Creating self-signed certificate for kernel-mode code signing..."

    # Create certificate with code signing EKU
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $CertSubject `
        -CertStoreLocation $CertStorePath `
        -NotAfter (Get-Date).AddYears(5) `
        -KeySpec Signature `
        -KeyUsage DigitalSignature `
        -KeyLength 2048 `
        -HashAlgorithm SHA256

    Write-Host "Certificate created:" -ForegroundColor Green
    Write-Host "  Subject: $($cert.Subject)"
    Write-Host "  Thumbprint: $($cert.Thumbprint)"
    Write-Host "  Expires: $($cert.NotAfter)"

    if ($isAdmin) {
        Write-Host "`nInstalling certificate to trusted stores..."

        # Export cert (public key only)
        $certPath = Join-Path $env:TEMP 'ecuconnect_test.cer'
        Export-Certificate -Cert $cert -FilePath $certPath -Force | Out-Null

        # Import to Trusted Root CA (required for chain validation)
        Import-Certificate -FilePath $certPath -CertStoreLocation 'Cert:\LocalMachine\Root' -ErrorAction SilentlyContinue | Out-Null
        Write-Host "  Added to Trusted Root Certification Authorities" -ForegroundColor Green

        # Import to Trusted Publishers (required for driver installation)
        Import-Certificate -FilePath $certPath -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher' -ErrorAction SilentlyContinue | Out-Null
        Write-Host "  Added to Trusted Publishers" -ForegroundColor Green

        Remove-Item $certPath -Force -ErrorAction SilentlyContinue
    } else {
        Write-Host "`nWARNING: Not running as Administrator." -ForegroundColor Yellow
        Write-Host "To install the driver, you must also run this as Administrator to add"
        Write-Host "the certificate to the machine's trusted stores:"
        Write-Host "  1. Run PowerShell as Administrator"
        Write-Host "  2. Run: .\driver-util.ps1 -Action create-cert"
    }

    return $cert
}

function Invoke-SignDriver {
    Write-Host "`n=== Signing L2CAP Kernel Driver ===" -ForegroundColor Cyan

    # Check driver exists
    if (-not (Test-Path $SysFile)) {
        Write-Host "ERROR: Driver not built. Run build first." -ForegroundColor Red
        exit 1
    }

    # Find signtool
    $signtool = Find-SignTool
    if (-not $signtool) {
        Write-Host "ERROR: signtool.exe not found. Install Windows SDK." -ForegroundColor Red
        exit 1
    }
    Write-Host "Using signtool: $signtool"

    # Get or create certificate
    $cert = Get-TestCertificate
    if (-not $cert) {
        Write-Host "No test certificate found. Creating one..."
        $cert = Invoke-CreateCertificate
    }

    Write-Host "Using certificate: $($cert.Thumbprint)"

    # Sign the .sys file
    Write-Host "`nSigning $DriverName.sys..."
    & $signtool sign /fd SHA256 /sha1 $cert.Thumbprint /v $SysFile

    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Failed to sign driver" -ForegroundColor Red
        exit 1
    }

    # Create catalog file if inf2cat is available
    $inf2cat = Find-Inf2Cat
    if ($inf2cat) {
        Write-Host "`nCreating catalog file..."

        # Copy INF to build directory for catalog generation
        $buildInf = Join-Path $BuildDir "$DriverName.inf"
        Copy-Item $InfFile $buildInf -Force

        & $inf2cat /driver:$BuildDir /os:10_X64 /verbose 2>&1 | Out-Null

        if ((Test-Path $CatFile)) {
            Write-Host "Signing catalog file..."
            & $signtool sign /fd SHA256 /sha1 $cert.Thumbprint /v $CatFile
        }
    }

    # Verify signature
    Write-Host "`nVerifying signature..."
    & $signtool verify /pa /v $SysFile 2>&1 | Select-String -Pattern '(Signature|Issued|Expires|SignTool|Successfully)'

    Write-Host "`nDriver signed successfully!" -ForegroundColor Green
}

function Invoke-Build {
    Write-Host "`n=== Building L2CAP Kernel Driver ===" -ForegroundColor Cyan

    # Check project file exists
    if (-not (Test-Path $ProjectFile)) {
        Write-Host "ERROR: Project file not found: $ProjectFile" -ForegroundColor Red
        exit 1
    }

    # Find MSBuild
    $msbuild = Find-MSBuild
    if (-not $msbuild) {
        Write-Host "ERROR: MSBuild not found. Please install Visual Studio with C++ workload." -ForegroundColor Red
        exit 1
    }
    Write-Host "Using MSBuild: $msbuild"

    # Check WDK
    if (-not (Test-WDKInstalled)) {
        Write-Host "WARNING: Windows Driver Kit (WDK) may not be installed." -ForegroundColor Yellow
        Write-Host "Download from: https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk"
    }

    # Build
    Write-Host "`nBuilding $DriverName.sys..." -ForegroundColor White
    & $msbuild $ProjectFile /p:Configuration=Release /p:Platform=x64 /verbosity:minimal

    if ($LASTEXITCODE -ne 0) {
        Write-Host "`nBuild FAILED" -ForegroundColor Red
        exit $LASTEXITCODE
    }

    # Verify output
    if (Test-Path $SysFile) {
        $fileInfo = Get-Item $SysFile
        Write-Host "`nBuild successful!" -ForegroundColor Green
        Write-Host "  Output: $SysFile"
        Write-Host "  Size:   $($fileInfo.Length) bytes"
    } else {
        Write-Host "`nWARNING: Build completed but .sys file not found at expected location" -ForegroundColor Yellow
        Write-Host "  Expected: $SysFile"
    }
}

function Invoke-Install {
    Write-Host "`n=== Installing L2CAP Kernel Driver ===" -ForegroundColor Cyan

    # Check admin
    $isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Host "ERROR: Administrator privileges required for driver installation." -ForegroundColor Red
        Write-Host "Please run this command from an elevated prompt."
        exit 1
    }

    # Build first if needed
    if (-not (Test-Path $SysFile)) {
        Write-Host "Driver not built. Building first..."
        Invoke-Build
    }

    # Check if driver is signed
    $signtool = Find-SignTool
    $isSigned = $false
    if ($signtool) {
        $ErrorActionPreference = 'SilentlyContinue'
        $verifyResult = & $signtool verify /pa $SysFile 2>&1
        $signExitCode = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($signExitCode -eq 0) {
            $isSigned = $true
            Write-Host "Driver is signed" -ForegroundColor Green
        }
    }

    if (-not $isSigned) {
        # Check test signing mode
        $bcdedit = bcdedit 2>$null
        $testSigning = $bcdedit | Select-String -Pattern 'testsigning\s+Yes'
        $secureBootOn = $bcdedit | Select-String -Pattern 'secureboot\s+Yes'

        if ($secureBootOn -and -not $testSigning) {
            Write-Host "`nWARNING: Driver is not signed and Secure Boot is enabled." -ForegroundColor Yellow
            Write-Host "You must sign the driver first:"
            Write-Host "  1. Run as Administrator: .\driver-util.ps1 -Action create-cert"
            Write-Host "  2. Then run: .\driver-util.ps1 -Action sign"
            Write-Host ""
            $response = Read-Host "Sign the driver now? (Y/n)"
            if ($response -ne 'n' -and $response -ne 'N') {
                Invoke-SignDriver
            } else {
                Write-Host "Aborting installation." -ForegroundColor Red
                exit 1
            }
        } elseif (-not $testSigning) {
            Write-Host "`nWARNING: Test signing is not enabled." -ForegroundColor Yellow
            Write-Host "For unsigned drivers, enable test signing:"
            Write-Host "  bcdedit /set testsigning on"
            Write-Host "  (requires reboot)"
            Write-Host ""
        }
    }

    # Remove existing driver if present
    Write-Host "Checking for existing driver..."
    $existing = pnputil /enum-drivers 2>$null | Select-String -Pattern $DriverName
    if ($existing) {
        Write-Host "Removing existing driver..."
        $driverInfo = pnputil /enum-drivers 2>$null
        $lines = $driverInfo -split "`n"
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -match $DriverName) {
                # Search backwards for the oem*.inf line
                for ($j = $i; $j -ge 0 -and $j -gt ($i - 10); $j--) {
                    if ($lines[$j] -match '(oem\d+\.inf)') {
                        $oemInf = $matches[1]
                        Write-Host "  Removing $oemInf..."
                        pnputil /delete-driver $oemInf /uninstall /force 2>$null
                        break
                    }
                }
                break
            }
        }
    }

    # Copy files to build directory for installation
    # pnputil expects all files referenced in INF to be in the same directory
    Write-Host "Preparing driver package..."
    $buildInf = Join-Path $BuildDir "$DriverName.inf"
    Copy-Item $InfFile $buildInf -Force
    Write-Host "  Copied INF to $BuildDir"

    # Install driver from build directory
    Write-Host "Installing driver from: $buildInf"
    $result = pnputil /add-driver $buildInf /install 2>&1

    if ($LASTEXITCODE -ne 0) {
        Write-Host "`nDriver package installation failed:" -ForegroundColor Red
        Write-Host $result
        exit 1
    }

    Write-Host "Driver package added to store." -ForegroundColor Green

    # Create the root-enumerated device if it doesn't exist
    Write-Host "`nCreating root device..."

    # Check if device already exists
    $existingDevice = pnputil /enum-devices /class System 2>$null | Select-String -Pattern 'ECUconnectL2CAP'

    if ($existingDevice) {
        Write-Host "  Device already exists" -ForegroundColor Yellow
    } else {
        # Find devcon.exe in WDK
        $devcon = $null
        $wdkToolsPath = "${env:ProgramFiles(x86)}\Windows Kits\10\Tools"
        if (Test-Path $wdkToolsPath) {
            $versions = Get-ChildItem $wdkToolsPath -Directory | Where-Object { $_.Name -match '^\d+\.\d+' } | Sort-Object Name -Descending
            foreach ($ver in $versions) {
                $devconPath = Join-Path $ver.FullName 'x64\devcon.exe'
                if (Test-Path $devconPath) {
                    $devcon = $devconPath
                    break
                }
            }
        }

        if ($devcon) {
            Write-Host "  Using devcon: $devcon"
            & $devcon install $buildInf "Root\ECUconnectL2CAP"

            if ($LASTEXITCODE -eq 0) {
                Write-Host "  Root device created" -ForegroundColor Green
            } else {
                Write-Host "  devcon returned error code $LASTEXITCODE" -ForegroundColor Yellow
            }
        } else {
            Write-Host "  WARNING: devcon.exe not found in WDK." -ForegroundColor Yellow
            Write-Host "  You may need to manually add the device using Device Manager:"
            Write-Host "    Action -> Add legacy hardware -> Install manually"
            Write-Host "    -> Show All Devices -> Have Disk -> Browse to driver folder"
        }
    }

    Write-Host "`nDriver installed successfully!" -ForegroundColor Green
}

function Invoke-Uninstall {
    Write-Host "`n=== Uninstalling L2CAP Kernel Driver ===" -ForegroundColor Cyan

    # Check admin
    $isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Host "ERROR: Administrator privileges required." -ForegroundColor Red
        exit 1
    }

    # Find and remove driver
    $driverInfo = pnputil /enum-drivers 2>$null
    $lines = $driverInfo -split "`n"
    $found = $false

    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $DriverName) {
            $found = $true
            for ($j = $i; $j -ge 0 -and $j -gt ($i - 10); $j--) {
                if ($lines[$j] -match '(oem\d+\.inf)') {
                    $oemInf = $matches[1]
                    Write-Host "Removing driver: $oemInf"
                    pnputil /delete-driver $oemInf /uninstall /force
                    if ($LASTEXITCODE -eq 0) {
                        Write-Host "Driver uninstalled successfully!" -ForegroundColor Green
                    }
                    break
                }
            }
            break
        }
    }

    if (-not $found) {
        Write-Host "Driver not found (not installed)." -ForegroundColor Yellow
    }
}

function Show-Status {
    Write-Host "`n=== L2CAP Kernel Driver Status ===" -ForegroundColor Cyan

    # Check installed drivers
    Write-Host "`nInstalled Driver:" -ForegroundColor White
    $driverInfo = pnputil /enum-drivers 2>$null
    $lines = $driverInfo -split "`n"
    $found = $false

    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $DriverName) {
            $found = $true
            # Print context (previous 5 lines contain useful info)
            $start = [Math]::Max(0, $i - 5)
            for ($j = $start; $j -le $i; $j++) {
                Write-Host "  $($lines[$j])"
            }
            break
        }
    }

    if (-not $found) {
        Write-Host "  (not installed)" -ForegroundColor Yellow
    }

    # Check Secure Boot and test signing
    Write-Host "`nSecurity:" -ForegroundColor White
    $bcdedit = bcdedit 2>$null
    $testSigning = $bcdedit | Select-String -Pattern 'testsigning\s+Yes'
    $secureBootOn = $bcdedit | Select-String -Pattern 'secureboot\s+Yes'

    if ($secureBootOn) {
        Write-Host "  Secure Boot: ENABLED" -ForegroundColor Cyan
        Write-Host "    (driver must be signed with trusted certificate)"
    } else {
        Write-Host "  Secure Boot: DISABLED"
    }

    if ($testSigning) {
        Write-Host "  Test Signing: ENABLED" -ForegroundColor Green
    } else {
        Write-Host "  Test Signing: DISABLED" -ForegroundColor Yellow
        if (-not $secureBootOn) {
            Write-Host "    (Run as Admin: bcdedit /set testsigning on)"
        }
    }

    # Check driver signature
    if (Test-Path $SysFile) {
        $signtool = Find-SignTool
        if ($signtool) {
            Write-Host "`nDriver Signature:" -ForegroundColor White
            $ErrorActionPreference = 'SilentlyContinue'
            $verifyResult = & $signtool verify /pa $SysFile 2>&1
            $signExitCode = $LASTEXITCODE
            $ErrorActionPreference = 'Stop'
            if ($signExitCode -eq 0) {
                Write-Host "  SIGNED" -ForegroundColor Green
            } else {
                Write-Host "  NOT SIGNED" -ForegroundColor Yellow
                if ($secureBootOn) {
                    Write-Host "  Run: .\driver-util.ps1 -Action sign"
                }
            }
        }
    }

    # Check test certificate
    Write-Host "`nTest Certificate:" -ForegroundColor White
    $cert = Get-TestCertificate
    if ($cert) {
        Write-Host "  Subject: $($cert.Subject)"
        Write-Host "  Thumbprint: $($cert.Thumbprint)"
        Write-Host "  Expires: $($cert.NotAfter)" -ForegroundColor $(if ($cert.NotAfter -lt (Get-Date)) { 'Red' } else { 'Green' })
    } else {
        Write-Host "  (not created)" -ForegroundColor Yellow
        Write-Host "  Run: .\driver-util.ps1 -Action create-cert"
    }

    # Check build output
    Write-Host "`nBuild Output:" -ForegroundColor White
    if (Test-Path $SysFile) {
        $fileInfo = Get-Item $SysFile
        Write-Host "  File: $SysFile"
        Write-Host "  Size: $($fileInfo.Length) bytes"
        Write-Host "  Date: $($fileInfo.LastWriteTime)"
    } else {
        Write-Host "  (not built)" -ForegroundColor Yellow
    }

    # Check WDK
    Write-Host "`nWDK Status:" -ForegroundColor White
    if (Test-WDKInstalled) {
        Write-Host "  Installed" -ForegroundColor Green
    } else {
        Write-Host "  Not found" -ForegroundColor Yellow
    }

    # Check MSBuild
    Write-Host "`nMSBuild:" -ForegroundColor White
    $msbuild = Find-MSBuild
    if ($msbuild) {
        Write-Host "  $msbuild" -ForegroundColor Green
    } else {
        Write-Host "  Not found" -ForegroundColor Yellow
    }
}

function Invoke-Clean {
    Write-Host "`n=== Cleaning L2CAP Driver Build ===" -ForegroundColor Cyan

    $dirsToClean = @(
        (Join-Path $DriverDir 'x64'),
        (Join-Path $DriverDir 'ARM64'),
        (Join-Path $DriverDir 'Debug'),
        (Join-Path $DriverDir 'Release')
    )

    foreach ($dir in $dirsToClean) {
        if (Test-Path $dir) {
            Write-Host "Removing $dir"
            Remove-Item -Recurse -Force $dir
        }
    }

    # Clean log files
    Get-ChildItem $DriverDir -Filter '*.log' | Remove-Item -Force

    Write-Host "Clean complete." -ForegroundColor Green
}

function Invoke-Reinstall {
    Write-Host "`n=== Reinstalling L2CAP Kernel Driver ===" -ForegroundColor Cyan

    # Check admin
    $isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Host "ERROR: Administrator privileges required." -ForegroundColor Red
        Write-Host "Please run this command from an elevated PowerShell prompt."
        exit 1
    }

    # Build if needed
    if (-not (Test-Path $SysFile)) {
        Write-Host "Driver not built. Building first..."
        Invoke-Build
    }

    # Sign
    Write-Host ""
    Invoke-SignDriver

    # Uninstall existing
    Write-Host ""
    Invoke-Uninstall

    # Install
    Write-Host ""
    Invoke-Install

    Write-Host "`n=== Reinstall Complete ===" -ForegroundColor Green
}

# Main
switch ($Action) {
    'build'       { Invoke-Build }
    'install'     { Invoke-Install }
    'uninstall'   { Invoke-Uninstall }
    'status'      { Show-Status }
    'clean'       { Invoke-Clean }
    'sign'        { Invoke-SignDriver }
    'create-cert' { Invoke-CreateCertificate }
    'reinstall'   { Invoke-Reinstall }
}

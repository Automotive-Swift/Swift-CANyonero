; ECUconnect J2534 Driver Installer
; NSIS Script for SAE J2534-1 compliant PassThru driver
;
; Build with: makensis installer.nsi
; Requires: NSIS 3.x (https://nsis.sourceforge.io)

!include "MUI2.nsh"
!include "x64.nsh"
!include "WinVer.nsh"

; ============================================================================
; General Configuration
; ============================================================================

!define PRODUCT_NAME "ECUconnect J2534 Driver"
!define PRODUCT_PUBLISHER "ECUconnect"
!define PRODUCT_VERSION "1.0.0"
!define DRIVER_NAME "ECUconnect"

; Registry paths for J2534 04.04 specification
!define REG_PASSTHRU_64 "SOFTWARE\PassThruSupport.04.04\${DRIVER_NAME}"
!define REG_PASSTHRU_32 "SOFTWARE\WOW6432Node\PassThruSupport.04.04\${DRIVER_NAME}"
!define REG_UNINSTALL "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\${DRIVER_NAME}"

Name "${PRODUCT_NAME}"
OutFile "..\dist\ECUconnect-J2534-Setup.exe"
InstallDir "$PROGRAMFILES64\ECUconnect\J2534"
InstallDirRegKey HKLM "${REG_UNINSTALL}" "InstallLocation"
RequestExecutionLevel admin

; Compression
SetCompressor /SOLID lzma

; ============================================================================
; Modern UI Configuration
; ============================================================================

!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Welcome page
!define MUI_WELCOMEPAGE_TITLE "Welcome to ECUconnect J2534 Driver Setup"
!define MUI_WELCOMEPAGE_TEXT "This wizard will install the ECUconnect J2534 PassThru driver on your computer.$\r$\n$\r$\nBoth 32-bit and 64-bit versions will be installed for maximum compatibility with diagnostic software.$\r$\n$\r$\nClick Next to continue."

; Finish page
!define MUI_FINISHPAGE_TITLE "Installation Complete"
!define MUI_FINISHPAGE_TEXT "The ECUconnect J2534 driver has been installed successfully.$\r$\n$\r$\nThe driver will appear as 'ECUconnect' in J2534-compatible diagnostic applications."

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ============================================================================
; Installer Sections
; ============================================================================

Section "J2534 Driver (required)" SecDriver
    SectionIn RO

    SetOutPath "$INSTDIR"

    ; Install both DLLs
    File "..\build64\Release\ecuconnect64.dll"
    File "..\build32\Release\ecuconnect32.dll"

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; ========================================================================
    ; J2534 Registry Entries
    ; ========================================================================

    ; 64-bit registry (for 64-bit applications)
    ; Native 64-bit apps read from HKLM\SOFTWARE\PassThruSupport.04.04
    SetRegView 64
    WriteRegStr HKLM "${REG_PASSTHRU_64}" "Name" "${DRIVER_NAME}"
    WriteRegStr HKLM "${REG_PASSTHRU_64}" "Vendor" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKLM "${REG_PASSTHRU_64}" "FunctionLibrary" "$INSTDIR\ecuconnect64.dll"
    WriteRegStr HKLM "${REG_PASSTHRU_64}" "ConfigApplication" ""
    WriteRegDWORD HKLM "${REG_PASSTHRU_64}" "CAN" 1
    WriteRegDWORD HKLM "${REG_PASSTHRU_64}" "ISO15765" 0
    WriteRegDWORD HKLM "${REG_PASSTHRU_64}" "ISO9141" 0
    WriteRegDWORD HKLM "${REG_PASSTHRU_64}" "ISO14230" 0
    WriteRegDWORD HKLM "${REG_PASSTHRU_64}" "J1850VPW" 0
    WriteRegDWORD HKLM "${REG_PASSTHRU_64}" "J1850PWM" 0

    ; 32-bit registry (for 32-bit applications on 64-bit Windows)
    ; WOW64 apps read from HKLM\SOFTWARE\WOW6432Node\PassThruSupport.04.04
    SetRegView 32
    WriteRegStr HKLM "SOFTWARE\PassThruSupport.04.04\${DRIVER_NAME}" "Name" "${DRIVER_NAME}"
    WriteRegStr HKLM "SOFTWARE\PassThruSupport.04.04\${DRIVER_NAME}" "Vendor" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKLM "SOFTWARE\PassThruSupport.04.04\${DRIVER_NAME}" "FunctionLibrary" "$INSTDIR\ecuconnect32.dll"
    WriteRegStr HKLM "SOFTWARE\PassThruSupport.04.04\${DRIVER_NAME}" "ConfigApplication" ""
    WriteRegDWORD HKLM "SOFTWARE\PassThruSupport.04.04\${DRIVER_NAME}" "CAN" 1
    WriteRegDWORD HKLM "SOFTWARE\PassThruSupport.04.04\${DRIVER_NAME}" "ISO15765" 0
    WriteRegDWORD HKLM "SOFTWARE\PassThruSupport.04.04\${DRIVER_NAME}" "ISO9141" 0
    WriteRegDWORD HKLM "SOFTWARE\PassThruSupport.04.04\${DRIVER_NAME}" "ISO14230" 0
    WriteRegDWORD HKLM "SOFTWARE\PassThruSupport.04.04\${DRIVER_NAME}" "J1850VPW" 0
    WriteRegDWORD HKLM "SOFTWARE\PassThruSupport.04.04\${DRIVER_NAME}" "J1850PWM" 0

    ; ========================================================================
    ; Add/Remove Programs Entry
    ; ========================================================================

    SetRegView 64
    WriteRegStr HKLM "${REG_UNINSTALL}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKLM "${REG_UNINSTALL}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKLM "${REG_UNINSTALL}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKLM "${REG_UNINSTALL}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${REG_UNINSTALL}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "${REG_UNINSTALL}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegDWORD HKLM "${REG_UNINSTALL}" "NoModify" 1
    WriteRegDWORD HKLM "${REG_UNINSTALL}" "NoRepair" 1

    ; Estimate installed size (in KB)
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${REG_UNINSTALL}" "EstimatedSize" "$0"

SectionEnd

; ============================================================================
; Uninstaller Section
; ============================================================================

Section "Uninstall"

    ; Remove files
    Delete "$INSTDIR\ecuconnect64.dll"
    Delete "$INSTDIR\ecuconnect32.dll"
    Delete "$INSTDIR\uninstall.exe"

    ; Remove install directory (only if empty)
    RMDir "$INSTDIR"
    RMDir "$PROGRAMFILES64\ECUconnect"

    ; Remove J2534 registry entries
    SetRegView 64
    DeleteRegKey HKLM "${REG_PASSTHRU_64}"

    SetRegView 32
    DeleteRegKey HKLM "SOFTWARE\PassThruSupport.04.04\${DRIVER_NAME}"

    ; Remove uninstall registry entry
    SetRegView 64
    DeleteRegKey HKLM "${REG_UNINSTALL}"

SectionEnd

; ============================================================================
; Installer Functions
; ============================================================================

Function .onInit
    ; Check Windows version (Vista or later required)
    ${IfNot} ${AtLeastWinVista}
        MessageBox MB_OK|MB_ICONSTOP "This driver requires Windows Vista or later."
        Abort
    ${EndIf}

    ; Check for 64-bit Windows
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "This installer requires 64-bit Windows.$\r$\n$\r$\nFor 32-bit Windows, please contact support."
        Abort
    ${EndIf}
FunctionEnd

Function un.onInit
FunctionEnd

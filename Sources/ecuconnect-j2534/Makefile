# ECUconnect J2534 Driver - Makefile
# Builds both 32-bit and 64-bit versions

# Configuration
DRIVER_NAME     := ECUconnect
DLL_NAME_32     := ecuconnect32.dll
DLL_NAME_64     := ecuconnect64.dll
VENDOR          := ECUconnect
BUILD_DIR_32    := build32
BUILD_DIR_64    := build64
INSTALL_DIR     := $(PROGRAMFILES)/ECUconnect/J2534

# Registry paths for J2534 04.04 drivers
REG_PARENT_32   := HKLM:\\SOFTWARE\\PassThruSupport.04.04
REG_PARENT_WOW  := HKLM:\\SOFTWARE\\WOW6432Node\\PassThruSupport.04.04
REG_PATH_32     := $(REG_PARENT_32)\\$(DRIVER_NAME)
REG_PATH_WOW64  := $(REG_PARENT_WOW)\\$(DRIVER_NAME)

.PHONY: all release debug clean rebuild install uninstall register unregister help
.PHONY: release32 release64 debug32 debug64 clean32 clean64
.PHONY: install32 install64 register32 register64 unregister32 unregister64
.PHONY: status list-drivers dev-register
.PHONY: installer installer-full clean-dist

# Default target - build both architectures
all: release

help:
	@echo "ECUconnect J2534 Driver Build System"
	@echo ""
	@echo "Build Targets:"
	@echo "  all / release  - Build both 32-bit and 64-bit release"
	@echo "  release32      - Build 32-bit release only"
	@echo "  release64      - Build 64-bit release only"
	@echo "  debug          - Build both 32-bit and 64-bit debug"
	@echo "  clean          - Remove all build directories"
	@echo "  rebuild        - Clean and build release"
	@echo ""
	@echo "Installer Targets (require NSIS 3.x):"
	@echo "  installer      - Build NSIS installer (after release build)"
	@echo "  installer-full - Build DLLs and create installer"
	@echo "  clean-dist     - Remove dist directory"
	@echo ""
	@echo "Install Targets (require Administrator):"
	@echo "  install        - Install both DLLs and register"
	@echo "  install32      - Install 32-bit DLL only"
	@echo "  install64      - Install 64-bit DLL only"
	@echo "  uninstall      - Unregister and remove all"
	@echo ""
	@echo "Registry Targets (require Administrator):"
	@echo "  register       - Register both DLLs in registry"
	@echo "  unregister     - Remove all registry entries"
	@echo "  status         - Show registration status"
	@echo "  list-drivers   - List all J2534 drivers"
	@echo "  dev-register   - Register from build dirs (development)"
	@echo ""
	@echo "Install directory: $(INSTALL_DIR)"
	@echo "Installer output:  $(DIST_DIR)/ECUconnect-J2534-Setup.exe"

# ============================================================================
# Build targets
# ============================================================================

release: release32 release64
	@echo ""
	@echo "Build complete:"
	@echo "  32-bit: $(BUILD_DIR_32)/Release/$(DLL_NAME_32)"
	@echo "  64-bit: $(BUILD_DIR_64)/Release/$(DLL_NAME_64)"

debug: debug32 debug64

release32:
	@echo "=== Building 32-bit Release ==="
	@cmake -S . -B $(BUILD_DIR_32) -A Win32 -DDLL_NAME=$(DLL_NAME_32) 2>nul || \
		cmake -S . -B $(BUILD_DIR_32) -G "MinGW Makefiles" -DDLL_NAME=$(DLL_NAME_32)
	@cmake --build $(BUILD_DIR_32) --config Release

release64:
	@echo "=== Building 64-bit Release ==="
	@cmake -S . -B $(BUILD_DIR_64) -A x64 -DDLL_NAME=$(DLL_NAME_64) 2>nul || \
		cmake -S . -B $(BUILD_DIR_64) -G "MinGW Makefiles" -DDLL_NAME=$(DLL_NAME_64)
	@cmake --build $(BUILD_DIR_64) --config Release

debug32:
	@echo "=== Building 32-bit Debug ==="
	@cmake -S . -B $(BUILD_DIR_32) -A Win32 -DDLL_NAME=$(DLL_NAME_32) 2>nul || \
		cmake -S . -B $(BUILD_DIR_32) -G "MinGW Makefiles" -DDLL_NAME=$(DLL_NAME_32)
	@cmake --build $(BUILD_DIR_32) --config Debug

debug64:
	@echo "=== Building 64-bit Debug ==="
	@cmake -S . -B $(BUILD_DIR_64) -A x64 -DDLL_NAME=$(DLL_NAME_64) 2>nul || \
		cmake -S . -B $(BUILD_DIR_64) -G "MinGW Makefiles" -DDLL_NAME=$(DLL_NAME_64)
	@cmake --build $(BUILD_DIR_64) --config Debug

clean: clean32 clean64

clean32:
	@rm -rf $(BUILD_DIR_32)

clean64:
	@rm -rf $(BUILD_DIR_64)

rebuild: clean release

# ============================================================================
# Install targets (require Administrator)
# ============================================================================

# Use native 64-bit PowerShell to avoid registry virtualization issues
POWERSHELL64 := C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe

install: release
	@echo ""
	@echo "=== Installing ECUconnect J2534 Driver ==="
	@"$(POWERSHELL64)" -ExecutionPolicy Bypass -File scripts/install.ps1 -SkipBuild

uninstall:
	@echo ""
	@echo "=== Uninstalling ECUconnect J2534 Driver ==="
	@"$(POWERSHELL64)" -ExecutionPolicy Bypass -File scripts/install.ps1 -Uninstall

# ============================================================================
# Registry-only targets
# ============================================================================

register:
	@"$(POWERSHELL64)" -ExecutionPolicy Bypass -File scripts/j2534-util.ps1 -Action register

unregister:
	@"$(POWERSHELL64)" -ExecutionPolicy Bypass -File scripts/j2534-util.ps1 -Action unregister

# ============================================================================
# Status and diagnostic targets
# ============================================================================

status:
	@"$(POWERSHELL64)" -ExecutionPolicy Bypass -File scripts/j2534-util.ps1 -Action status

list-drivers:
	@"$(POWERSHELL64)" -ExecutionPolicy Bypass -File scripts/j2534-util.ps1 -Action list

# Development: register from build directories
dev-register: release
	@"$(POWERSHELL64)" -ExecutionPolicy Bypass -File scripts/j2534-util.ps1 -Action dev-register -BuildDir32 $(BUILD_DIR_32) -BuildDir64 $(BUILD_DIR_64)

# ============================================================================
# Installer targets
# ============================================================================

DIST_DIR := dist

# Build NSIS installer (requires NSIS 3.x: https://nsis.sourceforge.io)
installer: release
	@echo ""
	@echo "=== Building Installer ==="
	@"$(POWERSHELL64)" -ExecutionPolicy Bypass -File scripts/build-installer.ps1 -SkipBuild
	@echo ""
	@echo "Installer: $(DIST_DIR)/ECUconnect-J2534-Setup.exe"

# Build installer from scratch (includes DLL build)
installer-full:
	@echo ""
	@echo "=== Building Full Installer ==="
	@"$(POWERSHELL64)" -ExecutionPolicy Bypass -File scripts/build-installer.ps1

# Clean dist directory
clean-dist:
	@rm -rf $(DIST_DIR)

SHELL := /bin/bash

CMAKE ?= cmake
BUILD_DIR ?= build
BINARY := $(BUILD_DIR)/ecos-socketcan-bridge

# Runtime defaults (override on command line, e.g. `make run ENDPOINT=10.0.0.2:129`)
CAN_IF ?= vcan0
WIFI_IF ?=
ENDPOINT ?= 192.168.42.42:129
BITRATE ?= 500000
DATA_BITRATE ?= 2000000
RECONNECT_MS ?= 1000
STATS_INTERVAL ?= 5
EXTRA_ARGS ?=

.PHONY: help configure build rebuild check-can run run-fd up down vcan-up vcan-down wifi-connect wifi-connect-sudo wifi-disconnect clean distclean

help:
	@echo "Targets:"
	@echo "  configure   Configure CMake in $(BUILD_DIR)"
	@echo "  build       Build bridge binary"
	@echo "  rebuild     Clean CMake cache and rebuild"
	@echo "  check-can   Verify CAN interface exists"
	@echo "  run         Run bridge in classic CAN mode"
	@echo "  run-fd      Run bridge in CAN FD mode"
	@echo "  up          Connect Wi-Fi, create vcan, then run"
	@echo "  down        Bring down vcan and disconnect Wi-Fi"
	@echo "  vcan-up     Create and bring up $(CAN_IF) (requires sudo)"
	@echo "  vcan-down   Bring down and delete $(CAN_IF) (requires sudo)"
	@echo "  wifi-connect Connect to first Wi-Fi SSID prefixed by ECUconnect"
	@echo "  wifi-connect-sudo Same as wifi-connect but runs with sudo"
	@echo "  wifi-disconnect Disconnect Wi-Fi interface (requires sudo)"
	@echo "  clean       Remove CMake build artifacts (keeps cache)"
	@echo "  distclean   Remove entire $(BUILD_DIR)"
	@echo ""
	@echo "Common vars:"
	@echo "  ENDPOINT=$(ENDPOINT)"
	@echo "  CAN_IF=$(CAN_IF)"
	@echo "  WIFI_IF=$(WIFI_IF)"
	@echo "  BITRATE=$(BITRATE)"
	@echo "  DATA_BITRATE=$(DATA_BITRATE)"
	@echo "  RECONNECT_MS=$(RECONNECT_MS)"
	@echo "  STATS_INTERVAL=$(STATS_INTERVAL)"
	@echo "  EXTRA_ARGS=$(EXTRA_ARGS)"

configure:
	$(CMAKE) -S . -B $(BUILD_DIR)

build: configure
	$(CMAKE) --build $(BUILD_DIR) -j

rebuild: distclean build

check-can:
	@ip link show "$(CAN_IF)" >/dev/null 2>&1 || { \
		echo "CAN interface '$(CAN_IF)' not found."; \
		echo "Run: make vcan-up CAN_IF=$(CAN_IF)"; \
		exit 1; \
	}

run: build check-can
	$(BINARY) \
		--can-if $(CAN_IF) \
		--endpoint $(ENDPOINT) \
		--bitrate $(BITRATE) \
		--reconnect-ms $(RECONNECT_MS) \
		--stats-interval $(STATS_INTERVAL) \
		$(EXTRA_ARGS)

run-fd: build check-can
	$(BINARY) \
		--can-if $(CAN_IF) \
		--endpoint $(ENDPOINT) \
		--fd \
		--bitrate $(BITRATE) \
		--data-bitrate $(DATA_BITRATE) \
		--reconnect-ms $(RECONNECT_MS) \
		--stats-interval $(STATS_INTERVAL) \
		$(EXTRA_ARGS)

vcan-up:
	sudo modprobe vcan
	sudo ip link add dev $(CAN_IF) type vcan 2>/dev/null || true
	sudo ip link set up $(CAN_IF)

vcan-down:
	sudo ip link set down $(CAN_IF) 2>/dev/null || true
	sudo ip link del dev $(CAN_IF) 2>/dev/null || true

up: wifi-connect-sudo vcan-up run

down: vcan-down wifi-disconnect

wifi-connect:
	./scripts/connect-ecuconnect.sh --prefix ECUconnect $(if $(WIFI_IF),--ifname $(WIFI_IF),) $(EXTRA_ARGS)

wifi-connect-sudo:
	sudo ./scripts/connect-ecuconnect.sh --prefix ECUconnect $(if $(WIFI_IF),--ifname $(WIFI_IF),) $(EXTRA_ARGS)

wifi-disconnect:
	sudo nmcli device disconnect $(if $(WIFI_IF),$(WIFI_IF),$(shell nmcli -t -f DEVICE,TYPE device status | awk -F: '$$2 == "wifi" { print $$1; exit }'))

clean:
	@if [ -d "$(BUILD_DIR)" ]; then $(CMAKE) --build $(BUILD_DIR) --target clean; fi

distclean:
	rm -rf $(BUILD_DIR)

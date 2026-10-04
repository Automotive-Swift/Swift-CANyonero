.DEFAULT_GOAL := help
MEMORY_TEST_BUILD ?= .build/memory-pressure
BENCHMARK_SERIAL ?= FFFEF3
BENCHMARK_COUNT ?= 32
BENCHMARK_MTU_OUTPUT ?= /tmp/s31-diagnostic-macos-mtu.json
BENCHMARK_OUTPUT ?= /tmp/s31-diagnostic-macos.json
.PHONY: help build test test-memory benchmark-diagnostic benchmark-mtu

help: ## Show developer targets.
	@awk '/^[a-zA-Z0-9_.-]+:.*##/ { split($$0, a, ":.*## "); printf "  %-18s %s\n", a[1], a[2] }' $(MAKEFILE_LIST)
	@echo 'Override CXX or MEMORY_TEST_BUILD for the isolated C++ allocation test.'

build: ## Build SwiftPM products.
	swift build

test: test-memory ## Run allocation regressions and the SwiftPM test suite.
	swift test

test-memory: ## Test failed ISO-TP receive allocations and recovery (C++20).
	mkdir -p "$(MEMORY_TEST_BUILD)"
	$(CXX) -std=c++20 -Wall -Wextra -Werror -Wno-missing-field-initializers -I Sources/libCANyonero/include Tests/MemoryPressure/ReceiveAllocation.cpp -o "$(MEMORY_TEST_BUILD)/receive-allocation"
	"$(MEMORY_TEST_BUILD)/receive-allocation"

benchmark-diagnostic: ## Run the macOS BLE request/response benchmark against the S31 debug firmware.
	swift run -c release ecuconnect-tool benchmark --diagnostic --expected-serial "$(BENCHMARK_SERIAL)" -n "$(BENCHMARK_COUNT)" --output "$(BENCHMARK_OUTPUT)"

benchmark-mtu: ## Measure diagnostic responses around the 1251-byte SDU boundary on macOS.
	swift run -c release ecuconnect-tool benchmark --diagnostic --expected-serial "$(BENCHMARK_SERIAL)" -n "$(BENCHMARK_COUNT)" --responses 1024 1247 1248 2048 4096 --output "$(BENCHMARK_MTU_OUTPUT)"

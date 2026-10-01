.DEFAULT_GOAL := help
MEMORY_TEST_BUILD ?= .build/memory-pressure
.PHONY: help build test test-memory

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

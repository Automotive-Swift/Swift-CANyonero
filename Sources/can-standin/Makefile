.PHONY: help build release run-sender run-receiver run-pair clean

CARGO ?= cargo

SENDER_IFACE ?= canA
RECEIVER_IFACE ?= canB
ARB_ID ?= 0x123
TEST_ID ?= 1

SENDER_ARGS ?= --quality-test --test-id $(TEST_ID) --count 10000 --delay-ms 1
RECEIVER_ARGS ?= --quality-test --test-id $(TEST_ID) --stats-interval 1

help:
	@echo "Targets:"
	@echo "  build         - cargo build"
	@echo "  release       - cargo build --release"
	@echo "  run-sender    - run sender on $(SENDER_IFACE)"
	@echo "  run-receiver  - run receiver on $(RECEIVER_IFACE)"
	@echo "  run-pair      - run receiver in background, then sender"
	@echo "  clean         - remove target directory"
	@echo ""
	@echo "Variables:"
	@echo "  SENDER_IFACE=canA RECEIVER_IFACE=canB ARB_ID=0x123 TEST_ID=1"
	@echo "  SENDER_ARGS='...' RECEIVER_ARGS='...'"

build:
	$(CARGO) build

release:
	$(CARGO) build --release

run-sender:
	$(CARGO) run --release -- sender --iface $(SENDER_IFACE) --id $(ARB_ID) $(SENDER_ARGS)

run-receiver:
	$(CARGO) run --release -- receiver --iface $(RECEIVER_IFACE) --id $(ARB_ID) $(RECEIVER_ARGS)

run-pair:
	@bash -lc 'set -euo pipefail; \
	  $(CARGO) run --release -- receiver --iface $(RECEIVER_IFACE) --id $(ARB_ID) $(RECEIVER_ARGS) & \
	  rx_pid=$$!; \
	  sleep 0.5; \
	  $(CARGO) run --release -- sender --iface $(SENDER_IFACE) --id $(ARB_ID) $(SENDER_ARGS); \
	  wait $$rx_pid'

clean:
	rm -rf target

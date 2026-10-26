# Developer entry points. Everything runs through the project-local
# PlatformIO in .venv/, so a clone needs nothing installed beyond python3.
#
# One verb per thing a human, an agent or CI does; the verbs call
# scripts/*.sh so that a hook without make reaches the same commands.

PIO := .venv/bin/pio

.PHONY: help setup ready checks test build size clean distclean

help:
	@echo "make setup    - install PlatformIO into .venv/ and pre-fetch the toolchains"
	@echo "make ready    - block until a fresh container is prepared (instant when it is)"
	@echo "make checks   - the commit gate: native tests and every firmware build (silence = pass)"
	@echo "make test     - the native unit tests"
	@echo "make build    - the examples, for the Teensy 4.1 and 3.6"
	@echo "make size     - memory usage of the Passthrough example on a Teensy 4.1"
	@echo "make clean    - remove build output"

$(PIO):
	@scripts/bootstrap.sh

setup:
	@scripts/bootstrap.sh --warm

ready:
	@bash scripts/await_ready.sh

checks: $(PIO)
	bash scripts/checks.sh $(SCOPE)

test: $(PIO)
	$(PIO) test -e native

# Every example, for every board. scripts/checks.sh has the same list.
EXAMPLES := passthrough record reverb delay overdrive modulation vsthost
FIRMWARE_ENVS := $(foreach e,$(EXAMPLES),-e $(e)_teensy41 -e $(e)_teensy36)

build: $(PIO)
	$(PIO) run $(FIRMWARE_ENVS)

size: $(PIO)
	$(PIO) run -e passthrough_teensy41 -t size

clean: $(PIO)
	$(PIO) run -t clean

distclean:
	rm -rf .pio .venv

# Developer entry points. Everything runs through the project-local
# PlatformIO in .venv/, so a clone needs nothing installed beyond python3.
#
# One verb per thing a human, an agent or CI does; the verbs call
# scripts/*.sh so that a hook without make reaches the same commands.

PIO := .venv/bin/pio

# Every example, for every board. scripts/checks.sh has the same list.
EXAMPLES := passthrough record reverb delay overdrive modulation vsthost vstmidi cumulus
BOARDS := teensy41 teensy36
FIRMWARE_ENVS := $(foreach e,$(EXAMPLES),-e $(e)_teensy41 -e $(e)_teensy36)

# The one unit the flash and monitor verbs act on: an example, a board and,
# when more than one Teensy is plugged in, the serial port it shows up as.
EXAMPLE ?= passthrough
BOARD ?= teensy41
PORT ?=
ENV := $(EXAMPLE)_$(BOARD)
UPLOAD_PORT := $(if $(PORT),--upload-port $(PORT))
MONITOR_PORT := $(if $(PORT),--monitor-port $(PORT))

.PHONY: help setup ready checks test build size clean distclean \
        check-unit firmware flash monitor flash-monitor ports udev

help:
	@echo "make setup    - install PlatformIO into .venv/ and pre-fetch the toolchains"
	@echo "make ready    - block until a fresh container is prepared (instant when it is)"
	@echo "make checks   - the commit gate: native tests and every firmware build (silence = pass)"
	@echo "make test     - the native unit tests"
	@echo "make build    - the examples, for the Teensy 4.1 and 3.6"
	@echo "make size     - memory usage of the Passthrough example on a Teensy 4.1"
	@echo "make clean    - remove build output"
	@echo ""
	@echo "On a unit (EXAMPLE=$(EXAMPLE) BOARD=$(BOARD), PORT=auto unless set):"
	@echo "make firmware      - build the one example for the one board"
	@echo "make flash         - build it and upload it to the Teensy"
	@echo "make monitor       - open the serial monitor (Ctrl+C to quit)"
	@echo "make flash-monitor - flash, then open the monitor as the Teensy comes back"
	@echo "make ports         - list the serial ports, to pick PORT"
	@echo "make udev          - install PlatformIO's udev rules (Linux, sudo; once per machine)"
	@echo ""
	@echo "  EXAMPLE = $(EXAMPLES)"
	@echo "  BOARD   = $(BOARDS)"
	@echo "  e.g.  make flash-monitor EXAMPLE=reverb BOARD=teensy36 PORT=/dev/ttyACM0"

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


build: $(PIO)
	$(PIO) run $(FIRMWARE_ENVS)

size: $(PIO)
	$(PIO) run -e passthrough_teensy41 -t size

# The upload is teensy-cli (platformio.ini): a sketch built with USB_SERIAL is
# rebooted into the bootloader by software. A blank Teensy, or one running a
# sketch without USB serial, needs its program button pressed once.
check-unit:
	@case " $(EXAMPLES) " in *" $(EXAMPLE) "*) ;; \
	  *) echo "unknown EXAMPLE '$(EXAMPLE)': one of $(EXAMPLES)" >&2; exit 2 ;; esac
	@case " $(BOARDS) " in *" $(BOARD) "*) ;; \
	  *) echo "unknown BOARD '$(BOARD)': one of $(BOARDS)" >&2; exit 2 ;; esac

firmware: check-unit $(PIO)
	$(PIO) run -e $(ENV)

flash: check-unit $(PIO)
	$(PIO) run -e $(ENV) -t upload $(UPLOAD_PORT)

# Settings (speed, and the port when PORT is unset) come from the
# environment; the monitor reconnects on its own when the Teensy reboots.
monitor: check-unit $(PIO)
	$(PIO) device monitor -e $(ENV) $(if $(PORT),--port $(PORT))

flash-monitor: check-unit $(PIO)
	$(PIO) run -e $(ENV) -t upload -t monitor $(UPLOAD_PORT) $(MONITOR_PORT)

ports: $(PIO)
	$(PIO) device list --serial

# Without these rules, Linux gives the bootloader's HID device and the serial
# port to root only, and the upload fails with "Unable to open device".
UDEV_RULES := 99-platformio-udev.rules
udev: $(PIO)
	@src=$$(.venv/bin/python -c 'import os, platformio; print(os.path.join(os.path.dirname(platformio.__file__), "assets", "system", "$(UDEV_RULES)"))') && \
	  sudo install -m 644 "$$src" /etc/udev/rules.d/$(UDEV_RULES)
	sudo udevadm control --reload-rules
	sudo udevadm trigger
	@echo "udev rules installed: unplug and replug the Teensy"

clean: $(PIO)
	$(PIO) run -t clean

distclean:
	rm -rf .pio .venv

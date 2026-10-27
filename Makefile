# Developer entry points. Everything runs through the project-local
# PlatformIO in .venv/, so a clone needs nothing installed beyond python3.
#
# One verb per thing a human, an agent or CI does; the verbs call
# scripts/*.sh so that a hook without make reaches the same commands.

ifeq ($(OS),Windows_NT)
# Every recipe here is POSIX. Make on Windows runs them through cmd.exe unless
# told otherwise, and runs a command with no shell syntax in it directly, so
# Git for Windows' coreutils go on PATH as well as its bash becoming SHELL.
# That is what makes it work from PowerShell. For a non-default install:
# `make GIT_ROOT=D:/Git`.
GIT_ROOT ?= C:/Program Files/Git
export PATH := $(GIT_ROOT)/usr/bin;$(PATH)
SHELL := $(GIT_ROOT)/usr/bin/bash.exe
EXE := .exe
# HTTPS scanners (Avast) re-sign traffic with a root only the Windows store
# trusts, which uv's own bundle does not; and set SSLKEYLOGFILE, which makes
# Python's ssl crash with "OPENSSL_Uplink: no OPENSSL_Applink" on Windows the
# moment PlatformIO opens a connection.
export UV_NATIVE_TLS := 1
unexport SSLKEYLOGFILE
endif

PIO := .venv/bin/pio$(EXE)

# What upload and monitor act on: `make upload EXAMPLE=reverb BOARD=teensy36`.
EXAMPLE ?= passthrough
BOARD ?= teensy41
FIRMWARE_ENV := $(EXAMPLE)_$(BOARD)

.PHONY: help setup ready checks test build size upload monitor panel clean distclean

help:
	@echo "make setup    - install PlatformIO into .venv/ and pre-fetch the toolchains"
	@echo "make ready    - block until a fresh container is prepared (instant when it is)"
	@echo "make checks   - the commit gate: native tests and every firmware build (silence = pass)"
	@echo "make test     - the native unit tests"
	@echo "make build    - the examples, for the Teensy 4.1 and 3.6"
	@echo "make size     - memory usage of the Passthrough example on a Teensy 4.1"
	@echo "make upload   - flash an attached Teensy with $(FIRMWARE_ENV)"
	@echo "make monitor  - the Teensy's serial output (Ctrl+C to quit)"
	@echo "make panel    - the FerricTape web panel on http://localhost:8000 (Chrome or Edge)"
	@echo "make clean    - remove build output"
	@echo ""
	@echo "upload and monitor take EXAMPLE= and BOARD= (teensy41, teensy36);"
	@echo "\`make upload monitor\` flashes then watches. Examples: $(EXAMPLES)"

$(PIO):
	@bash scripts/bootstrap.sh

setup:
	@bash scripts/bootstrap.sh --warm

ready:
	@bash scripts/await_ready.sh

checks: $(PIO)
	PIO=$(PIO) bash scripts/checks.sh $(SCOPE)

test: $(PIO)
	$(PIO) test -e native

# Every example, for every board. scripts/checks.sh has the same list.
EXAMPLES := passthrough linecheck record reverb delay overdrive modulation vsthost vstmidi cumulus ferric
FIRMWARE_ENVS := $(foreach e,$(EXAMPLES),-e $(e)_teensy41 -e $(e)_teensy36)

build: $(PIO)
	$(PIO) run $(FIRMWARE_ENVS)

size: $(PIO)
	$(PIO) run -e passthrough_teensy41 -t size

upload: $(PIO)
	$(PIO) run -e $(FIRMWARE_ENV) -t upload

# The examples all open Serial at 115200, the monitor_speed in platformio.ini.
monitor: $(PIO)
	$(PIO) device monitor -e $(FIRMWARE_ENV)

# The FerricTape sketch's front panel, over Web Serial; it holds the port,
# so not at the same time as monitor.
panel: $(PIO)
	.venv/bin/python$(EXE) -m http.server 8000 --bind 127.0.0.1 -d examples/FerricTape/panel

clean: $(PIO)
	$(PIO) run -t clean

distclean:
	rm -rf .pio .venv

#!/usr/bin/env bash
# Provision PlatformIO into .venv/ at the repository root. Safe to re-run.
# Pass --warm to also download the Teensy and native platforms up front.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [ ! -x .venv/bin/python ]; then
    python3 -m venv .venv
fi
.venv/bin/python -m pip --version >/dev/null 2>&1 || .venv/bin/python -m ensurepip --upgrade
.venv/bin/python -m pip install --quiet --upgrade pip
.venv/bin/python -m pip install --quiet --upgrade platformio

if [ "${1:-}" = "--warm" ]; then
    .venv/bin/pio pkg install -e passthrough_teensy41 -e passthrough_teensy36 -e native >/dev/null 2>&1 ||
        echo "bootstrap: could not pre-fetch platforms; the first build will fetch them" >&2
fi
.venv/bin/pio --version

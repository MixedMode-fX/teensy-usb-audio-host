#!/usr/bin/env bash
#
# Provision the PlatformIO toolchain into .venv/ at the repository root.
#
# Safe to re-run: a working venv is reused and packages are only upgraded.
# Pass --warm to also download the Teensy platform and the native test
# platform up front, which is what makes the first build fast in a fresh
# container.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

# uv when it is installed: it uses a Python it already has in this range, or
# fetches one, so a machine needs nothing else. Without it, the system python3
# and pip, which is what a container or CI has.
PYTHON_VERSION='>=3.11,<3.14'

# Run the interpreter rather than test for the file: a venv made by another
# OS (WSL's, seen from Windows) leaves a bin/python that links to nothing.
if ! .venv/bin/python -c '' >/dev/null 2>&1; then
    rm -rf .venv
    if command -v uv >/dev/null; then
        uv venv --quiet --python "$PYTHON_VERSION" .venv
    else
        python3 -m venv .venv
    fi
fi

# Python on Windows puts the venv's executables in Scripts/, not bin/. A
# junction gives every caller the one path, and needs no privilege to make.
if [ -d .venv/Scripts ] && [ ! -e .venv/bin ]; then
    cmd //c mklink //J "$(cygpath -w .venv/bin)" "$(cygpath -w .venv/Scripts)" >/dev/null
fi

if command -v uv >/dev/null; then
    uv pip install --quiet --python .venv --upgrade platformio
else
    # A venv can exist with no pip in it: a container image that ships a
    # half-built one, or `python3 -m venv` on a system whose ensurepip is a
    # separate package. Repair it here rather than making every caller
    # recognise "No module named pip".
    .venv/bin/python -m pip --version >/dev/null 2>&1 || .venv/bin/python -m ensurepip --upgrade
    .venv/bin/python -m pip install --quiet --upgrade pip
    .venv/bin/python -m pip install --quiet --upgrade platformio
fi

if [ "${1:-}" = "--warm" ]; then
    # Roughly 600 MB of toolchain. Not fatal if the network is unavailable:
    # the first build will fetch it instead.
    .venv/bin/pio pkg install -e passthrough_teensy41 -e passthrough_teensy36 -e native >/dev/null 2>&1 ||
        echo "bootstrap: could not pre-fetch platforms; the first build will fetch them" >&2
fi

.venv/bin/pio --version

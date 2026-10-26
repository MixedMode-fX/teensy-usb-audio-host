#!/usr/bin/env bash
# Everything a fresh checkout needs before any check can run, then the marker
# that lets scripts/await_ready.sh return in milliseconds next time.
#
# This repository is a library with no app to serve, so preparation is the
# toolchain alone: PlatformIO into .venv/, and the Teensy and native platforms
# downloaded up front so the first `pio test` is not also a 600 MB fetch.
# Reached through await_ready.sh, which serialises concurrent callers.
set -uo pipefail

repo_root=$(git rev-parse --show-toplevel 2>/dev/null) || repo_root=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"

run_dir="$repo_root/.dev"
mkdir -p "$run_dir"

log() { echo "[prepare] $*"; }

t0=$SECONDS
log "installing PlatformIO (.venv/) and the Teensy and native platforms"
# The marker is touched only on success: a failed bootstrap that still left a
# marker behind would have every later session believe the container is ready
# with no pio in it.
if ! scripts/bootstrap.sh --warm >>"$run_dir/setup.log" 2>&1; then
  log "prepare failed"
  tail -30 "$run_dir/setup.log"
  exit 1
fi

touch "$run_dir/prepared"
log "prepared in $((SECONDS - t0))s"

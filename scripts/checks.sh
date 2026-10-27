#!/usr/bin/env bash
# The commit gate: one home for the checks, shared by the Makefile, by CI and
# by a Claude commit hook. Prints a failure report and exits 1; exits 0
# silently on success. Silence is the pass.
#
#   bash scripts/checks.sh            the native tests and every firmware build
#   bash scripts/checks.sh native     the native tests only (no toolchain needed)
#   bash scripts/checks.sh firmware   the example builds for every board
set -uo pipefail

repo_root=$(git rev-parse --show-toplevel 2>/dev/null) || repo_root=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"

scopes=("$@")
[[ ${#scopes[@]} -eq 0 ]] && scopes=(native firmware)
for scope in "${scopes[@]}"; do
  case "$scope" in
    native|firmware) ;;
    *) echo "unknown scope: $scope (expected 'native' or 'firmware')" >&2; exit 2 ;;
  esac
done
has() { [[ " ${scopes[*]} " == *" $1 "* ]]; }

pio=${PIO:-}
if [[ -z "$pio" ]]; then
  if [[ -x .venv/bin/pio ]]; then pio=.venv/bin/pio
  elif command -v pio >/dev/null 2>&1; then pio=pio
  else
    echo "Checks failed: no PlatformIO. Run 'bash scripts/bootstrap.sh' or set PIO=/path/to/pio."
    exit 1
  fi
fi

failures=()
report=""
run() {
  local label="$1"; shift
  local out
  if ! out=$("$@" 2>&1); then
    failures+=("$label")
    report+=$'\n'"--- $label ---"$'\n'"$(tail -n 40 <<<"$out")"$'\n'
    return 1
  fi
}

if has native; then
  run "native tests" "$pio" test -e native
fi

if has firmware; then
  # Every example, for every board. The Makefile's build target has the same list.
  envs=()
  for example in passthrough linecheck record reverb delay overdrive modulation vsthost vstmidi cumulus ferric; do
    envs+=(-e "${example}_teensy41" -e "${example}_teensy36")
  done
  run "firmware builds" "$pio" run "${envs[@]}"
fi

if [[ ${#failures[@]} -gt 0 ]]; then
  echo "Checks failed: ${failures[*]}"
  echo "$report"
  exit 1
fi

#!/usr/bin/env bash
# PreToolUse/Bash gate: refuse `git commit` until the native tests pass.
#
# The firmware builds need the ARM toolchain and take a minute; the native
# tests are what every commit must pass. CI builds the firmware on every
# push. Escape hatch: CLAUDE_SKIP_COMMIT_CHECKS=1.
set -uo pipefail

payload=$(cat)
command=$(printf '%s' "$payload" | jq -r '.tool_input.command // ""')

if ! grep -Eq '(^|[;&|(]|&&)[[:space:]]*git([[:space:]]+-[^[:space:]]+([[:space:]]+[^[:space:]-][^[:space:]]*)?)*[[:space:]]+commit([[:space:]]|$)' <<<"$command"; then
  exit 0
fi
grep -Eq -- '--dry-run' <<<"$command" && exit 0
[[ "${CLAUDE_SKIP_COMMIT_CHECKS:-}" == "1" ]] && exit 0

repo_root=$(git rev-parse --show-toplevel 2>/dev/null) || exit 0
cd "$repo_root" || exit 0

changed=$(git diff --cached --name-only --diff-filter=ACMR; git diff --name-only --diff-filter=ACMR)
grep -Eq '^(src/|test/|examples/|platformio\.ini$|scripts/)' <<<"$changed" || exit 0

report=$(bash scripts/checks.sh native) && exit 0

jq -n --arg reason "Commit blocked: checks did not pass. Fix these, then commit again.

$report" '{
  hookSpecificOutput: {
    hookEventName: "PreToolUse",
    permissionDecision: "deny",
    permissionDecisionReason: $reason
  }
}'

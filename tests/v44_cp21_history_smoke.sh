#!/usr/bin/env bash
set -euo pipefail
bin=${1:-./nift}
td=$(mktemp -d); trap 'rm -rf "$td"' EXIT
HOME="$td" printf 'pwd\nexit\n' | HOME="$td" "$bin" sh >/dev/null
grep -qx 'pwd' "$td/.nift_history"

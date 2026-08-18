#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
command -v valgrind >/dev/null 2>&1 || { echo "error: valgrind not found" >&2; exit 2; }

# Keep this wrapper as the supervisor. The endurance harness sends SIGINT to
# this PID; forward it to Valgrind, which forwards the signal to the monitored
# Nift process and remains able to finalize its heap report.
valgrind --leak-check=full --show-leak-kinds=all \
  --errors-for-leak-kinds=definite,indirect,possible --track-origins=yes \
  --error-exitcode=99 "$ROOT/nift" "$@" &
vg_pid=$!

forward_int() {
  kill -INT "$vg_pid" 2>/dev/null || true
}
forward_term() {
  kill -TERM "$vg_pid" 2>/dev/null || true
}
trap forward_int INT
trap forward_term TERM

set +e
wait "$vg_pid"
status=$?
set -e
exit "$status"

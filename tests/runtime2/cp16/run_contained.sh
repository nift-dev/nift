#!/bin/sh
# Runs a CP16 process/I-O test binary under hard limits (D-031 standing rule):
# file size (ulimit -f), address space (ulimit -v; skipped for sanitizer
# builds, which reserve large shadow mappings), wall time (timeout), its own
# session, and child cleanup afterwards: every process still carrying a CP16
# test tag (NIFT_TEST_PROC_TAG=cp16-*) is killed, whatever the test did.
# Usage: run_contained.sh [--sanitized] BINARY [ARGS...]
set -u
SANITIZED=0
if [ "${1:-}" = --sanitized ]; then SANITIZED=1; shift; fi
BIN=${1:?usage: run_contained.sh [--sanitized] BINARY [ARGS...]}
shift
cleanup() {
  # No pipeline (POSIX sh has no pipefail): read the environment block first,
  # then match it; an unreadable block is skipped explicitly.
  nl='
'
  for env in /proc/[0-9]*/environ; do
    block=$( { tr '\0' '\n' < "$env"; } 2>/dev/null ) || continue
    case "$nl$block" in *"${nl}NIFT_TEST_PROC_TAG=cp16-"*) tagged=1 ;; *) tagged=0 ;; esac
    if [ "$tagged" = 1 ]; then
      pid=${env#/proc/}; pid=${pid%/environ}
      kill -9 "$pid" 2>/dev/null && echo "run_contained: killed leftover test process $pid" >&2
    fi
  done
}
(
  ulimit -f 1048576          # 1 GiB per file
  [ "$SANITIZED" = 1 ] || ulimit -v 4194304   # 4 GiB address space
  exec setsid timeout -k 10 600 "$BIN" "$@"
)
status=$?
cleanup
exit $status

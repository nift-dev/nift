#!/usr/bin/env bash
# Negative and positive controls for syscalls.sh. A failed/empty/malformed
# trace or a failed workload must never produce a PASS.
set -u -o pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
NIFT="${NIFT_BIN:?set NIFT_BIN}"
if ! command -v strace >/dev/null 2>&1; then echo "SKIP syscall harness: strace unavailable"; exit 77; fi
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
ok=0
fake() { # name -> writes fake tracer script
  local n=$1; cat >"$T/$n" <<'SH'
#!/bin/sh
out=""
while [ $# -gt 0 ]; do
  case "$1" in
    -o) out=$2; shift 2;;
    -f|-qq) shift;;
    --) shift; break;;
    *) break;;
  esac
done
[ -n "$out" ] && : >"$out"
exec "$@"
SH
  chmod +x "$T/$n"; echo "$T/$n"
}
expect() { # expected description command...
  local want=$1; shift; local desc=$1; shift
  "$@" >"$T/out" 2>"$T/err"; local rc=$?
  if [ "$want" = 0 ]; then [ $rc -eq 0 ] && { echo "PASS syscall harness: $desc"; ok=$((ok+1)); } || { echo "FAIL syscall harness: $desc rc=$rc"; tail -n 3 "$T/out" "$T/err"; fail=1; }
  elif [ "$want" = 77 ]; then [ $rc -eq 77 ] && { echo "PASS syscall harness: $desc (skip)"; ok=$((ok+1)); } || { echo "FAIL syscall harness: $desc rc=$rc expected 77"; fail=1; }
  else [ $rc -ne 0 ] && [ $rc -ne 77 ] && { echo "PASS syscall harness: $desc (failed as required)"; ok=$((ok+1)); } || { echo "FAIL syscall harness: $desc rc=$rc accepted"; fail=1; }
  fi
}
# Real tracer success.
expect 0 "real successful trace" env -u STRACE bash "$HERE/syscalls.sh" 5 0 ok="$NIFT"
# Missing tracer is an explicit skip.
expect 77 "missing tracer" env STRACE="$T/no-such-tracer" bash "$HERE/syscalls.sh" 5 0 skip="$NIFT"
# Tracer failure, empty and malformed traces all fail.
cat >"$T/fail-tracer" <<'SH'
#!/bin/sh
exit 1
SH
chmod +x "$T/fail-tracer"
expect 1 "tracer failure" env STRACE="$T/fail-tracer" bash "$HERE/syscalls.sh" 5 0 fail="$NIFT"
# Empty/malformed tracers execute the real workload but write a bad file.
FAKE_EMPTY=$(fake empty-tracer)
expect 1 "empty trace" env STRACE="$FAKE_EMPTY" bash "$HERE/syscalls.sh" 5 0 empty="$NIFT"
cat >"$T/malformed-tracer" <<'SH'
#!/bin/sh
out=""; while [ $# -gt 0 ]; do case "$1" in -o) out=$2; shift 2;; -f|-qq) shift;; --) shift; break;; *) break;; esac; done
[ -n "$out" ] && printf 'not a trace\n' >"$out"
exec "$@"
SH
chmod +x "$T/malformed-tracer"
expect 1 "malformed trace" env STRACE="$T/malformed-tracer" bash "$HERE/syscalls.sh" 5 0 bad="$NIFT"
# A failed workload must fail, not be counted.
expect 1 "failed workload" env -u STRACE bash "$HERE/syscalls.sh" 5 0 bad=/bin/false
[ $fail -eq 0 ] && echo "PASS syscall harness: $ok controls"
exit $fail

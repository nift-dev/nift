#!/usr/bin/env bash
# Regression test for the scanner-parity flakiness fix (2026-11-08). Fake
# candidate and reference binaries reproduce the three causes deterministically:
# a VM-only thread fixture whose reference order varies, the socket probe whose
# reference prints the port the kernel allocated, and a reference run that is
# killed (bash then reports the job with its pid, at a later point). With the
# old behaviours (NIFT_PARITY_UNFIXED=1) two runs differ; fixed, two runs are
# byte-identical, no shell job message leaks, and every difference is reported.
set -u -o pipefail
REPO="$(pwd)"; fail=0
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cat > "$T/cand" <<'SH'
#!/bin/sh
case "$1" in
  *v_thread_order.f) printf 'running\nmain\nt1\nt2\n2\ndone\n' ;;
  *socket_proof.f) echo "error: not compiled for the VM (builtin-ffi)" >&2; exit 1 ;;
  *t27_assign_toplevel_after_shrink.f) echo "error: not compiled for the VM (for-missing-write)" >&2; exit 1 ;;
esac
SH
cat > "$T/ref" <<'SH'
#!/bin/bash
case "$1" in
  *v_thread_order.f) if [ $((RANDOM % 2)) -eq 0 ]; then printf 't1\nt2\nrunning\nmain\n2\ndone\n'; else printf 't2\nrunning\nt1\nmain\n2\ndone\n'; fi ;;
  *socket_proof.f) echo "PASS socket proof (port $((20000 + (RANDOM * 7 + $$) % 40000)))" ;;
  *t27_assign_toplevel_after_shrink.f) echo 5; kill -9 $$ ;;
esac
SH
chmod +x "$T/cand" "$T/ref"
printf '%s\n' tests/runtime2/cp22/vm-only/v_thread_order.f \
  tests/runtime2/prevalidate/projects/nift-packages_http/tests/socket_proof.f \
  tests/runtime2/for-s2/t27_assign_toplevel_after_shrink.f > "$T/list"
run() { NIFT_BIN="$T/cand" NIFT_REF="$T/ref" NIFT_PARITY_SCRIPTS="$T/list" NIFT_PARITY_UNFIXED="$1" \
          bash tests/runtime2/scanner_parity.sh 2>&1; }
same_twice() { # unfixed-flag -> 0 when 6 runs are byte-identical
  local first; first="$(run "$1")"
  for i in 1 2 3 4 5; do [ "$(run "$1")" = "$first" ] || return 1; done
  printf '%s\n' "$first" > "$T/out.$1"; return 0
}
if same_twice 1; then echo "FAIL scanner parity self-test: the unfixed behaviours did not vary in 6 runs"; fail=1
else echo "PASS scanner parity self-test: the old behaviours make runs differ (thread order, port, job message)"; fi
if ! same_twice 0; then echo "FAIL scanner parity self-test: fixed runs still differ"; fail=1
else
  o="$T/out.0"
  grep -q "Killed" "$o" && { echo "FAIL scanner parity self-test: a shell job message leaked"; fail=1; }
  grep -q "^FAIL script tests/runtime2/prevalidate/projects/nift-packages_http/tests/socket_proof.f$" "$o" \
    && grep -q "^FAIL script tests/runtime2/for-s2/t27_assign_toplevel_after_shrink.f$" "$o" \
    || { echo "FAIL scanner parity self-test: a real difference was not reported"; fail=1; }
  grep -q "v_thread_order" "$o" && { echo "FAIL scanner parity self-test: a VM-only fixture was compared"; fail=1; }
  grep -q "1 cp22 VM-only fixtures left to test-runtime2-cp22" "$o" || { echo "FAIL scanner parity self-test: VM-only fixture not counted"; fail=1; }
  [ "$fail" -eq 0 ] && echo "PASS scanner parity self-test: 6 fixed runs byte-identical; differences reported masked; no job message; VM-only fixture counted"
fi
exit "$fail"

#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-${NIFT:-./nift}}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
CC=${CC:-cc}
case "$(uname -s)" in
  Darwin) LIB="$TMP/libffi.dylib"; "$CC" -std=c99 -fPIC -dynamiclib "$ROOT/tests/ffi/fixture.c" -o "$LIB" ;;
  Linux) LIB="$TMP/libffi.so"; "$CC" -std=c99 -fPIC -shared "$ROOT/tests/ffi/fixture.c" -o "$LIB" ;;
  *) LIB="" ;;
esac
fail_case(){
  local name=$1 pattern=$2; shift 2
  if "$NIFT" "$@" >"$TMP/$name.out" 2>"$TMP/$name.err"; then echo "FAIL: $name unexpectedly succeeded" >&2; exit 1; fi
  grep -Eqi "$pattern" "$TMP/$name.err" || { echo "FAIL: $name diagnostic" >&2; cat "$TMP/$name.err" >&2; exit 1; }
}
run_bounded(){
  local name=$1 stdin_path=$2; shift 2
  python3 - "$name" "$stdin_path" "$@" <<'PY'
import subprocess
import sys

name, stdin_path, *command = sys.argv[1:]
stdin = open(stdin_path, "rb") if stdin_path else None
try:
    completed = subprocess.run(command, stdin=stdin, stdout=subprocess.DEVNULL, timeout=20)
except subprocess.TimeoutExpired:
    print(f"FAIL: {name} timed out after 20 seconds", file=sys.stderr)
    raise SystemExit(124)
finally:
    if stdin is not None:
        stdin.close()
if completed.returncode != 0:
    print(f"FAIL: {name} exited with status {completed.returncode}", file=sys.stderr)
    raise SystemExit(completed.returncode)
PY
}
if [[ -n "$LIB" ]]; then
  cat >"$TMP/badsig.f" <<'NIFT'
lib := ffi_open(args[0])
ffi_call(lib, "nift_ffi_add_i64", "banana(i64)", 1)
NIFT
  fail_case badsig 'signature|type|unsupported|invalid' "$TMP/badsig.f" "$LIB"
  cat >"$TMP/arity.f" <<'NIFT'
lib := ffi_open(args[0])
ffi_call(lib, "nift_ffi_add_i64", "i64(i64,i64)", 1)
NIFT
  fail_case arity 'argument|arity|expects|signature' "$TMP/arity.f" "$LIB"
  cat >"$TMP/closed.f" <<'NIFT'
lib := ffi_open(args[0])
ffi_close(lib)
ffi_call(lib, "nift_ffi_add_i64", "i64(i64,i64)", 1, 2)
NIFT
  fail_case closed 'closed|invalid.*library' "$TMP/closed.f" "$LIB"
fi
cat >"$TMP/mutex-owner.f" <<'NIFT'
fn(bad(m)) { m.unlock(); return true }
m := mutex(0); m.lock(); t := thread(bad, m); t.join()
NIFT
fail_case mutex_owner 'non-owner|owner' "$TMP/mutex-owner.f"
cat >"$TMP/thread-error.f" <<'NIFT'
fn(bad()) { return 1 / 0 }
t := thread(bad); t.join()
NIFT
fail_case thread_error 'thread:' "$TMP/thread-error.f"
cat >"$TMP/async-error.f" <<'NIFT'
@fn[async](bad()) { return 1 / 0 }
f := bad(); r := await f
NIFT
fail_case async_error 'future:' "$TMP/async-error.f"
# Churn handles and shutdown paths repeatedly; success is bounded completion/no signal.
cat >"$TMP/churn.f" <<'NIFT'
@fn[async](id(x)) { return x }
i := 0
while(i < 50) {
  t := thread(id, i); t.join()
  f := id(i); r := await f
  i += 1
}
NIFT
# Named callables are not visible inside this loop in the current parser scope model;
# use an unrolled fixture to keep the churn test about teardown rather than lexical scope.
python3 - "$TMP/churn.f" <<'PY'
import sys
p=sys.argv[1]
lines=['fn(worker(x)) { return x }', '@fn[async](aworker(x)) { return x }']
for i in range(50):
    lines += [f't{i} := thread(worker, {i})', f't{i}.join()', f'f{i} := aworker({i})', f'r{i} := await f{i}']
open(p,'w').write('\n'.join(lines)+'\n')
PY
run_bounded runtime_churn "" "$NIFT" "$TMP/churn.f"
# Repeated process/job churn through the shell must terminate rather than leak/hang.
printf '%s\n' 'sleep 0.01 &' 'sleep 0.01 &' 'sleep 0.01 &' 'jobs' 'wait' 'jobs' 'exit' >"$TMP/jobs.in"
run_bounded job_churn "$TMP/jobs.in" "$NIFT"

echo 'v4.5 adversarial runtime: PASS'

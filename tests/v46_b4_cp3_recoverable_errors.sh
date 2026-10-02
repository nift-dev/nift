#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT_BIN:-${NIFT:-./nift}}
case "$NIFT" in /*) ;; *) NIFT="$(pwd)/$NIFT" ;; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

actual=$("$NIFT" -e '
cause := error("root", "user.root")
value := error("outer", "user.owner.failed", cause)
print(type(value))
print(value.message)
print(value.code)
print(value.category)
print(value.source == "")
print(value.cause.message)
try { throw value } catch(err) {
    print(err.source != "")
    print(err.line > 0)
    print(err.column > 0)
    try { throw err } catch(again) { print(again.line == err.line) }
}
')
[[ "$actual" == $'error\nouter\nuser.owner.failed\nuser\ntrue\nroot\ntrue\ntrue\ntrue\ntrue' ]]

for invalid in \
    'error("x", "io.open_failed")' \
    'error("x", "user.Upper")' \
    'error("x", "user")' \
    'error("x", "user.a", 1)'; do
    if "$NIFT" eval "$invalid" >"$TMP/invalid.out" 2>"$TMP/invalid.err"; then
        echo "invalid Error constructor unexpectedly succeeded: $invalid" >&2
        exit 1
    fi
done

if "$NIFT" -e 'try { value := 1 / 0 } catch(err) { print("caught") }' \
    >"$TMP/fatal.out" 2>"$TMP/fatal.err"; then
    echo 'fatal arithmetic failure was caught' >&2
    exit 1
fi
[[ ! -s "$TMP/fatal.out" ]]
grep -q 'division by zero' "$TMP/fatal.err"

actual=$("$NIFT" -e '
fn(fail()) { throw error("call", "user.call") }
try { fail() } catch(err) { print(err.code) }
fn[async](async_fail()) { throw error("future", "user.future") }
future := async_fail()
try { first := await future } catch(err) { print(err.code) }
try { second := await future } catch(err) { print(err.message) }
fn(thread_fail()) { throw error("thread", "user.thread") }
worker := thread(thread_fail)
try { worker.join() } catch(err) { print(err.code) }
try { worker.join() } catch(err) { print(err.message) }
')
[[ "$actual" == $'user.call\nuser.future\nfuture\nuser.thread\nthread' ]]

if "$NIFT" eval 'error("escape")' >"$TMP/escape.out" 2>"$TMP/escape.err"; then
    echo 'Error escaped the CLI eval boundary' >&2
    exit 1
fi
grep -q 'Error values are not serializable' "$TMP/escape.err"
if "$NIFT" eval '[error("nested")]' >"$TMP/nested.out" 2>"$TMP/nested.err"; then
    echo 'nested Error escaped the CLI eval boundary' >&2
    exit 1
fi
grep -q 'Error values are not serializable' "$TMP/nested.err"

cat >"$TMP/rollback.f" <<'NIFT'
leaked := stack()
fn(leaked_callable()) { return 1 }
export(leaked_callable)
throw error("module failed", "user.module")
NIFT
cat >"$TMP/importer.f" <<'NIFT'
try { import("./rollback.f") } catch(err) { print(err.code) }
fn(leaked_callable()) { return 2 }
print(leaked_callable())
NIFT
[[ "$("$NIFT" "$TMP/importer.f")" == $'user.module\n2' ]]

cat >"$TMP/worker-import.f" <<'NIFT'
fn(work()) { return 1 }
worker := thread(work)
throw error("unsafe", "user.unsafe")
NIFT
cat >"$TMP/worker-importer.f" <<'NIFT'
try { import("./worker-import.f") } catch(err) { print("caught") }
NIFT
if "$NIFT" "$TMP/worker-importer.f" >"$TMP/worker-import.out" 2>"$TMP/worker-import.err"; then
    echo 'worker-owning failed import unexpectedly recovered' >&2
    exit 1
fi
[[ ! -s "$TMP/worker-import.out" ]]
grep -q 'failed import created worker resources' "$TMP/worker-import.err"

cat >"$TMP/fatal-worker-import.f" <<'NIFT'
fn(work()) { return 1 }
worker := thread(work)
value := 1 / 0
NIFT
cat >"$TMP/fatal-worker-importer.f" <<'NIFT'
try { import("./fatal-worker-import.f") } catch(err) { print("caught") }
NIFT
if "$NIFT" "$TMP/fatal-worker-importer.f" >"$TMP/fatal-worker.out" 2>"$TMP/fatal-worker.err"; then
    echo 'fatal worker-owning import unexpectedly succeeded' >&2
    exit 1
fi
[[ ! -s "$TMP/fatal-worker.out" ]]
grep -q 'division by zero' "$TMP/fatal-worker.err"
grep -q 'failed import created worker resources' "$TMP/fatal-worker.err"

actual=$("$NIFT" -e '
fn(fail()) { throw error("prepared", "user.prepared") }
worker := thread(fail)
try { for(item : [1]) { value := worker.join() } } catch(err) { print(err.code) }
print("after")
')
[[ "$actual" == $'user.prepared\nafter' ]]

if "$NIFT" -e '@join([error("nested")], ",")' >"$TMP/join.out" 2>"$TMP/join.err"; then
    echo 'direct template join rendered an Error value' >&2
    exit 1
fi
[[ ! -s "$TMP/join.out" ]]
grep -q 'Error values cannot be rendered as text' "$TMP/join.err"

[[ "$("$NIFT" -e 'fn(other(x)) { return x + 1 } error := other; print(error(2))')" == '3' ]]
[[ "$("$NIFT" -e 'throw := 1; throw += 2; print(throw)')" == '3' ]]
[[ "$("$NIFT" -e 'try { throw error("x") } catch(err) { code := err.code; print(code) }')" == 'user.raised' ]]
[[ "$("$NIFT" -e $'try {\n  value := 1\n  throw error("x")\n} catch(err) { print(err.line); print(err.column) }')" == $'3\n3' ]]

if "$NIFT" -e 'return error("escape")' >"$TMP/return.out" 2>"$TMP/return.err"; then
    echo 'Error escaped a script return boundary' >&2
    exit 1
fi
grep -q 'script return value is not directly renderable' "$TMP/return.err"

printf 'throw error("old")\ntry { value := 1 / 0 } catch(err) { print(err.code) }\nexit\n' | \
    "$NIFT" >"$TMP/repl.out" 2>"$TMP/repl.err" || true
[[ ! -s "$TMP/repl.out" ]]
grep -q 'old' "$TMP/repl.err"
grep -q 'division by zero' "$TMP/repl.err"
if grep -q 'user.raised' "$TMP/repl.out"; then
    echo 'stale recoverable Error caught a later fatal REPL failure' >&2
    exit 1
fi

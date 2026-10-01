#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT_BIN:-${NIFT:-./nift}}
case "$NIFT" in /*) ;; *) NIFT="$(pwd)/$NIFT" ;; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# These names are ordinary identifiers before CP3. Contextual syntax must not
# steal call-position spellings or member/module names.
cat >"$TMP/names.f" <<'NIFT'
fn(error(x)) { return x + 1 }
fn(try(x)) { return x + 2 }
fn(catch(x)) { return x + 3 }
fn(throw(x)) { return x + 4 }
callable := throw
struct(named) {
    try := 10
    fn(error(x)) { return this.try + x }
    fn(catch(x)) { return this.try + x }
    fn(throw(x)) { return this.try + x }
}
item := named()
print(error(1))
print(try(1))
print(catch(1))
print(throw(1))
print(throw (1))
print(callable(2))
print(item.try)
print(item.error(4))
print(item.catch(5))
print(item.throw(6))
NIFT
actual=$("$NIFT" "$TMP/names.f")
[[ "$actual" == $'2\n3\n4\n5\n5\n6\n10\n14\n15\n16' ]] || {
    printf 'unexpected contextual-name baseline:\n%s\n' "$actual" >&2
    exit 1
}

cat >"$TMP/variables.f" <<'NIFT'
error := 1
try := 2
catch := 3
throw := 4
named := {"error":5,"try":6,"catch":7,"throw":8}
print(error + try + catch + throw)
print(named.error + named.try + named.catch + named.throw)
NIFT
[[ "$("$NIFT" "$TMP/variables.f")" == $'10\n26' ]]

cat >"$TMP/named-module.f" <<'NIFT'
fn(error(x)) { return x + 10 }
fn(try(x)) { return x + 20 }
fn(catch(x)) { return x + 30 }
fn(throw(x)) { return x + 20 }
export(error)
export(try)
export(catch)
export(throw)
NIFT
cat >"$TMP/import-names.f" <<'NIFT'
import("./named-module.f")
print(error(1))
print(try(2))
print(catch(3))
print(throw(2))
NIFT
[[ "$("$NIFT" "$TMP/import-names.f")" == $'11\n22\n33\n22' ]]

# Statement-like spellings do not exist yet. Bare `throw value` is currently a
# no-op script line, while a try/catch-shaped expression is rejected. CP3's
# intentional grammar additions must update these explicit baselines.
"$NIFT" -e 'throw 1' >"$TMP/throw.out" 2>"$TMP/throw.err"
[[ ! -s "$TMP/throw.out" && ! -s "$TMP/throw.err" ]]

if "$NIFT" -e 'try { value := 1 } catch(err) { value := 2 }' \
    >"$TMP/try.out" 2>"$TMP/try.err"; then
    echo 'pre-CP3 try/catch statement unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'unknown value or malformed expression' "$TMP/try.err"
for malformed in \
    'try {}' \
    'try { value := 1 } catch' \
    'try { value := 1 } catch() {}'; do
    if "$NIFT" -e "$malformed" >"$TMP/malformed-try.out" 2>"$TMP/malformed-try.err"; then
        echo "pre-CP3 malformed try unexpectedly succeeded: $malformed" >&2
        exit 1
    fi
    grep -q 'unknown value or malformed expression' "$TMP/malformed-try.err"
done

# Execute failing prepared and forced-legacy twins. Their exact call-site
# columns differ, but both currently expose the same semantic diagnostic.
cat >"$TMP/prepared-failure.f" <<'NIFT'
i := 0
while(i < 1) { i = 1 / 0 }
NIFT
cat >"$TMP/legacy-failure.f" <<'NIFT'
i := 0
while(i < 1) { 9007199254740993; i = 1 / 0 }
NIFT
for mode in prepared legacy; do
    if "$NIFT" "$TMP/$mode-failure.f" >"$TMP/$mode.out" 2>"$TMP/$mode.err"; then
        echo "$mode failure twin unexpectedly succeeded" >&2
        exit 1
    fi
    [[ ! -s "$TMP/$mode.out" ]]
    grep -q 'division by zero' "$TMP/$mode.err"
done
prepared_message=$(<"$TMP/prepared.err")
legacy_message=$(<"$TMP/legacy.err")
[[ "${prepared_message##*: }" == "${legacy_message##*: }" ]]

# A single persistent REPL must survive expression, statement, import, resource,
# and worker failures without losing pre-existing state.
cat >"$TMP/bad-import.f" <<'NIFT'
print(missing_from_import)
after_failed_import := 99
export(after_failed_import)
NIFT
cat >"$TMP/repl.in" <<'NIFT'
state := 40
print(missing_expression)
if true {}
import("./bad-import.f")
print(after_failed_import)
managed := file("missing-parent/value.txt")
managed.open("w")
managed.write("dirty")
managed.save()
fn(bad_worker()) { return 1 / 0 }
worker := thread(bad_worker)
worker.join()
print(state + 2)
quit
NIFT
(cd "$TMP" && "$NIFT" <repl.in >repl.out 2>repl.err) || true
for expected in \
    'unknown value or malformed expression: missing_expression' \
    "function if requires '(...)'" \
    'save: parent directory does not exist' \
    'thread: return: division by zero'; do
    grep -q "$expected" "$TMP/repl.err" || {
        printf 'missing REPL diagnostic [%s]:\n' "$expected" >&2
        printf '%s\n' "$(<"$TMP/repl.err")" >&2
        exit 1
    }
done
grep -q 'unknown value or malformed expression: after_failed_import' "$TMP/repl.err" || {
    echo 'failed REPL import installed a later export' >&2
    exit 1
}
if grep -q 'import:' "$TMP/repl.err"; then
    echo 'failed REPL import unexpectedly produced an import diagnostic' >&2
    exit 1
fi
grep -q '42' "$TMP/repl.out" || {
    printf 'REPL did not retain state:\n%s\n' "$(<"$TMP/repl.out")" >&2
    exit 1
}

echo 'v4.6 Batch 4 CP1 characterization: PASS'

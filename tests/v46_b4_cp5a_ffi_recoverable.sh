#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT_BIN:-${NIFT:-./nift}}
case "$NIFT" in /*) ;; *) NIFT="$(pwd)/$NIFT" ;; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
ROOT=$(cd "$(dirname "$0")/.." && pwd)

cd "$TMP"
FIXTURE="$(bash "$ROOT/tests/ffi/build_fixture.sh" .build)"
FIXTURE_ABS="$(cd "$(dirname "$FIXTURE")" && pwd)/$(basename "$FIXTURE")"

# ---------------------------------------------------------------- missing library recoverable
[[ "$("$NIFT" -e 'try { lib := ffi_open("no-such-library-xyz.so") } catch(e) { print(e.code); print(e.category); print(e.source != ""); print(e.line > 0); print(e.message) }')" == $'ffi.library_load_failed\nffi\ntrue\ntrue\nffi_open: no-such-library-xyz.so: cannot open shared object file: No such file or directory' ]]

# missing library uncaught compatibility: message preserved, exit nonzero
if "$NIFT" -e 'lib := ffi_open("no-such-library-xyz.so")' >"$TMP/u.out" 2>"$TMP/u.err"; then
    echo 'uncaught missing-library unexpectedly succeeded' >&2; exit 1
fi
grep -q 'ffi_open: no-such-library-xyz.so: cannot open shared object file' "$TMP/u.err" || {
    echo 'uncaught missing-library message mismatch' >&2; cat "$TMP/u.err" >&2; exit 1
}
if grep -q '"code"\|"cause"\|"category"' "$TMP/u.err"; then
    echo 'uncaught missing-library exposed Error serialization' >&2; exit 1
fi

# ---------------------------------------------------------------- missing symbol recoverable
[[ "$("$NIFT" -e "lib := ffi_open(\"$FIXTURE_ABS\"); try { ffi_call(lib, \"no_such_symbol_xyz\", \"i64(i64,i64)\", 1, 2) } catch(e) { print(e.code); print(e.category); print(e.source != \"\"); print(e.line > 0) }")" == $'ffi.symbol_not_found\nffi\ntrue\ntrue' ]]

if "$NIFT" -e "lib := ffi_open(\"$FIXTURE_ABS\"); ffi_call(lib, \"no_such_symbol_xyz\", \"i64(i64,i64)\", 1, 2)" >"$TMP/s.out" 2>"$TMP/s.err"; then
    echo 'uncaught missing-symbol unexpectedly succeeded' >&2; exit 1
fi
grep -q 'symbol lookup failed: .*undefined symbol: no_such_symbol_xyz' "$TMP/s.err" || {
    echo 'uncaught missing-symbol message mismatch' >&2; cat "$TMP/s.err" >&2; exit 1
}

# ---------------------------------------------------------------- no mutation after failed lookup
# A failed symbol lookup must not disturb the library registry: a subsequent
# valid call on the same handle still works.
[[ "$("$NIFT" -e "
lib := ffi_open(\"$FIXTURE_ABS\")
try { ffi_call(lib, \"no_such_symbol_xyz\", \"i64(i64,i64)\", 1, 2) } catch(e) { print(\"missed:\" + e.code) }
print(ffi_call(lib, \"nift_ffi_add_i64\", \"i64(i64,i64)\", 5, 6))
ffi_close(lib)
")" == $'missed:ffi.symbol_not_found\n11' ]]

# ---------------------------------------------------------------- fatal stays fatal
for script in \
    "lib := ffi_open(\"$FIXTURE_ABS\"); try { ffi_call(lib, \"nift_ffi_add_i64\", \"bogus\", 1) } catch(e) { print(\"caught\") }" \
    "lib := ffi_open(\"$FIXTURE_ABS\"); try { ffi_call(lib, \"nift_ffi_add_i64\", \"i64(banana)\", 1) } catch(e) { print(\"caught\") }" \
    "lib := ffi_open(\"$FIXTURE_ABS\"); try { ffi_call(lib, \"nift_ffi_add_i64\", \"i64(i64)\", 1, 2) } catch(e) { print(\"caught\") }" \
    'try { ffi_call("forged-handle", "sym", "i64()") } catch(e) { print("caught") }' \
    'try { ffi_close("forged-handle") } catch(e) { print("caught") }' \
    "lib := ffi_open(\"$FIXTURE_ABS\"); try { ffi_call(lib, \"nift_ffi_add_i64\", \"i64(i64,i64,i64,i64,i64,i64,i64)\", 1,2,3,4,5,6,7) } catch(e) { print(\"caught\") }" \
    "lib := ffi_open(\"$FIXTURE_ABS\"); try { ffi_callback(1, \"i64(i64)\") } catch(e) { print(\"caught\") }"; do
    if "$NIFT" -e "$script" >"$TMP/ff.out" 2>"$TMP/ff.err"; then
        echo "fatal FFI misuse unexpectedly succeeded: $script" >&2; exit 1
    fi
    grep -q 'caught' "$TMP/ff.out" && { echo "fatal FFI misuse was caught: $script" >&2; exit 1; }
done

# ffi_close unload failure stays fatal (deferred unload semantics); double close fatal
"$NIFT" -e "lib := ffi_open(\"$FIXTURE_ABS\"); ffi_close(lib)" >/dev/null 2>&1
if "$NIFT" -e "lib := ffi_open(\"$FIXTURE_ABS\"); ffi_close(lib); try { ffi_close(lib) } catch(e) { print(\"caught\") }" >"$TMP/dc.out" 2>&1; then
    echo 'double ffi_close unexpectedly succeeded' >&2; exit 1
fi
grep -q 'caught' "$TMP/dc.out" && { echo 'double ffi_close was caught' >&2; exit 1; }

# ---------------------------------------------------------------- successful FFI + repeated load/use/unload
[[ "$("$NIFT" -e "
i := 0
while(i < 3) {
    lib := ffi_open(\"$FIXTURE_ABS\")
    print(ffi_call(lib, \"nift_ffi_add_i64\", \"i64(i64,i64)\", i, 1))
    ffi_close(lib)
    i = i + 1
}
")" == $'1\n2\n3' ]]

# ---------------------------------------------------------------- worker propagation
[[ "$("$NIFT" -e '
fn[async](fail_load()) { lib := ffi_open("no-such-lib-xyz.so") }
h := fail_load()
try { await h } catch(e) { print(e.code); print(e.category) }
try { await h } catch(e) { print(e.code) }
')" == $'ffi.library_load_failed\nffi\nffi.library_load_failed' ]]

[[ "$("$NIFT" -e "
fn(fail_sym()) { lib := ffi_open(\"$FIXTURE_ABS\"); ffi_call(lib, \"no_such_symbol_xyz\", \"i64(i64,i64)\", 1, 2) }
w := thread(fail_sym)
try { w.join() } catch(e) { print(e.code) }
try { w.join() } catch(e) { print(e.code) }
")" == $'ffi.symbol_not_found\nffi.symbol_not_found' ]]

# ---------------------------------------------------------------- import rollback with FFI resource
cat > "$TMP/mod.f" <<NIFT
lib := ffi_open("$FIXTURE_ABS")
print("lib-loaded")
throw error("mod failed")
NIFT
cat > "$TMP/main.f" <<NIFT
try { import("./mod.f") } catch(e) { print("caught:" + e.code) }
lib2 := ffi_open("$FIXTURE_ABS")
print(ffi_call(lib2, "nift_ffi_add_i64", "i64(i64,i64)", 4, 4))
ffi_close(lib2)
NIFT
[[ "$("$NIFT" "$TMP/main.f")" == $'lib-loaded\ncaught:user.raised\n8' ]]

cat > "$TMP/mod2.f" <<NIFT
lib := ffi_open("no-such-lib-xyz.so")
NIFT
cat > "$TMP/main2.f" <<NIFT
try { import("./mod2.f") } catch(e) { print("caught:" + e.code) }
lib3 := ffi_open("$FIXTURE_ABS")
print(ffi_call(lib3, "nift_ffi_add_i64", "i64(i64,i64)", 1, 1))
ffi_close(lib3)
NIFT
[[ "$("$NIFT" "$TMP/main2.f")" == $'caught:ffi.library_load_failed\n2' ]]

echo 'v4.6 Batch 4 CP5a FFI recoverable: PASS'
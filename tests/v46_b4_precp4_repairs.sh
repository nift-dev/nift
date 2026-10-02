#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT_BIN:-${NIFT:-./nift}}
case "$NIFT" in /*) ;; *) NIFT="$(pwd)/$NIFT" ;; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# P1: generic persistence APIs must reject Error values (direct and nested).
# Deliberate serialization remains the explicit stringify()/prettify() surface.
for target in file stream; do
    for case_name in direct array object collection struct; do
        case "$case_name" in
            direct) body='f.write_val(error("boom","user.test"))' ;;
            array) body='f.write_val([error("boom","user.test")])' ;;
            object) body='f.write_val({"e": error("boom","user.test")})' ;;
            collection) body='values := stack(); values.push(error("boom","user.test")); f.write_val(values)' ;;
            struct) body='struct(holder) { value := error("boom","user.test") }; f.write_val(holder())' ;;
        esac
        if [ "$target" = "file" ]; then
            setup='f := file("err.json"); f.open("w"); '
        else
            setup='f := ofstream("err.txt"); '
        fi
        if "$NIFT" -e "$setup$body" >"$TMP/$target-$case_name.out" 2>"$TMP/$target-$case_name.err"; then
            echo "$target.write_val($case_name) unexpectedly accepted an Error" >&2
            exit 1
        fi
        grep -q 'Error values are not serializable' "$TMP/$target-$case_name.err" || {
            echo "$target.write_val($case_name) produced an unexpected error:" >&2
            cat "$TMP/$target-$case_name.err" >&2
            exit 1
        }
    done
done

# Deliberate serialization still works through the explicit presentation path.
cat >"$TMP/stringify-write.f" <<NIFT
f := file("diag.json")
f.open("w")
e := error("boom","user.test")
f.write(e.stringify())
f.save()
f.close()
g := file("diag.json")
g.open("r")
print(g.read_all())
g.close()
NIFT
[[ "$("$NIFT" "$TMP/stringify-write.f")" == '{"message":"boom","code":"user.test","category":"user","source":"","line":0,"column":0,"cause":null}' ]] || {
    echo 'stringify() write path regressed' >&2
    exit 1
}

# P3: a failed try body discards its buffered render output, but already
# emitted external side effects (stdout) are not retracted.
cat >"$TMP/render-buffer.f" <<NIFT
try {
    \$[1 + 1]
    throw error("x", "user.x")
} catch(err) {
    print("caught:" + err.code)
}
NIFT
actual=$("$NIFT" "$TMP/render-buffer.f" 2>"$TMP/render-buffer.err")
[[ "$actual" == "caught:user.x" ]] || {
    echo 'failed try body leaked buffered render output' >&2
    printf '%s\n' "$actual" >&2
    exit 1
}

cat >"$TMP/stdout-sent.f" <<NIFT
try {
    print("already-sent")
    throw error("x", "user.x")
} catch(err) {
    print("caught:" + err.code)
}
NIFT
actual=$("$NIFT" "$TMP/stdout-sent.f" 2>/dev/null)
[[ "$actual" == $'already-sent\ncaught:user.x' ]] || {
    echo 'already-emitted stdout was not preserved across catch' >&2
    printf '%s\n' "$actual" >&2
    exit 1
}

# Error value language characterization (CP3 semantics folded in here).
[[ "$("$NIFT" -e 'print(error("m","user.a").stringify())')" == \
    '{"message":"m","code":"user.a","category":"user","source":"","line":0,"column":0,"cause":null}' ]]
[[ "$("$NIFT" -e 'print(error("m","user.a",error("c","user.c")).stringify())')" == \
    '{"message":"m","code":"user.a","category":"user","source":"","line":0,"column":0,"cause":{"message":"c","code":"user.c","category":"user","source":"","line":0,"column":0,"cause":null}}' ]]
[[ "$("$NIFT" -e 'a := error("m","user.a",error("c","user.c")); b := error("m","user.a",error("c","user.c")); print(a == b)')" == 'true' ]]
[[ "$("$NIFT" -e 'a := error("m","user.a",error("c","user.c")); d := error("m","user.a",error("other","user.o")); print(a == d)')" == 'false' ]]
[[ "$("$NIFT" -e 'a := error("m","user.a"); print(copy(a) == a); print(deepcopy(a) == a)')" == $'true\ntrue' ]]

# A fatal failure inside catch propagates past every enclosing catch.
if "$NIFT" -e 'try { try { throw error("x") } catch(e) { value := 1 / 0 } } catch(outer) { print("outer caught") }' \
    >"$TMP/fatal-catch.out" 2>"$TMP/fatal-catch.err"; then
    echo 'fatal failure inside catch was caught by an outer catch' >&2
    exit 1
fi
grep -q 'division by zero' "$TMP/fatal-catch.err"

# A catch body may throw a different Error, which propagates to an outer catch.
[[ "$("$NIFT" -e 'try { try { throw error("a","user.a") } catch(e) { throw error("b","user.b") } } catch(outer) { print(outer.code) }')" == 'user.b' ]]

# Nested worker cause chains survive await/join replay.
[[ "$("$NIFT" -e '
fn[async](outer()) { fn[async](inner()) { throw error("deep","user.d") }; h := inner(); try { await h } catch(e) { throw error("wrapped","user.w", e) } }
h := outer()
try { await h } catch(e) { print(e.code); print(e.cause.code) }
')" == $'user.w\nuser.d' ]]

# @script bodies and block lambdas support try/catch.
[[ "$("$NIFT" -e '@script { try { throw error("s","user.s") } catch(e) { print(e.code) } }')" == 'user.s' ]]
[[ "$("$NIFT" -e 'f := () => { try { throw error("b","user.b") } catch(e) { return e.code } }; print(f())')" == 'user.b' ]]

# Repeated interleaved await/join replays the same recoverable completion.
[[ "$("$NIFT" -e '
fn[async](af()) { throw error("f","user.f") }
fn[async](ag()) { throw error("g","user.g") }
h1 := af()
h2 := ag()
for(i : [1,2]) {
    try { await h1 } catch(e) { print(e.code) }
    try { await h2 } catch(e) { print(e.code) }
}
')" == $'user.f\nuser.g\nuser.f\nuser.g' ]]

# Import rollback erases newly created command resources before catch.
cat >"$TMP/cmd-imp.f" <<NIFT
c := cmd("printf", "x")
throw error("cmd import failed","user.c")
NIFT
cat >"$TMP/cmd-main.f" <<NIFT
try { import("./cmd-imp.f") } catch(err) { print("caught: " + err.code) }
print("still alive")
NIFT
[[ "$("$NIFT" "$TMP/cmd-main.f")" == $'caught: user.c\nstill alive' ]]

echo 'v4.6 Batch 4 pre-CP4 repairs: PASS'
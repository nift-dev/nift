#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat >"$t/run.f" <<'F'
r := run("sh", "-c", "printf out; printf err >&2; exit 3")
if(r.exit_code != 3) { return "bad exit" }
if(r.stdout != "out") { return "bad stdout" }
if(r.stderr != "err") { return "bad stderr" }
p := cmd("printf", "hello").pipe(cmd("tr", "a-z", "A-Z")).run()
if(p.stdout != "HELLO") { return "bad pipeline" }
setenv("NIFT_V44_ENV", "yes")
e := run("sh", "-c", "printf $NIFT_V44_ENV")
if(e.stdout != "yes") { return "bad env" }
F
"$NIFT" run "$t/run.f" >/dev/null
printf 'printf hello | tr a-z A-Z > %s/out\ncat %s/out\nexit\n' "$t" "$t" | "$NIFT" sh >"$t/shell" 2>/dev/null
grep -q HELLO "$t/shell"
# Shell assignment statements must route to the Nift statement engine, and
# adjacent fd-redirects (2>) must not become a literal argument.
printf 'x := 5\nx = 7\nprint(x)\nexit\n' | "$NIFT" sh 2>/dev/null | grep -qE ' 7$'
printf 'sh -c "echo out; echo err >&2" 2>%s/err.txt\ncat %s/err.txt\nexit\n' "$t" "$t" | "$NIFT" sh 2>/dev/null | grep -q ' err$'
# $[...] interpolation in command arguments and ; command separation.
interp_out=$(printf 'who := "world"\necho hello $[who]\nprintf one ; printf two\nfalse ; printf three\nexit\n' | "$NIFT" sh 2>/dev/null || true)
grep -q 'hello world' <<<"$interp_out"
grep -q 'onetwo' <<<"$interp_out"
grep -q 'three' <<<"$interp_out"
# Background & still fails explicitly rather than being misinterpreted.
bg_out=$(printf 'sleep 1 &\nexit\n' | "$NIFT" sh 2>&1 || true)
grep -q 'background job control is not implemented' <<<"$bg_out"

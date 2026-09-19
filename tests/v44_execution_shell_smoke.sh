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

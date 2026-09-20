#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
echo "MARK exec-shell start"
cat >"$t/run.f" <<'F'
r := run("sh", "-c", "printf out; printf err >&2; exit 3")
if(r.exit_code != 3) { return "bad exit" }
if(r.stdout.trim() != "out") { return "bad stdout" }
if(r.stderr.trim() != "err") { return "bad stderr" }
p := cmd("printf", "hello").pipe(cmd("tr", "a-z", "A-Z")).run()
if(p.stdout.trim() != "HELLO") { return "bad pipeline" }
setenv("NIFT_V44_ENV", "yes")
e := run("sh", "-c", "printf $NIFT_V44_ENV")
if(e.stdout.trim() != "yes") { return "bad env" }
F
rf_out=$("$NIFT" run "$t/run.f" 2>&1) || { echo "run.f failed: $rf_out" >&2; exit 1; }
test -z "$rf_out" || { echo "run.f returned: $rf_out" >&2; exit 1; }
echo "MARK after run.f"
shell_out=$(printf 'printf hello | tr a-z A-Z > %s/out\ncat %s/out\nexit\n' "$t" "$t" | "$NIFT" sh 2>&1 || true)
grep -q HELLO <<<"$shell_out" || { echo "shell pipeline: $shell_out" >&2; exit 1; }
echo "MARK after pipeline"
# Shell assignment statements must route to the Nift statement engine, and
# adjacent fd-redirects (2>) must not become a literal argument.
assign_out=$(printf 'x := 5\nx = 7\nprint(x)\nexit\n' | "$NIFT" sh 2>&1 || true)
grep -qE ' 7$' <<<"$assign_out" || { echo "assign: $assign_out" >&2; exit 1; }
err_out=$(printf 'sh -c "echo out; echo err >&2" 2>%s/err.txt\ncat %s/err.txt\nexit\n' "$t" "$t" | "$NIFT" sh 2>&1 || true)
grep -q ' err$' <<<"$err_out" || { echo "err-redirect: $err_out" >&2; exit 1; }
# $[...] interpolation in command arguments and ; command separation.
interp_out=$(printf 'who := "world"\necho hello $[who]\nprintf one ; printf two\nfalse ; printf three\nexit\n' | "$NIFT" sh 2>/dev/null || true)
grep -q 'hello world' <<<"$interp_out" || { echo "interp: $interp_out" >&2; exit 1; }
grep -q 'onetwo' <<<"$interp_out" || { echo "interp-one-two: $interp_out" >&2; exit 1; }
grep -q 'three' <<<"$interp_out" || { echo "interp-three: $interp_out" >&2; exit 1; }
# Function-call interpolation in command arguments (e.g. $[project_root()])
# must route through command land, not the Nift statement path.
fn_out=$(printf 'print("R1")
echo $[project_root()]
exit\n' | "$NIFT" sh 2>/dev/null || true)
grep -q 'R1' <<<"$fn_out" || { echo "fn-interp: $fn_out" >&2; exit 1; }
# Background & still fails explicitly rather than being misinterpreted.
bg_out=$(printf 'sleep 1 &\nexit\n' | "$NIFT" sh 2>&1 || true)
grep -q 'background job control is not implemented' <<<"$bg_out" || { echo "bg: $bg_out" >&2; exit 1; }

# Bare single-token commands fall through to ordinary external executable/PATH
# resolution (fastfetch, git, env, printf, ...), preserving Nift precedence.
mkdir -p "$t/bin"
cat > "$t/bin/fixtool" <<'B'
#!/bin/sh
echo "fixtool-ran $1"
B
chmod +x "$t/bin/fixtool"
bare_out=$(cd "$t" && printf 'fixtool\n' | PATH="$t/bin:$PATH" "$NIFT" sh 2>/dev/null)
grep -q 'fixtool-ran' <<<"$bare_out" || { echo "$bare_out" >&2; exit 1; }
# bare Nift builtins/values keep precedence: `true` is a Nift boolean, not /bin/true
true_out=$(printf 'true\n' | "$NIFT" sh 2>/dev/null)
grep -qE '(^| )true$' <<<"$true_out" || { echo "$true_out" >&2; exit 1; }
# a shell-scope binding shadows any external executable of the same name
shadow_out=$(printf 'fixtool := "shadowed"\nfixtool\n' | "$NIFT" sh 2>&1 || true)
grep -q '"shadowed"' <<<"$shadow_out" || { echo "shadow: $shadow_out" >&2; exit 1; }
# a deliberately nonexistent bare command reports command not found
nope_out=$(printf 'nonexistentcmdxyz\n' | "$NIFT" sh 2>&1)
grep -q 'command not found: nonexistentcmdxyz' <<<"$nope_out" || { echo "$nope_out" >&2; exit 1; }
# --no-process rejects the external fallback
if printf 'fixtool\n' | PATH="$t/bin:$PATH" NIFT_NO_PROCESS=1 "$NIFT" sh 2>/dev/null | grep -q 'fixtool-ran'; then echo "bare external ran under --no-process" >&2; exit 1; fi
# multi-token external commands (one and multiple arguments)
multi_out=$(printf 'fixtool one\nfixtool one two three\n' | PATH="$t/bin:$PATH" "$NIFT" sh 2>/dev/null)
grep -q 'fixtool-ran one' <<<"$multi_out" || { echo "$multi_out" >&2; exit 1; }

#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) BIN="$NIFT";; *) BIN="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat > "$t/hello.f" <<'F'
print("direct")
F
[ "$("$BIN" "$t/hello.f")" = "direct" ]
# zero args is the REPL; piped input lets the contract exercise it without a PTY
out=$(printf 'x := 4\nx\nexit\n' | "$BIN")
grep -qx '4' <<<"$out"
# v4.5 intentionally removes both wrapper subcommands.
if "$BIN" run "$t/hello.f" >"$t/run.out" 2>&1; then echo 'nift run unexpectedly accepted' >&2; exit 1; fi
grep -q "unknown command 'run' and path does not exist" "$t/run.out"
if "$BIN" sh >"$t/sh.out" 2>&1; then echo 'nift sh unexpectedly accepted' >&2; exit 1; fi
grep -q "unknown command 'sh' and path does not exist" "$t/sh.out"
# A known command wins over a same-named cwd file; explicit spelling selects the file.
mkdir -p "$t/precedence"; printf 'print("file-build")\n' > "$t/precedence/build"
if (cd "$t/precedence" && "$BIN" build >/dev/null 2>&1); then :; fi
[ "$(cd "$t/precedence" && "$BIN" ./build)" = "file-build" ]
cat > "$t/invocation.f" <<'F'
print(cmd)
print(args.join("|"))
F
invocation="$t/invocation.f"
invocation_identity="$invocation"
case "$(uname -s)" in MINGW*|MSYS*) invocation_identity=$(cygpath -am "$invocation");; esac
out=$("$BIN" "$invocation" "hello world" -- --no-process -x)
[ "$(sed -n '1p' <<<"$out")" = "$invocation_identity" ]
[ "$(sed -n '2p' <<<"$out")" = 'hello world|--no-process|-x' ]
# host-owned values are immutable unless deliberately shadowed by declaration.
printf 'cmd = "x"\n' > "$t/mutate.f"
if "$BIN" "$t/mutate.f" >/dev/null 2>&1; then echo 'cmd mutation unexpectedly accepted' >&2; exit 1; fi
printf 'args = []\n' > "$t/mutate.f"
if "$BIN" "$t/mutate.f" >/dev/null 2>&1; then echo 'args mutation unexpectedly accepted' >&2; exit 1; fi
out=$(printf 'cmd\nargs.prettify()\nexit\n' | "$BIN")
grep -q '<repl>' <<<"$out"
# -e is canonical full-program execution; -c is an exact alias.
[ "$("$BIN" -e 'x := 3; print(x * 4)')" = '12' ]
[ "$("$BIN" -c 'x := 3; print(x * 4)')" = '12' ]
[ "$("$BIN" -e $'x := 2\ny := 5\nprint(x + y)')" = '7' ]
[ "$("$BIN" -e 'return 9')" = '9' ]
[ "$("$BIN" -i -c 'exit')" = '' ]
# -i continues in the exact same live parser/runtime after success.
out=$(printf 'twice(6)\nx\ncmd\nargs.join("|")\nexit\n' | "$BIN" -i -e 'x := 41; fn(twice(v)) { return v * 2 }' alpha -- beta)
[ "$out" = $'12\n41\n"<command-line>"\n"alpha|beta"' ]
cat > "$t/interactive.f" <<'F'
x := 17
fn(plus_one(v)) { return v + 1 }
F
interactive="$t/interactive.f"
interactive_identity="$interactive"
case "$(uname -s)" in MINGW*|MSYS*) interactive_identity=$(cygpath -am "$interactive");; esac
out=$(printf 'plus_one(x)\ncmd\nexit\n' | "$BIN" -i "$interactive")
[ "$(sed -n '1p' <<<"$out")" = '18' ]
[ "$(sed -n '2p' <<<"$out")" = "\"$interactive_identity\"" ]
# A failed initial program is terminal: piped follow-up input must not be run as a REPL.
if printf 'print("BAD-REPL")\n' | "$BIN" -i -e 'this is not valid := ' >"$t/fail.out" 2>"$t/fail.err"; then
  echo '-i accepted invalid initial program' >&2; exit 1
fi
! grep -q 'BAD-REPL' "$t/fail.out"
# stdin is an explicit one-shot script source with a stable identity.
out=$(printf 'print(cmd)\nprint(args.join("|"))\n' | "$BIN" - alpha -- beta)
[ "$out" = $'<stdin>\nalpha|beta' ]
printf 'print("redirected")\n' > "$t/stdin.f"
[ "$("$BIN" - < "$t/stdin.f")" = 'redirected' ]
[ "$(printf '' | "$BIN" -)" = '' ]
# Stdin diagnostics retain the synthetic source identity and parser location.
if printf 'break\n' | "$BIN" - >"$t/stdin-bad.out" 2>"$t/stdin-bad.err"; then
  echo 'invalid stdin program unexpectedly succeeded' >&2; exit 1
fi
grep -q '<stdin>:1:' "$t/stdin-bad.err"
# Binary/NUL input is rejected rather than silently truncated or reinterpreted.
python3 - "$BIN" <<'PY_NUL'
import subprocess, sys
p=subprocess.run([sys.argv[1], '-'], input=b'print("before")\x00print("after")\n', stdout=subprocess.PIPE, stderr=subprocess.PIPE)
assert p.returncode != 0
assert b'NUL byte' in p.stderr
assert b'before' not in p.stdout and b'after' not in p.stdout
PY_NUL
# A reasonably large piped source is consumed through EOF without truncation.
python3 - <<'PY_LARGE' | "$BIN" - > "$t/large.out"
for _ in range(5000): print('// padding')
print('print("large-ok")')
PY_LARGE
[ "$(cat "$t/large.out")" = 'large-ok' ]
echo 'PASS v4.5 unified CLI invocation'

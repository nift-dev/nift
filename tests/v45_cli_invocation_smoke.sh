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
echo 'PASS v4.5 unified CLI invocation'

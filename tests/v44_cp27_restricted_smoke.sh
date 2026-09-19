#!/usr/bin/env bash
set -euo pipefail
bin=${1:-./nift}
# --no-process must be accepted by nift sh and deny external process execution.
out=$(printf 'printf hello\nexit\n' | "$bin" sh --no-process 2>&1 || true)
grep -q 'external process execution disabled' <<<"$out"
# The structured cmd() pipeline API must not bypass the restriction.
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat >"$t/bypass.f" <<'F'
p := cmd("echo", "BYPASS").run()
print(p.stdout)
F
if NIFT_NO_PROCESS=1 "$bin" run "$t/bypass.f" >"$t/o" 2>&1; then echo "cmd().run() bypassed --no-process" >&2; exit 1; fi
grep -q 'external process execution disabled' "$t/o"
# Nift-native filesystem operations remain available under the restriction.
cat >"$t/native.f" <<F
touch("$t/ok.txt")
print(exists("$t/ok.txt"))
F
[ "$("$bin" run "$t/native.f" --no-process)" = "true" ]
echo 'PASS v4.4 restricted mode'
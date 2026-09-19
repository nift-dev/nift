#!/usr/bin/env bash
set -euo pipefail
bin=${1:-./nift}
# --no-process must be accepted by nift sh/run/eval/build and deny process
# execution on every script-reachable surface including build hooks.
out=$(printf 'printf hello\nexit\n' | "$bin" sh --no-process 2>&1 || true)
grep -q 'external process execution disabled' <<<"$out"
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
# The structured cmd() pipeline API must not bypass the restriction.
cat >"$t/bypass.f" <<'F'
p := cmd("echo", "BYPASS").run()
print(p.stdout)
F
if NIFT_NO_PROCESS=1 "$bin" run "$t/bypass.f" >"$t/o" 2>&1; then echo "cmd().run() bypassed --no-process" >&2; exit 1; fi
grep -q 'external process execution disabled' "$t/o"
# nift eval honors the restriction.
if NIFT_NO_PROCESS=1 "$bin" eval 'run("echo","x").stdout' >"$t/o2" 2>&1; then echo "eval bypassed --no-process" >&2; exit 1; fi
grep -q 'external process execution disabled' "$t/o2"
# Build hooks are covered: a hook calling run() is denied under --no-process.
mkdir -p "$t/site/.nift" "$t/site/content" "$t/site/templates" "$t/site/public" "$t/site/scripts"
printf '{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/main.html","pre build":"scripts/h.f"}}\n' > "$t/site/.nift/config.json"
printf '{"tracked":[{"name":"/","title":"Home","template":"templates/main.html"}]}\n' > "$t/site/.nift/tracked.json"
printf 'x\n' > "$t/site/content/index.html"
printf '<div>@content</div>\n' > "$t/site/templates/main.html"
printf 'r := run("echo","hi")\nprint("hook-ran")\n' > "$t/site/scripts/h.f"
hb=$(cd "$t/site" && "$bin" build --all --no-process 2>&1 || true)
grep -q 'external process execution disabled' <<<"$hb"
# Nift-native filesystem operations remain available under the restriction.
cat >"$t/native.f" <<F
touch("$t/ok.txt")
print(exists("$t/ok.txt"))
F
[ "$("$bin" run "$t/native.f" --no-process)" = "true" ]
# Filesystem-root restriction confines Nift-native filesystem operations.
mkdir -p "$t/root" "$t/root/inner"
printf 'z\n' > "$t/root/outside.txt"
printf 'in\n' > "$t/root/inner/ok.txt"
cat >"$t/root/inner/t.f" <<'F'
print(open("ok.txt"))
F
[ "$(cd "$t/root/inner" && "$bin" run t.f --fs-root="$t/root/inner")" = "in" ]
cat >"$t/root/inner/escape.f" <<F
print(touch("$t/root/outside.txt"))
F
if (cd "$t/root/inner" && "$bin" run escape.f --fs-root="$t/root/inner") >"$t/e" 2>&1; then echo "fs-root escape allowed" >&2; exit 1; fi
grep -q 'escapes configured filesystem root' "$t/e"
echo 'PASS v4.4 restricted mode'
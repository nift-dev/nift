#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-$(pwd)/nift}
case "$NIFT" in /*) BIN="$NIFT";; *) BIN="$(pwd)/$NIFT";; esac
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/site/.nift/packages/demo/src" "$T/site/.nift" "$T/site/lib"
printf '{"name":"demo","entry":"src/main.f"}\n' > "$T/site/.nift/packages/demo/manifest.json"
cat > "$T/site/.nift/packages/demo/src/main.f" <<'NIFT'
fn(package_double(x)) { return x * 2 }
export(package_double)
NIFT
cc -std=c99 -Wall -Wextra -fPIC -shared tests/ffi/fixture.c -o "$T/site/lib/fixture.so"
cat > "$T/site/tool.f" <<'NIFT'
#!/usr/bin/env nift
@import("demo")
fn(add(x, y)) { return x + y }
fn(inc(m, n)) {
    i := 0
    while(i < n) { m.lock(); v := m.get(); m.set(v + 1); m.unlock(); i += 1 }
    return true
}
lib := ffi_open("lib/fixture.so")
m := mutex(0)
t1 := thread(inc, m, 100)
t2 := thread(inc, m, 100)
a := async(add, 20, 22)
t1.join(); t2.join()
m.lock(); count := m.get(); m.unlock()
print(cmd)
print(args.join(","))
print(target())
print(os())
print(env().get("NIFT_DOGFOOD"))
print(package_double(21))
print(await(a))
print(count)
print(ffi_call(lib, "nift_ffi_add_i64", "i64(i64,i64)", 40, 2))
NIFT
chmod +x "$T/site/tool.f"
out=$(cd "$T/site" && NIFT_DOGFOOD=ok "$BIN" --android tool.f a b)
printf '%s\n' "$out" > "$T/direct.out"
[ "$(sed -n '1p' "$T/direct.out")" = tool.f ]
[ "$(sed -n '2p' "$T/direct.out")" = a,b ]
[ "$(sed -n '3p' "$T/direct.out")" = android ]
case "$(sed -n '4p' "$T/direct.out")" in linux|macos|windows) ;; *) exit 1;; esac
[ "$(sed -n '5p' "$T/direct.out")" = ok ]
[ "$(sed -n '6p' "$T/direct.out")" = 42 ]
[ "$(sed -n '7p' "$T/direct.out")" = 42 ]
[ "$(sed -n '8p' "$T/direct.out")" = 200 ]
[ "$(sed -n '9p' "$T/direct.out")" = 42 ]
# Shebang uses PATH and preserves direct script cmd/args semantics.
PATH="$(dirname "$BIN"):$PATH" shebang=$(cd "$T/site" && NIFT_DOGFOOD=ok ./tool.f z)
[ "$(sed -n '1p' <<<"$shebang")" = ./tool.f ] || [ "$(sed -n '1p' <<<"$shebang")" = tool.f ]
[ "$(sed -n '2p' <<<"$shebang")" = z ]
# stdin-as-source remains one-shot and receives <stdin> cmd.
stdin_out=$(printf 'print(cmd); print(args.length())\n' | "$BIN" -)
[ "$stdin_out" = $'<stdin>\n0' ]
# Interactive job control/pipeline path.
jobs_out=$(printf 'printf dogfood | cat\nsleep 0.03 &\nwait 2\njobs\nexit\n' | "$BIN")
grep -q dogfood <<<"$jobs_out"
grep -Eq '\[[0-9]+\] Done \(0\).*sleep 0.03 &' <<<"$jobs_out"
echo 'v4.5 integration dogfood: PASS'

#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="$ROOT/nift"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

# Long-running persistent REPL: closures, structs, collections, streams,
# imports, cd(), errors and cleanup survive across many commands.
printf 'imported_val := 10\ndouble_it := (x) => x * 2\nexport(imported_val)\nexport(double_it)\n' > "$TMP/lib.nift"
cat > "$TMP/session.txt" <<'EOF'
count := 0
inc := () => ++count
struct(acc) { v := 0; fn(add(n)) { v += n } fn(get()) { return v } }
a := acc()
m := map()
out := ofstream("log.txt")
out.write_line("L1")
close(out)
f := (x) => {
  return x * 2
}
cd("work")
cd("..")
inc()
a.add(5)
m.set("k", 1)
this is broken
inc()
a.add(2)
m.set("k", 2)
@import("lib.nift")
print(inc())
print(a.get())
print(m.get("k"))
print(f(21))
print(imported_val)
print(double_it(5))
print("still-alive")
in := ifstream("log.txt")
print(in.read_line())
close(in)
quit
EOF
mkdir -p "$TMP/work"
out="$(cd "$TMP" && "$NIFT" sh < session.txt 2>/dev/null || true)"
# Strip the bold prompt (path + "$ ") and CRs so values are on their own lines.
cleaned="$(printf '%s' "$out" | sed 's|'$TMP'[^$]*\$ ||g; s|'"'"'[$TMP'"'"'[^$]*\$ ||g' | tr -d '\r')"
# Persistent closure/struct/map values and multiline lambda result.
grep -q $'^3$' <<<"$cleaned"
grep -q $'^7$' <<<"$cleaned"
grep -q $'^2$' <<<"$cleaned"
grep -q $'^42$' <<<"$cleaned"
# Imported live bindings survive in the session.
grep -q $'^10$' <<<"$cleaned"
# Stream opened earlier still reads after intervening commands.
grep -q $'^L1$' <<<"$cleaned"
# An ordinary error did not destroy the session.
grep -q 'still-alive' <<<"$cleaned"

echo 'v4.3 CP88 long-running REPL smoke: PASS'
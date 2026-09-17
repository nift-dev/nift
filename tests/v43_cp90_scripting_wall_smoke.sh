#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="$ROOT/nift"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/data"

# Exported live bindings, closures with compound-assignment bodies, exported
# structs and streams all survive the same run (the sanitizer wall exercised
# the identical workload under ASan/UBSan/LSan and Valgrind).
cat > "$TMP/data/closures.nift" <<'NIFT'
count := 0
inc := () => ++count
dec := () => --count
shared := 10
add_shared := (v) => shared += v
struct(counter) { n := 0; fn(bump()) { n += 1 } }
export(count)
export(inc)
export(dec)
export(shared)
export(add_shared)
export(counter)
NIFT
cat > "$TMP/wall.nift" <<'NIFT'
@import("data/closures.nift")
print(inc())
print(inc())
print(dec())
print(count)
add_shared(5)
add_shared(7)
print(shared)
c := counter()
c.bump()
c.bump()
print(c.stringify())
out := ofstream("w.txt")
out.write_line("hello")
close(out)
in := ifstream("w.txt")
print(in.read_line())
print(in.read_val())
close(in)
NIFT
[[ "$(cd "$TMP" && "$NIFT" run wall.nift)" == $'1\n2\n1\n1\n22\ncounter{n:2}\nhello\nnull' ]]

# A lambda whose body is a compound assignment must not be intercepted by the
# top-level compound-assignment matcher.
cat > "$TMP/compound.nift" <<'NIFT'
total := 0
push := (v) => total += v
push(5)
push(7)
print(total)
NIFT
[[ "$(cd "$TMP" && "$NIFT" run compound.nift)" == '12' ]]

# Failed imports (missing export, import cycle) fail cleanly.
cat > "$TMP/data/missing.nift" <<'NIFT'
x := 1
export(does_not_exist)
NIFT
printf '@import("data/missing.nift")\n' > "$TMP/bad1.nift"
if (cd "$TMP" && "$NIFT" run bad1.nift >/dev/null 2>&1); then echo 'missing export import succeeded' >&2; exit 1; fi
cat > "$TMP/data/cyc-a.nift" <<'NIFT'
@import("cyc-b.nift")
NIFT
cat > "$TMP/data/cyc-b.nift" <<'NIFT'
@import("cyc-a.nift")
NIFT
printf '@import("data/cyc-a.nift")\n' > "$TMP/bad2.nift"
if (cd "$TMP" && "$NIFT" run bad2.nift >/dev/null 2>&1); then echo 'import cycle succeeded' >&2; exit 1; fi

echo 'v4.3 CP90 scripting sanitizer-wall smoke: PASS'
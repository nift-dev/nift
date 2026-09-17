#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NIFT="${NIFT_BIN:-$ROOT/nift}"
td="$(mktemp -d)"; trap 'rm -rf "$td"' EXIT
printf 'alpha foo\n42 true\nomega\n' > "$td/a.txt"
cat > "$td/main.nift" <<'NIFT'
f := file("a.txt")
print(f.path().ends_with("/a.txt"))
print(f.exists())
f.open("rw")
print(f.read_line())
print(f.tell())
f.seek(0)
print(f.read(5))
f.seek(0)
f.replace_once("foo", "bar")
f.insert_after("bar", "!")
f.prepend(">")
f.append("<")
print(f.modified())
f.save()
print(f.modified())
f.seek(0)
print(f.read_line())
f.revert()
f.close()
b := file("a.txt").copy("b.txt")
print(b.exists())
m := b.move("c.txt")
print(m.exists())
m.remove()
print(m.exists())
NIFT
out="$(cd "$td" && "$NIFT" run main.nift)"
[[ "$out" == $'true\ntrue\nalpha foo\n10\nalpha\ntrue\nfalse\n>alpha bar!\ntrue\ntrue\nfalse' ]]
[[ "$(cat "$td/a.txt")" == $'>alpha bar!\n42 true\nomega\n<' ]]

# Dirty close is fail-closed and leaves disk unchanged until save.
printf 'f := file("a.txt")\nf.open("rw")\nf.replace_once("bar", "BAD")\nf.close()\n' > "$td/dirty.nift"
if (cd "$td" && "$NIFT" run dirty.nift >"$td/o" 2>"$td/e"); then echo "dirty close unexpectedly succeeded" >&2; exit 1; fi
grep -q 'unsaved changes' "$td/e"
grep -q 'bar' "$td/a.txt"; ! grep -q BAD "$td/a.txt"

# Ambiguous surgical edits fail without mutation.
printf 'x x\n' > "$td/amb.txt"
printf 'f := file("amb.txt")\nf.open("rw")\nf.replace_once("x", "y")\n' > "$td/amb.nift"
if (cd "$td" && "$NIFT" run amb.nift >/dev/null 2>"$td/e"); then echo "ambiguous replace unexpectedly succeeded" >&2; exit 1; fi
[[ "$(cat "$td/amb.txt")" == 'x x' ]]

# w/a creation is transactional; revert makes a clean close legal.
cat > "$td/create.nift" <<'NIFT'
f := file("new.txt")
f.open("w")
print(f.modified())
f.write_line("hello")
f.save()
f.close()
a := file("append.txt")
a.open("a")
a.write("tail")
a.save()
a.close()
r := file("a.txt")
r.open("rw")
r.append("discard")
r.revert()
print(r.modified())
r.close()
NIFT
[[ "$(cd "$td" && "$NIFT" run create.nift)" == $'true\nfalse' ]]
[[ "$(cat "$td/new.txt")" == hello ]]
[[ "$(cat "$td/append.txt")" == tail ]]
! grep -q discard "$td/a.txt"

# Host termination reports an open FileValue and discards dirty state.
printf 'f := file("a.txt")\nf.open("rw")\nf.append("LEAK")\n' > "$td/leak.nift"
if (cd "$td" && "$NIFT" run leak.nift >/dev/null 2>"$td/e"); then echo "open FileValue unexpectedly accepted" >&2; exit 1; fi
grep -q 'managed file left open' "$td/e"
! grep -q LEAK "$td/a.txt"

# Mode and lifecycle violations.
for body in \
  'f := file("a.txt"); f.read_all()' \
  'f := file("a.txt"); f.open(); f.write("x")' \
  'f := file("a.txt"); f.open("w"); f.read_all()' \
  'f := file("a.txt"); f.open(); f.open()'
do
  printf '%s\n' "$body" | tr ';' '\n' > "$td/bad.nift"
  if (cd "$td" && "$NIFT" run bad.nift >/dev/null 2>&1); then echo "lifecycle violation unexpectedly succeeded: $body" >&2; exit 1; fi
done


# @import and @script are resource boundaries too.
cat > "$td/mod.nift" <<'NIFT'
f := file("a.txt")
f.open("rw")
NIFT
printf '@import("mod.nift")\n' > "$td/importer.nift"
if (cd "$td" && "$NIFT" run importer.nift >/dev/null 2>"$td/e"); then echo "import leaked FileValue unexpectedly" >&2; exit 1; fi
grep -q '@import completion' "$td/e"

echo 'CP105-CP113 managed FileValue smoke: PASS'

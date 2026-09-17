#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="$ROOT/nift"
TMP=$(mktemp -d); trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP"

# Stale-cursor adversarial: shrink below cursor then read/write must not crash.
printf 'abcdef' > c.txt
cat > cursor.nift <<'NIFT'
f := file("c.txt")
f.open("rw")
f.seek(4)
f.replace_once("abcdef", "x")
print(f.tell())
print(f.read_all() == "")
f.write("Z")
f.seek(0)
print(f.read_all())
print(f.eof())
f.revert()
f.close()
g := file("c.txt")
g.open("rw")
g.seek(5)
g.insert(0, "PRE-")
print(g.tell())
g.seek(0)
print(g.read_all())
g.revert()
g.close()
NIFT
[[ "$("$NIFT" run cursor.nift)" == $'1\ntrue\nxZ\ntrue\n5\nPRE-abcdef' ]]

# Save failure: missing parent and read-only directory keep disk/working/dirty
# coherent; retry after the permission is restored succeeds; no orphan temps.
mkdir -p rodir; printf 'DATA' > rodir/f.txt; chmod 555 rodir
cat > ro.nift <<'NIFT'
f := file("rodir/f.txt")
f.open("rw")
f.replace_once("DATA", "CHANGED")
f.save()
NIFT
if "$NIFT" run ro.nift >/dev/null 2>&1; then echo "ro save succeeded" >&2; exit 1; fi
[[ "$(cat rodir/f.txt)" == 'DATA' ]]
chmod 755 rodir
cat > retry.nift <<'NIFT'
f := file("rodir/f.txt")
f.open("rw")
f.replace_once("DATA", "CHANGED")
f.save()
f.close()
print(open("rodir/f.txt"))
NIFT
[[ "$("$NIFT" run retry.nift)" == 'CHANGED' ]]
[[ -z "$(ls .nift-tmp-* 2>/dev/null || true)" ]]

# Dirty retention after a genuine failed save (REPL error recovery).
printf 'BODY' > m.txt
mkdir -p rodir2; printf 'BODY' > rodir2/m.txt; chmod 555 rodir2
repl=$(cd "$TMP" && printf 'f := file("rodir2/m.txt")\nf.open("rw")\nf.replace_once("BODY", "E")\nf.save()\nf.modified()\nf.revert()\nf.modified()\nf.close()\nprint("alive")\nquit\n' | "$NIFT" sh 2>&1 || true)
grep -q 'save: cannot create temporary file' <<<"$repl"
grep -q 'true' <<<"$repl"
grep -q 'alive' <<<"$repl"
chmod 755 rodir2

# Host teardown never saves: multiple open files at exit error, disk unchanged.
printf 'KEEP' > keep.txt
cat > he.nift <<'NIFT'
f := file("keep.txt")
g := file("keep2.txt")
f.open("rw")
g.open("w")
f.replace_once("KEEP", "GONE")
g.write("x")
NIFT
if "$NIFT" run he.nift >/dev/null 2>&1; then echo "open dirty exit ok" >&2; exit 1; fi
[[ "$(cat keep.txt)" == 'KEEP' ]]
[[ ! -e keep2.txt ]]

# Independent same-path sessions have independent working copies; save through
# one does not affect the other's working state.
printf 'BASE' > s.txt
cat > same.nift <<'NIFT'
a := file("s.txt")
b := file("s.txt")
print(same(a, b))
a.open("rw")
b.open("rw")
a.replace_once("BASE", "AAAA")
b.replace_once("BASE", "BBBB")
print(a.read_all())
print(b.read_all())
a.save()
print(open("s.txt"))
b.revert()
print(b.read_all())
print(open("s.txt"))
a.close()
b.close()
NIFT
[[ "$("$NIFT" run same.nift)" == $'false\nAAAA\nBBBB\nAAAA\nBASE\nAAAA' ]]

# Repeated save cycles leave no orphan temporaries and commit only on save.
printf 'V' > v.txt
cat > cycles.nift <<'NIFT'
f := file("v.txt")
f.open("rw")
for(i : [1,2,3,4,5]) {
  f.replace_once("V", "W" + i)
  f.save()
  f.replace_once("W" + i, "V")
}
f.revert()
f.close()
NIFT
[[ "$("$NIFT" run cycles.nift)" == '' ]]
[[ "$(cat v.txt)" == 'W5' ]]
[[ -z "$(ls v.txt.nift-tmp-* 2>/dev/null || true)" ]]

# read_val parity with ifstream.
printf 'true 42 3.5 "s" [1,2,3] null' > vals.txt
cat > rv.nift <<'NIFT'
f := file("vals.txt")
f.open("r")
print(f.read_val())
print(f.read_val())
print(f.read_val())
print(f.read_val())
a := f.read_val()
print(a.join(","))
print(f.read_val() == null)
f.close()
s := ifstream("vals.txt")
print(s.read_val())
print(s.read_val())
print(s.read_val())
print(s.read_val())
b := s.read_val()
print(b.join(","))
print(s.read_val() == null)
close(s)
NIFT
[[ "$("$NIFT" run rv.nift)" == $'true\n42\n3.5\ns\n1,2,3\ntrue\ntrue\n42\n3.5\ns\n1,2,3\ntrue' ]]

echo 'v4.3 CP112 FileValue adversarial/resource smoke: PASS'
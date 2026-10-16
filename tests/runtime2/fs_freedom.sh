#!/usr/bin/env bash
# CP03 (decision D-002): Nift programs have ordinary filesystem semantics.
# Page renders, @script blocks and build hooks may read and write outside the
# project (absolute paths, '..', symlinks, neighbouring repositories); external
# files are tracked as dependencies without perpetual rebuilds; protections for
# Nift-owned outputs remain; --fs-root stays an explicit opt-in restriction.
set -u
NIFT="${NIFT_BIN:-$(pwd)/nift}"
fail=0
pass() { echo "PASS $1"; }
bad() { echo "FAIL $1"; fail=1; }
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
mkdir -p "$W/project" "$W/neighbour/data" "$W/elsewhere"
echo 'NEIGHBOUR-1' > "$W/neighbour/data/note.txt"
echo '{"title": "external json"}' > "$W/neighbour/data/info.json"
printf '# External markdown\n' > "$W/neighbour/data/doc.md"
ln -s "$W/neighbour/data" "$W/project-link-target" 2>/dev/null
cd "$W/project" && "$NIFT" init >/dev/null 2>&1 || { echo "setup failed"; exit 2; }
ln -s "$W/neighbour/data" linked-data

cat > content/index.html <<EOF
ABS:@input("$W/neighbour/data/note.txt")
REL:@input("../neighbour/data/note.txt")
LINK:@input("linked-data/note.txt")
@json(info, "../neighbour/data/info.json")JSON:\$[info.title]
MD:@markup("md", "../neighbour/data/doc.md")
EXISTS:@script { return exists("$W/neighbour/data/note.txt") }
LS:@script { return ls("../neighbour/data/*.txt").length() }
WRITE:@script { f := file("$W/elsewhere/out.txt"); f.open("w"); f.write("written"); f.save(); f.close(); return "ok" }
EOF
if "$NIFT" build --all >/dev/null 2>"$W/err"; then pass "build reading/writing outside the project"; else bad "build failed: $(head -3 "$W/err")"; fi
for want in "ABS:NEIGHBOUR-1" "REL:NEIGHBOUR-1" "LINK:NEIGHBOUR-1" "JSON:external json" "External markdown" "EXISTS:true" "LS:1" "WRITE:ok"; do
  grep -q -- "$want" public/index.html && pass "output has $want" || bad "missing $want"
done
[ "$(cat "$W/elsewhere/out.txt" 2>/dev/null)" = written ] && pass "script wrote outside the project" || bad "external write missing"

if "$NIFT" status | grep -q "up to date"; then pass "external dependencies do not force rebuilds"; else bad "perpetual rebuild: $("$NIFT" status | sed -n 2,4p | tr '\n' ' ')"; fi
sleep 1; echo 'NEIGHBOUR-2' > "$W/neighbour/data/note.txt"
"$NIFT" build >/dev/null 2>&1
grep -q "ABS:NEIGHBOUR-2" public/index.html && pass "external edit triggers rebuild" || bad "external edit missed"
echo 'x' > "$W/neighbour/data/new.txt"; "$NIFT" build >/dev/null 2>&1
grep -q "LS:2" public/index.html && pass "external glob change triggers rebuild" || bad "external glob change missed"

# Standalone script: absolute, parent traversal, symlinks.
printf 'print(cat("%s/neighbour/data/note.txt"))\nprint(exists("../neighbour/data"))\n' "$W" > "$W/project/s.f"
out="$("$NIFT" s.f 2>&1)"; echo "$out" | grep -q NEIGHBOUR-2 && pass "standalone absolute read" || bad "standalone read: $out"

# --fs-root remains an explicit restriction.
if "$NIFT" --fs-root="$W/project" s.f >/dev/null 2>&1; then bad "--fs-root did not restrict"; else pass "--fs-root still restricts when requested"; fi

# Nift-owned state stays protected: untracking/removing never touches external files.
"$NIFT" rm index >/dev/null 2>&1 || "$NIFT" rm / >/dev/null 2>&1
[ -f "$W/neighbour/data/note.txt" ] && [ -f "$W/elsewhere/out.txt" ] && pass "rm leaves external files alone" || bad "rm removed external files"
exit $fail

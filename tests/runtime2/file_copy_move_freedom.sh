#!/usr/bin/env bash
# feature-host-deps (D-002/D-028): file().copy() and file().move() in page
# renders follow normal OS path semantics like cp()/mv(): absolute paths,
# parent traversal, external directories and symlinks work. Before this
# checkpoint they failed with "<method>: path must stay inside the Nift
# project". The opt-in --fs-root restriction and Nift-owned state stay
# protected.
set -u -o pipefail
NIFT_REAL="${NIFT_BIN:-$(pwd)/nift}"
# Standing rule: every nift run is bounded in disk, memory and time.
NIFT() { ( ulimit -f 262144; ulimit -v 2097152; exec timeout 60 "$NIFT_REAL" "$@" ); }
NIFT=NIFT
fail=0
pass() { echo "PASS $1"; }
bad() { echo "FAIL $1"; fail=1; }
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
mkdir -p "$W/project" "$W/neighbour" "$W/elsewhere" "$W/linked-target"
cd "$W/project" && "$NIFT" init >/dev/null 2>&1 || { echo "setup failed"; exit 2; }
ln -s "$W/linked-target" linked-dir
echo 'link-old' > "$W/linked-target/through.txt"
ln -s "$W/linked-target/through.txt" through-link.txt
mkdir -p data
for n in abs rel link move inner over intodir; do echo "src-$n" > "data/$n.txt"; done
mkdir -p "$W/elsewhere/dir"

cat > content/index.html <<EOF
ABS:@script { a := file("data/abs.txt"); b := a.copy("$W/elsewhere/abs-copy.txt"); return b.path() }
REL:@script { c := file("data/rel.txt"); d := c.copy("../neighbour/rel-copy.txt"); return d.path() }
LINK:@script { e := file("data/link.txt"); g := e.copy("linked-dir/link-copy.txt"); return g.path() }
MOVE:@script { h := file("data/move.txt"); k := h.move("$W/elsewhere/moved.txt"); return k.path() }
INNER:@script { m := file("data/inner.txt"); p := m.copy("data/inner-copy.txt"); return p.path() }
OVER:@script { q := file("data/over.txt"); r := q.copy("through-link.txt"); return "ok" }
INTODIR:@script { s := file("data/intodir.txt"); t := s.copy("$W/elsewhere/dir"); return t.path() }
EOF
if "$NIFT" build --all >"$W/out" 2>&1; then pass "page build with external file().copy/move"; else bad "build failed: $(grep -m3 . "$W/out")"; fi
grep -q "path must stay inside the Nift project" "$W/out" && bad "containment error still reported"
for want in "ABS:$W/elsewhere/abs-copy.txt" "REL:$W/neighbour/rel-copy.txt" "LINK:$W/project/linked-dir/link-copy.txt" \
            "MOVE:$W/elsewhere/moved.txt" "INNER:$W/project/data/inner-copy.txt" "OVER:ok" "INTODIR:$W/elsewhere/dir/intodir.txt"; do
  grep -qF -- "$want" public/index.html && pass "output has ${want%%:*}" || bad "missing $want"
done
[ "$(cat "$W/elsewhere/abs-copy.txt" 2>/dev/null)" = src-abs ] && pass "absolute copy written" || bad "absolute copy missing"
[ "$(cat "$W/neighbour/rel-copy.txt" 2>/dev/null)" = src-rel ] && pass "parent-traversal copy written" || bad "parent copy missing"
[ "$(cat "$W/linked-target/link-copy.txt" 2>/dev/null)" = src-link ] && pass "copy through a symlinked directory lands in the target" || bad "symlinked dir copy missing"
[ "$(cat "$W/elsewhere/moved.txt" 2>/dev/null)" = src-move ] && [ ! -e data/move.txt ] && pass "move to an external directory" || bad "external move"
[ "$(cat "$W/elsewhere/dir/intodir.txt" 2>/dev/null)" = src-intodir ] && pass "file().copy into an existing directory keeps the name (as cp())" || bad "copy into directory"
[ -L through-link.txt ] && [ "$(cat "$W/linked-target/through.txt")" = src-over ] && pass "copy onto a symlinked file writes through it (as cp())" || bad "symlinked destination file"

# --fs-root remains an explicit, opt-in restriction (standalone scripts).
cat > "$W/project/s.f" <<EOF
f := file("$W/project/data/abs.txt")
g := f.copy("$W/elsewhere/fsroot-copy.txt")
print("copied")
EOF
out="$("$NIFT" "$W/project/s.f" --fs-root="$W/project" 2>&1)"
if echo "$out" | grep -q "path escapes configured filesystem root" && [ ! -e "$W/elsewhere/fsroot-copy.txt" ]; then
  pass "--fs-root still restricts file().copy"
else bad "--fs-root did not restrict file().copy: $out"; fi
out="$("$NIFT" "$W/project/s.f" 2>&1)"; echo "$out" | grep -q copied && pass "standalone external file().copy" || bad "standalone copy: $out"

# Cross-device moves (EXDEV) fall back to copy + rename + unlink, for
# file().move() and mv(), when /dev/shm is a different filesystem.
if [ -d /dev/shm ] && [ -w /dev/shm ] && [ "$(stat -c %d /dev/shm)" != "$(stat -c %d "$W")" ]; then
  S="$(mktemp -d /dev/shm/nift-xdev-XXXXXX)"
  echo xdev-a > "$W/xa.txt"; echo xdev-b > "$W/xb.txt"; chmod 640 "$W/xa.txt"
  printf 'f := file("%s")\ng := f.move("%s")\nmv("%s", "%s")\nprint(g.path())\n' "$W/xa.txt" "$S/xa.txt" "$W/xb.txt" "$S/xb.txt" > "$W/x.f"
  out="$("$NIFT" "$W/x.f" 2>&1)"
  if [ "$out" = "$S/xa.txt" ] && [ "$(cat "$S/xa.txt")" = xdev-a ] && [ "$(cat "$S/xb.txt")" = xdev-b ] && [ ! -e "$W/xa.txt" ] && [ ! -e "$W/xb.txt" ] \
     && [ "$(stat -c %a "$S/xa.txt")" = 640 ] && ! ls -A "$S" | grep -q nift-tmp; then
    pass "cross-device file().move and mv() (to /dev/shm)"
  else bad "cross-device move: $out; $(ls -la "$S" | tr '\n' ' ')"; fi
  rm -rf "$S"
else echo "SKIP cross-device move (/dev/shm missing or on the same filesystem)"; fi

# Writes into the project's .nift/ are refused loudly during a build
# (feature-host-deps interim protection; D-006 follow-up in the record).
cp .nift/tracked.json "$W/tracked.before"
for api in 'a := file("data/abs.txt"); b := a.copy(".nift/tracked.json")' 'cp("data/abs.txt", ".nift/tracked.json")' 'touch(".nift/x")' 'o := ofstream(".nift/y"); close(o)'; do
  printf '<p>@script { %s; return "wrote" }</p>\n' "$api" > content/index.html
  out="$("$NIFT" build 2>&1)"; rc=$?
  if [ $rc -ne 0 ] && echo "$out" | grep -q "refusing to write Nift build metadata" && cmp -s .nift/tracked.json "$W/tracked.before" && [ ! -e .nift/x ] && [ ! -e .nift/y ]; then
    pass "refused: $api"
  else bad "write into .nift not refused ($api): rc=$rc $out"; fi
done
printf '<p>ok</p>\n' > content/index.html; "$NIFT" build >/dev/null 2>&1 || bad "build after refused writes"

# Nift-owned state stays protected: untracking never touches external copies.
# (The index page is tracked as "/"; `nift rm index` untracks nothing and
# still exits 0.)
"$NIFT" rm / >/dev/null 2>&1
if grep -q '"name"' .nift/tracked.json; then bad "untracking did not happen"; else pass "index untracked"; fi
[ -f "$W/elsewhere/abs-copy.txt" ] && [ -f "$W/elsewhere/moved.txt" ] && [ -f "$W/linked-target/link-copy.txt" ] \
  && pass "untracking leaves external copies alone" || bad "untracking removed external files"
exit $fail

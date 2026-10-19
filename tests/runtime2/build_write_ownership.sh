#!/usr/bin/env bash
# feature-host-deps, owner decision on D-006: during a website build, script
# writes may not overwrite or remove another tracked page's build output, and
# may not write into the project's .nift/ metadata, whatever the spelling
# (symlink, `..`, alternative path) or write API. The page's own outputs,
# unowned files (also inside public/) and paths outside the project stay
# writable. Before this checkpoint every case below silently corrupted the
# other page's output while `nift status` reported it up to date.
#
# Also: cross-filesystem moves (R5) keep the original when the copy fails and
# report a partial result when the source cannot be removed afterwards.
#
# Every nift run is bounded: ulimit -f (disk), ulimit -v (memory), timeout.
set -u -o pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
fail=0
pass() { echo "PASS $1"; }
bad() { echo "FAIL $1"; fail=1; }
bounded() { ( ulimit -f 262144; ulimit -v 2097152; exec timeout 60 "$@" ); }
W="$(mktemp -d)"; trap 'chmod -R u+w "$W" 2>/dev/null; rm -rf "$W"' EXIT
cd "$W" && mkdir project outside && cd project || exit 2
bounded "$NIFT" init >/dev/null 2>&1 || { echo "setup failed"; exit 2; }
bounded "$NIFT" track other Other >/dev/null 2>&1
echo 'OTHER-BODY' > content/other.html
echo 'payload' > payload.txt
ln -s public linked-public
printf '<p>start</p>\n' > content/index.html
bounded "$NIFT" build --all >/dev/null 2>&1 || { echo "initial build failed"; exit 2; }
cp public/other.html "$W/other.good"

refused() { # name, script body
  cp "$W/other.good" public/other.html
  printf '<p>X:@script { %s; return "wrote" }</p>\n' "$2" > content/index.html
  out="$(bounded "$NIFT" build 2>&1)"; rc=$?
  if [ $rc -ne 0 ] && echo "$out" | grep -q "the build output of tracked page 'other'" && cmp -s public/other.html "$W/other.good"; then
    pass "refused: $1"
  else
    bad "not refused: $1 (rc=$rc, other.html $(cmp -s public/other.html "$W/other.good" && echo intact || echo CHANGED)): $(echo "$out" | grep -m2 -i 'error\|refus')"
  fi
}
refused "file().copy onto another page's output" 'a := file("payload.txt"); b := a.copy("public/other.html")'
refused "cp() onto another page's output" 'cp("payload.txt", "public/other.html")'
refused "cp() through a symlinked directory" 'cp("payload.txt", "linked-public/other.html")'
refused "cp() through a .. spelling" 'cp("payload.txt", "content/../public/./other.html")'
refused "cp() through an absolute spelling" "cp(\"payload.txt\", \"$W/project/public/other.html\")"
ln -sf "$W/project/public/other.html" "$W/outside/innocent.html"
refused "cp() onto a symlink outside the project that points at it" "cp(\"payload.txt\", \"$W/outside/innocent.html\")"
refused "rm() of another page's output" 'rm("public/other.html")'
refused "file().remove of another page's output" 'a := file("public/other.html"); a.remove()'
refused "mv() of another page's output away" 'mv("public/other.html", "moved.html")'
refused "file().move of another page's output away" 'a := file("public/other.html"); b := a.move("moved.html")'
refused "mv() onto another page's output" 'mv("payload.txt", "public/other.html")'
refused "file().open(w) + save() of another page's output" 'a := file("public/other.html"); a.open("w"); a.write("x"); a.save(); a.close()'
refused "ofstream onto another page's output" 'o := ofstream("public/other.html"); close(o)'
refused "touch() of another page's output" 'touch("public/other.html")'
refused "cmd().stdout() redirect onto another page's output" 'r := cmd("true").stdout("public/other.html").run()'
cp "$W/other.good" public/other.html
[ -f payload.txt ] && pass "refused moves left the source in place" || bad "a refused move consumed its source"

allowed() { # name, script body, check command
  printf '<p>X:@script { %s; return "wrote" }</p>\n' "$2" > content/index.html
  out="$(bounded "$NIFT" build 2>&1)"; rc=$?
  if [ $rc -eq 0 ] && eval "$3"; then pass "allowed: $1"; else bad "should be allowed: $1 (rc=$rc): $(echo "$out" | grep -m2 -i 'error\|refus')"; fi
}
allowed "unowned file inside public/" 'cp("payload.txt", "public/extra.txt")' '[ "$(cat public/extra.txt)" = payload ]'
allowed "path outside the project" "cp(\"payload.txt\", \"$W/outside/copy.txt\")" '[ "$(cat "$W/outside/copy.txt")" = payload ]'
allowed "the page's own output (rewritten by the build afterwards)" 'cp("payload.txt", "public/index.html")' 'grep -q "X:wrote" public/index.html'
allowed "an unowned file next to the outputs, removed" 'rm("public/extra.txt")' '[ ! -e public/extra.txt ]'
cmp -s public/other.html "$W/other.good" && pass "other page's output intact after all cases" || bad "other page's output changed"
st="$(bounded "$NIFT" status 2>&1)"; echo "$st" | grep -q "up to date" && pass "status consistent afterwards" || bad "status: $st"

# .nift/ metadata stays refused (spelling-independent too).
ln -s .nift linked-meta
for body in 'cp("payload.txt", ".nift/tracked.json")' 'cp("payload.txt", "linked-meta/tracked.json")' 'rm(".nift/config.json")'; do
  cp .nift/tracked.json "$W/tracked.good"; cp .nift/config.json "$W/config.good"
  printf '<p>@script { %s; return "x" }</p>\n' "$body" > content/index.html
  out="$(bounded "$NIFT" build 2>&1)"; rc=$?
  if [ $rc -ne 0 ] && echo "$out" | grep -q "refusing to write Nift build metadata" && cmp -s .nift/tracked.json "$W/tracked.good" && cmp -s .nift/config.json "$W/config.good"; then
    pass "refused metadata write: $body"
  else bad "metadata write not refused: $body (rc=$rc)"; fi
done
printf '<p>ok</p>\n' > content/index.html; bounded "$NIFT" build >/dev/null 2>&1 || bad "build after refusals"

# Standalone scripts own no pages: unaffected.
printf 'cp("%s/project/payload.txt", "%s/project/public/scratch.txt")\nprint("ok")\n' "$W" "$W" > "$W/s.f"
[ "$(bounded "$NIFT" "$W/s.f" 2>&1)" = ok ] && pass "standalone scripts are not restricted" || bad "standalone script restricted"

# R5: cross-filesystem move partial-failure semantics.
if [ -d /dev/shm ] && [ -w /dev/shm ] && [ "$(stat -c %d /dev/shm)" != "$(stat -c %d "$W")" ]; then
  S="$(mktemp -d /dev/shm/nift-xdev-XXXXXX)"
  # (a) the copy fails (file size limit): the original stays, no temporary is left.
  head -c 300000 /dev/zero > "$W/big.bin"
  printf 'try { mv("%s", "%s/big.bin") } catch(e) { print("CAUGHT " + e.code) }\n' "$W/big.bin" "$S" > "$W/m1.f"
  # SIGXFSZ ignored so the over-size write fails with EFBIG instead of killing nift.
  out="$( trap '' XFSZ; ulimit -f 200; ulimit -v 2097152; timeout 60 "$NIFT" "$W/m1.f" 2>&1 )"
  if echo "$out" | grep -q "CAUGHT io.move_failed" && [ "$(stat -c %s "$W/big.bin")" = 300000 ] && [ ! -e "$S/big.bin" ] && ! ls -A "$S" | grep -q nift-tmp; then
    pass "cross-device move: failed copy leaves the original intact"
  else bad "cross-device move copy failure: $out; $(ls -la "$S" | tr '\n' ' ')"; fi
  # (b) the copy succeeds but the source cannot be removed: reported as a partial result.
  if [ "$(id -u)" != 0 ] || command -v setpriv >/dev/null; then
    mkdir "$W/ro"; echo keep > "$W/ro/f.txt"; chmod 555 "$W/ro"
    printf 'try { mv("%s", "%s/f.txt") } catch(e) { print("CAUGHT " + e.message) }\n' "$W/ro/f.txt" "$S" > "$W/m2.f"
    PRIV=(); [ "$(id -u)" = 0 ] && PRIV=(setpriv --bounding-set -dac_override,-dac_read_search)
    out="$(bounded "${PRIV[@]}" "$NIFT" "$W/m2.f" 2>&1)"
    if echo "$out" | grep -q "CAUGHT move: copied to $S/f.txt but could not remove the source $W/ro/f.txt" && [ -f "$W/ro/f.txt" ] && [ "$(cat "$S/f.txt")" = keep ]; then
      pass "cross-device move: undeletable source is reported as a partial result"
    else bad "cross-device move partial: $out"; fi
    chmod 755 "$W/ro"
  else echo "SKIP partial cross-device move (root without setpriv)"; fi
  rm -rf "$S"
else echo "SKIP cross-device move cases (/dev/shm missing or on the same filesystem)"; fi
exit $fail

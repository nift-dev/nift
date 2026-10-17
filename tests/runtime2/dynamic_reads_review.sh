#!/usr/bin/env bash
# CP02/CP03 review regressions (same-model adversarial review, 2026-10-17).
# Each block names the finding it guards.
set -u -o pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
fail=0
ok() { echo "PASS [$mode] $1"; }
bad() { echo "FAIL [$mode] $1"; fail=1; }
stale() { ! "$NIFT" status 2>&1 | grep "up to date" >/dev/null; }
fresh() { "$NIFT" status 2>&1 | grep "up to date" >/dev/null; }
for mode in modified hash; do
  T="$(mktemp -d)"; cd "$T" && "$NIFT" init >/dev/null 2>&1 || { echo "setup failed"; exit 2; }
  sed -i "s/\"incremental-mode\": \"modified\"/\"incremental-mode\": \"$mode\"/" .nift/config.json
  mkdir -p data inc posts
  echo v1 > data/x.txt; echo hello > data/s.txt; echo c1 > data/c.txt; echo ROOTPART > part.html
  echo '@input("part.html")' > inc/wrap.html
  cat > content/index.html <<'EOF'
FEX:@script { fy := file("data/y.txt"); return fy.exists() }
OPEN:@script { return open("data/x.txt") }
SIZE:@script { return stat("data/s.txt").size }
FREAD:@script { fc := file("data/c.txt"); fc.open("r"); tc := fc.read(); fc.close(); return tc }
TYPE:@script { return is_file("data/t") }
TMP:@script { touch("data/scratch.tmp"); e := exists("data/scratch.tmp"); rm("data/scratch.tmp"); return e }
ENV:@script { envsnap := env(); return "ok" }
@input("inc/wrap.html")
EOF
  "$NIFT" build --all >/dev/null 2>&1
  # #1 env() fingerprint ignores per-invocation names; #7 page's own temp files.
  fresh && ok "env() and temp files leave the page up to date" || bad "spurious rebuild after build: $("$NIFT" status | sed -n 2,4p | tr '\n' ' ')"
  (cd content && "$NIFT" status 2>&1 | grep "up to date" >/dev/null) && ok "status from a subdirectory agrees" || bad "status from subdirectory reports stale"
  # #3 unrecorded reads
  sleep 1
  echo yes > data/y.txt; stale && ok "file().exists() appearance" || bad "file().exists() missed"; "$NIFT" build >/dev/null 2>&1
  echo v2 > data/x.txt; stale && ok "open() edit" || bad "open() edit missed"; "$NIFT" build >/dev/null 2>&1
  echo "hello, longer" > data/s.txt; stale && ok "stat() size change" || bad "stat() size change missed"; "$NIFT" build >/dev/null 2>&1
  echo c2 > data/c.txt; stale && ok "file().read() edit" || bad "file().read() edit missed"; "$NIFT" build >/dev/null 2>&1
  mkdir data/t; "$NIFT" build >/dev/null 2>&1; rmdir data/t; echo f > data/t
  stale && ok "path type change" || bad "directory->file type change missed"; "$NIFT" build >/dev/null 2>&1
  grep -q "TYPE:true" public/index.html && ok "type change rebuilt output" || bad "TYPE output stale"
  # #4 @input shadowing
  echo SHADOWPART > inc/part.html; stale && ok "@input shadowing file appears" || bad "@input shadowing missed"
  "$NIFT" build >/dev/null 2>&1; grep -q SHADOWPART public/index.html && ok "shadowed include rendered" || bad "shadowed include not rendered"
  fresh && ok "up to date after the last change" || bad "spurious rebuild at end"
  cd /; rm -rf "$T"
done

# #5 reads in per-item build scripts invalidate the page.
mode=hook
T="$(mktemp -d)"; cd "$T" && "$NIFT" init >/dev/null 2>&1 || exit 2
mkdir -p posts gen; echo a > posts/a.md
printf 'n := ls("posts/*.md").length()\nf := file("gen/count.html"); f.open("w"); f.write("COUNT" + n); f.save(); f.close()\n' > content/index.pre-build.f
echo '@input("gen/count.html")' > content/index.html
"$NIFT" build >/dev/null 2>&1; "$NIFT" build >/dev/null 2>&1
fresh && ok "pre-build hook page up to date" || bad "pre-build hook page stale after build"
echo b > posts/b.md; stale && ok "listing read in a pre-build hook" || bad "hook listing change missed"
"$NIFT" build >/dev/null 2>&1; grep -q COUNT2 public/index.html && ok "hook output regenerated" || bad "hook output stale"
cd /; rm -rf "$T"

# #6 an output directory outside the project does not rebuild forever.
mode=output
T="$(mktemp -d)"; mkdir -p "$T/project" "$T/site"; cd "$T/project" && "$NIFT" init >/dev/null 2>&1 || exit 2
sed -i 's#"output-dir": "public/"#"output-dir": "../site/"#' .nift/config.json
echo '<a href="@pathto("/")">home</a>' > content/index.html
"$NIFT" build --all >/dev/null 2>&1
fresh && ok "external output directory stays up to date" || bad "external output dir rebuilds: $("$NIFT" status | sed -n 2,4p | tr '\n' ' ')"
cd /; rm -rf "$T"
exit $fail

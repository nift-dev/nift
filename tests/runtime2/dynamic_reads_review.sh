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
# D-091 #3 (the C24 treatment, aa812279): an input written right before a build could share the
# filesystem's timestamp tick with the page-info that build writes (the v4.12 modified rule then
# rebuilds once more), so the "up to date" checks failed at random. Every input written before a
# build that a freshness check follows is sequenced (tests/runtime2/cp01/ticks.py): the project is
# aged as a whole, keeping its order, and the written paths (with the directory that holds the
# clock probe) are stamped just before the filesystem clock: strictly newer than the last build and
# strictly older than the next. (A file the page's own pre-build hook writes with save() is not a
# dependency of the page, so the hook case needs only its inputs sequenced.) No rebuild is tolerated.
PY="$(command -v python3 || command -v python)"
TICKS="$(cd "$(dirname "$0")" && pwd)/cp01/ticks.py"
sequenced() { root=$1; shift; "$PY" "$TICKS" --age "$root" "$@" >/dev/null || bad "sequencing the edit of $*"; }
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
  sequenced "$T" content/index.html content data/x.txt data data/s.txt data/c.txt part.html . inc/wrap.html inc .nift/config.json
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
  echo SHADOWPART > inc/part.html; sequenced "$T" inc/part.html inc; stale && ok "@input shadowing file appears" || bad "@input shadowing missed"
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
sequenced "$T" content/index.html content content/index.pre-build.f posts/a.md posts gen
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
sequenced "$T/project" content/index.html content .nift/config.json
"$NIFT" build --all >/dev/null 2>&1
fresh && ok "external output directory stays up to date" || bad "external output dir rebuilds: $("$NIFT" status | sed -n 2,4p | tr '\n' ' ')"
cd /; rm -rf "$T"
# platform() output depends on --platform (v45_target_smoke review: v4.12.0
# served the previous target's output when nothing else changed).
mode=platform
T="$(mktemp -d)"; cd "$T" && "$NIFT" init >/dev/null 2>&1 || exit 2
printf 'P:@script { return platform() }\n' > content/index.html
sequenced "$T" content/index.html content
"$NIFT" build --platform=android >/dev/null 2>&1
"$NIFT" build --platform=ios >/dev/null 2>&1
grep "P:ios" public/index.html >/dev/null && ok "switching --platform rebuilds platform() pages" || bad "stale platform output"
"$NIFT" build --platform=ios 2>&1 | grep "up to date" >/dev/null && ok "same platform stays up to date" || bad "same platform rebuilds"
printf 'NOPLATFORM\n' > content/index.html; sequenced "$T" content/index.html content; "$NIFT" build >/dev/null 2>&1
"$NIFT" build --platform=android 2>&1 | grep "up to date" >/dev/null && ok "pages not calling platform() ignore --platform" || bad "platform-independent page rebuilt"
cd /; rm -rf "$T"
exit $fail

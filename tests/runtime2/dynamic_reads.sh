#!/usr/bin/env bash
# CP02: reads that can change page output without touching a declared
# dependency must invalidate the page, in every incremental mode:
#   exists()/is_file()/is_dir() presence and absence, directory listings and
#   globs (ls), and environment variables (getenv, @getenv, env()).
set -u -o pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
fail=0
# D-091 #3 (the C24 treatment, aa812279): an input written right before a build could share the
# filesystem's timestamp tick with the page-info that build writes; the v4.12 modified rule then
# rebuilds the page once more, and "stays up to date" failed at random. Every input written before
# a build is now sequenced (tests/runtime2/cp01/ticks.py): the project is aged as a whole, keeping
# its order, and the written paths are stamped just before the filesystem clock, so they are
# strictly newer than the last build and strictly older than the next. No rebuild is tolerated.
# (The first path's directory holds ticks.py's clock probe, so it is listed too.)
PY="$(command -v python3 || command -v python)"
TICKS="$(cd "$(dirname "$0")" && pwd)/cp01/ticks.py"
sequenced() { "$PY" "$TICKS" --age "$T" "$@" >/dev/null || { echo "FAIL [$mode] sequencing the edit of $*"; fail=1; }; }
check() { # name expected-substring
  if grep -q -- "$2" public/index.html; then echo "PASS [$mode] $1"; else echo "FAIL [$mode] $1 (expected '$2')"; sed -n '/<body>/,/<\/body>/p' public/index.html | head -12; fail=1; fi
}
up_to_date() { # name
  if "$NIFT" status | grep "up to date" >/dev/null; then echo "PASS [$mode] $1 stays up to date"; else echo "FAIL [$mode] $1: spurious rebuild"; "$NIFT" status | head -6; fail=1; fi
}
for mode in modified hash hybrid; do
  T="$(mktemp -d)"
  ( cd "$T" && "$NIFT" init >/dev/null 2>&1 ) || { echo "setup failed"; exit 2; }
  cd "$T"
  sed -i "s/\"incremental-mode\": \"modified\"/\"incremental-mode\": \"$mode\"/" .nift/config.json
  mkdir -p data
  echo '{}' > data/a.json
  cat > content/index.html <<'EOF'
@if(exists("templates/banner.html")) { @input("templates/banner.html") }
GLOB:@script { return ls("data/*.json").length() }
DIR:@script { return ls("data").length() }
FILE:@script { return is_file("data/a.json") }
ENV:@getenv("NIFT_RT2_PROBE")
SENV:@script { return getenv("NIFT_RT2_OTHER") }
EOF
  export NIFT_RT2_PROBE=one; unset NIFT_RT2_OTHER
  sequenced content/index.html content data/a.json data .nift/config.json
  "$NIFT" build --all >/dev/null 2>&1
  check "initial glob" "GLOB:1"
  up_to_date "after initial build"

  echo '<b>BANNER</b>' > templates/banner.html; sequenced templates/banner.html templates; "$NIFT" build >/dev/null 2>&1
  check "absent path appears" "BANNER"
  echo '{}' > data/b.json; sequenced data/b.json data; "$NIFT" build >/dev/null 2>&1
  check "glob gains a match" "GLOB:2"
  check "directory gains an entry" "DIR:2"
  touch data/notes.txt; sequenced data/notes.txt data; "$NIFT" build >/dev/null 2>&1
  check "directory listing change outside glob" "DIR:3"
  check "glob unaffected by non-matching file" "GLOB:2"
  rm data/a.json; sequenced data .; "$NIFT" build >/dev/null 2>&1
  check "present path disappears" "FILE:false"
  export NIFT_RT2_PROBE=two; "$NIFT" build >/dev/null 2>&1
  check "@getenv value change" "ENV:two"
  export NIFT_RT2_OTHER=set; "$NIFT" build >/dev/null 2>&1
  check "getenv unset -> set" "SENV:set"
  up_to_date "after the last change"
  cd - >/dev/null; rm -rf "$T"
done
exit $fail

#!/usr/bin/env bash
set -euo pipefail
trap 'echo "minifier CLI smoke failed at line $LINENO" >&2' ERR
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN="${MINIFY_BIN:-$ROOT/minify}"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/nift-minifier-cli.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

printf 'const  x =  1 ;\n' >"$TMP/app.js"
"$BIN" "$TMP/app.js" >/dev/null
grep -Fxq 'const x=1' "$TMP/app.min.js"
grep -Fxq 'const  x =  1 ;' "$TMP/app.js"

printf 'function total(longLeft,longRight){ return longLeft + longRight; }\n' >"$TMP/structured.js"
"$BIN" --structured "$TMP/structured.js" >/dev/null
grep -Fxq 'function total($,_){return $+_}' "$TMP/structured.min.js"

cp "$TMP/structured.js" "$TMP/aggressive.js"
"$BIN" --aggressive "$TMP/aggressive.js" >/dev/null
grep -Fxq 'function total($,_){return $+_}' "$TMP/aggressive.min.js"

printf 'const view = <Card value={(function total(longName){return longName})(3)} />;\n' >"$TMP/structured.jsx"
"$BIN" --structured-jsx-expressions "$TMP/structured.jsx" >/dev/null
grep -Fxq 'const view=<Card value={(function _($){return $})(3)} />;' "$TMP/structured.min.jsx"

printf '.x { color : red ; }\n' >"$TMP/site.css"
"$BIN" --in-place "$TMP/site.css" >/dev/null
grep -Fxq '.x{color:red;}' "$TMP/site.css"
test ! -e "$TMP/site.min.css"

printf 'const  executable =  true ;\n' >"$TMP/tool.js"
chmod 755 "$TMP/tool.js"
"$BIN" --in-place "$TMP/tool.js" >/dev/null
grep -Fxq 'const executable=!0' "$TMP/tool.js"
# Executable-bit preservation is a POSIX permission contract; Windows has no
# exec bit visible to std::filesystem, so the -x assertion is POSIX-only.
case "$(uname -s)" in
    MINGW*|MSYS*) ;;
    *) test -x "$TMP/tool.js" ;;
esac

: >"$TMP/empty.js"
"$BIN" "$TMP/empty.js" >/dev/null
test -f "$TMP/empty.min.js"
test ! -s "$TMP/empty.min.js"

if [[ -e /proc/kcore ]]; then
  ln -s /proc/kcore "$TMP/unreadable.js"
  if "$BIN" "$TMP/unreadable.js" >"$TMP/unreadable.log" 2>&1; then
    echo 'unreadable regular file unexpectedly treated as empty input' >&2; exit 1
  fi
  grep -Fq "cannot read '$TMP/unreadable.js'" "$TMP/unreadable.log"
  test ! -e "$TMP/unreadable.min.js"
fi

printf '{ "broken": }\n' >"$TMP/bad.json"
cp "$TMP/bad.json" "$TMP/original"
if "$BIN" "$TMP/bad.json" >"$TMP/bad.log" 2>&1; then
  echo 'malformed JSON unexpectedly succeeded' >&2; exit 1
fi
cmp "$TMP/bad.json" "$TMP/original"
test ! -e "$TMP/bad.min.json"

# Only run the symlink-refusal check when a real symlink was actually created:
# some environments (e.g. msys2 without symlink privileges) silently fall back
# to a copy, in which case the destination is a regular file and replacement is
# the correct behavior.
if ln -s "$TMP/app.js" "$TMP/link.js" 2>/dev/null && test -L "$TMP/link.js"; then
  cp "$TMP/app.js" "$TMP/link-target.before"
  if "$BIN" --in-place "$TMP/link.js" >"$TMP/link.log" 2>&1; then
    echo 'in-place symbolic-link destination unexpectedly replaced' >&2; exit 1
  fi
  grep -Fq 'refusing to replace a symbolic link' "$TMP/link.log"
  test -L "$TMP/link.js"
  cmp "$TMP/app.js" "$TMP/link-target.before"
fi

printf 'const  directory =  true ;\n' >"$TMP/existing.js"
mkdir "$TMP/existing.min.js"
if "$BIN" "$TMP/existing.js" >"$TMP/directory.log" 2>&1; then
  echo 'directory destination unexpectedly replaced' >&2; exit 1
fi
grep -Fq 'destination is not a regular file' "$TMP/directory.log"
test -d "$TMP/existing.min.js"

if "$BIN" --wat "$TMP/app.js" >"$TMP/option.log" 2>&1; then
  echo 'unknown option unexpectedly succeeded' >&2; exit 1
fi
grep -Fq "unknown option '--wat'" "$TMP/option.log"

# The version lives in a single authoritative place (cli/main.cpp); the smoke
# derives the expectation from it so a version bump can never leave the
# assertion stale.
expected="$(grep -oE 'Minify\+\+ [0-9]+\.[0-9]+\.[0-9]+' "$ROOT/cli/main.cpp" | head -1)"
test "$($BIN --version)" = "$expected"
echo 'Standalone minifier CLI smoke test passed'

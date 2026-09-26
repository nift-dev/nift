#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-./nift}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/t.f" <<'SRC'
print(platform());
print(os());
SRC
[[ "$($NIFT "$tmp/t.f" | head -1)" == native ]]
[[ "$($NIFT --platform=android "$tmp/t.f" | head -1)" == android ]]
[[ "$($NIFT "$tmp/t.f" --platform=android | head -1)" == android ]]
[[ "$($NIFT --platform=my.custom "$tmp/t.f" | head -1)" == my.custom ]]
# `--` is the hard option boundary: a later platform-looking argument belongs
# to the script and must not change the selected runtime platform.
out=$($NIFT "$tmp/t.f" -- --platform=android)
[[ "$(head -1 <<<"$out")" == native ]]
# Build accepts the global selector on either side of the command.
site="$tmp/site"; mkdir -p "$site"
(cd "$site" && "$NIFT" init >/dev/null 2>&1)
printf '$[platform()]\n' > "$site/content/index.html"
(cd "$site" && "$NIFT" --platform=android build >/dev/null)
grep -q 'android' "$site/public/index.html"
printf '$[platform()]\n' > "$site/content/index.html"
(cd "$site" && "$NIFT" build --platform=ios >/dev/null)
grep -q 'ios' "$site/public/index.html"
if $NIFT --android "$tmp/t.f" >/dev/null 2>&1; then echo '--android unexpectedly accepted' >&2; exit 1; fi
if $NIFT --target=android "$tmp/t.f" >/dev/null 2>&1; then echo 'runtime --target unexpectedly accepted' >&2; exit 1; fi
if $NIFT --platform=android --platform=ios "$tmp/t.f" >/dev/null 2>&1; then exit 1; fi

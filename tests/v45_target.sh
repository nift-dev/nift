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
[[ "$($NIFT --platform=my.custom "$tmp/t.f" | head -1)" == my.custom ]]
if $NIFT --android "$tmp/t.f" >/dev/null 2>&1; then echo '--android unexpectedly accepted' >&2; exit 1; fi
if $NIFT --target=android "$tmp/t.f" >/dev/null 2>&1; then echo 'runtime --target unexpectedly accepted' >&2; exit 1; fi
if $NIFT --platform=android --platform=ios "$tmp/t.f" >/dev/null 2>&1; then exit 1; fi

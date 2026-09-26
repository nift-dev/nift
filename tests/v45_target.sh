#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-./nift}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/t.f" <<'SRC'
print(target());
print(os());
SRC
[[ "$($NIFT "$tmp/t.f" | head -1)" == native ]]
[[ "$($NIFT --android "$tmp/t.f" | head -1)" == android ]]
[[ "$($NIFT --target=my.custom "$tmp/t.f" | head -1)" == my.custom ]]
if $NIFT --ANDROID "$tmp/t.f" >/dev/null 2>&1; then exit 1; fi
if $NIFT --android --ios "$tmp/t.f" >/dev/null 2>&1; then exit 1; fi

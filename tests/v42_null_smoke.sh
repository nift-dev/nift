#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cd "$T"; "$NIFT_BIN" init >/dev/null
cat > content/index.html <<'EOT'
$[x := null]eq=$[x == null] ne=$[x != null] other=$[1 != null]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q 'eq=true ne=false other=true' public/index.html
cat > content/index.html <<'EOT'
$[null < 5]
EOT
if "$NIFT_BIN" build --all >/dev/null 2>err; then echo 'expected null ordering failure' >&2; exit 1; fi
grep -q 'ordering comparisons require' err

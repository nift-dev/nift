#!/usr/bin/env bash
set -euo pipefail

NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
P="$(mktemp -d)"
trap 'rm -rf "$P"' EXIT

cd "$P"
"$NIFT_BIN" init >/dev/null
cat > templates/template.html <<'TEMPLATE'
<!doctype html>
<html>
<head>
		<link rel="stylesheet" href="@pathto('/assets/css/style.css')">
</head>
<body>@content</body>
</html>
TEMPLATE

if "$NIFT_BIN" build >out.log 2>err.log; then
    echo "expected invalid absolute @pathto path to fail" >&2
    exit 1
fi

grep -Fq 'templates/template.html:4:32' err.log
grep -Fq '@pathto path must stay inside the Nift project: /assets/css/style.css' err.log

python3 - <<'PY'
from pathlib import Path
lines = Path('err.log').read_text().splitlines()
source = next(line for line in lines if '<link rel="stylesheet"' in line)
marker = lines[lines.index(source) + 1]
assert source.index('@pathto') == marker.index('^'), (source, marker)
assert marker.count('~') >= len('@pathto') - 1, marker
assert '\x1b[' not in '\n'.join(lines), 'redirected diagnostics must remain ANSI-free'
# The source excerpt is tab-expanded: two source tabs become sixteen spaces,
# after the four-space diagnostic margin.
assert source.startswith(' ' * 20 + '<link'), repr(source)
PY

echo "diagnostics smoke passed"

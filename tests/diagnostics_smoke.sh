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
		<link rel="stylesheet" href="@path('/assets/css/style.css')">
</head>
<body>@content</body>
</html>
TEMPLATE
if "$NIFT_BIN" build >out.log 2>err.log; then
    echo "expected invalid absolute @path path to fail" >&2
    exit 1
fi
grep -Fq 'templates/template.html:4:32' err.log
grep -Fq '@path path must stay inside the Nift project: /assets/css/style.css' err.log
python3 - <<'PY'
from pathlib import Path
lines = Path('err.log').read_text().splitlines()
source = next(line for line in lines if '<link rel="stylesheet"' in line)
marker = lines[lines.index(source) + 1]
assert source.index('@path') == marker.index('^'), (source, marker)
assert marker.count('~') >= len('@path') - 1, marker
assert '\x1b[' not in '\n'.join(lines), 'redirected diagnostics must remain ANSI-free'
# The source excerpt is tab-expanded: two source tabs become sixteen spaces,
# after the four-space diagnostic margin.
assert source.startswith(' ' * 20 + '<link'), repr(source)
PY
echo "diagnostics smoke passed"
# --- Long generated/minified single-line sources stay bounded ---
cat > templates/template.html <<'TEMPLATE'
<!doctype html>
<html>
<body>@content</body>
</html>
TEMPLATE

# A > 16 KiB one-line page with an error near column ~32000 must render a
# bounded excerpt, keep the true file:line:column, and never emit the whole
# line or a caret padded to the original column.
python3 - <<'PY'
from pathlib import Path
line = ("Lorem ipsum dolor sit amet " * 1200) + '@substr("abc", 100)'
Path('content/index.html').write_text(line + '\n')
PY
if "$NIFT_BIN" build >long.out 2>long.err; then
    echo "expected long-line substr failure" >&2
    exit 1
fi
grep -q ':1:32[0-9][0-9][0-9]' long.err
grep -q '@substr("abc", 100)' long.err
longest="$(awk '{ if (length($0) > max) max = length($0) } END { print max }' long.err)"
if [ "$longest" -gt 500 ]; then
    echo "FAIL: long-line diagnostic still emits an unbounded source/caret line" >&2
    exit 1
fi
if [ "$(wc -c < long.err)" -gt 2048 ]; then
    echo "FAIL: long-line diagnostic output is unbounded" >&2
    exit 1
fi
# Error near the very start of a long line: no leading ellipsis, trailing crop.
python3 - <<'PY'
from pathlib import Path
line = '@substr("abc", 100)' + ('z' * 20000)
Path('content/index.html').write_text(line + '\n')
PY
"$NIFT_BIN" build >start.out 2>start.err || true
grep -q ':1:1' start.err
grep -q '...' start.err
# Error at the very end of a long line: leading crop, no trailing content.
python3 - <<'PY'
from pathlib import Path
line = ('y' * 20000) + '@substr("abc", 100)'
Path('content/index.html').write_text(line + '\n')
PY
"$NIFT_BIN" build >end.out 2>end.err || true
grep -q ':1:20001' end.err
grep -qE '^ +\.\.\.' end.err
echo "diagnostics smoke passed"

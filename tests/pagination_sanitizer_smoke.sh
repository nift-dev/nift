#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/.build/nift-sanitize}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-pagination-san.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"
mkdir -p .nift content templates public
cat > .nift/config.json <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":8,"incremental-mode":"modified"}}
JSON
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"archive","title":"Archive","template":"templates/template.html","paginate":{"items-per-page":2}}]}
JSON
printf '@content\n' > templates/template.html
cat > content/archive.paginate.html <<'EOF2'
<div>$[paginate.items]</div><span>$[paginate.current]/$[paginate.total]</span>
EOF2
: > content/archive.html
for i in $(seq 1 24); do printf '@item{item-%s}\n' "$i" >> content/archive.html; done
printf '@paginate\n' >> content/archive.html
"$NIFT_BIN" build --all >/dev/null
test -f public/archive.html && test -f public/archive-12.html
# Exercise lifecycle shrink and complete regeneration under sanitizer.
cat > content/archive.html <<'EOF2'
@item{a}
@item{b}
@item{c}
@paginate
EOF2
"$NIFT_BIN" build >/dev/null
test -f public/archive-2.html && test ! -e public/archive-3.html
# Broken pagination rendering must fail without destroying the last-good set.
cp public/archive.html old-main
cp public/archive-2.html old-second
printf "@input('missing.html')\n\$[paginate.items]\n" > content/archive.paginate.html
if "$NIFT_BIN" build >/dev/null 2>&1; then
  echo 'broken pagination template unexpectedly succeeded under sanitizer' >&2
  exit 1
fi
cmp old-main public/archive.html
cmp old-second public/archive-2.html
echo 'Pagination sanitizer smoke test passed'

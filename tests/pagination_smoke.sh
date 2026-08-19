#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-pagination-test.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"
mkdir -p .nift content templates public data
cat > .nift/config.json <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}
JSON
cat > .nift/tracked.json <<'JSON'
{"tracked":[
 {"name":"/","title":"Paged Home","template":"templates/template.html","paginate":{"items-per-page":2}},
 {"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":2,"template":"templates/shared-paginate.html","separator":"templates/shared-separator.html"}}
]}
JSON
cat > templates/template.html <<'EOF2'
<!doctype html><title>$[title]</title><main>@content</main>
EOF2
cat > data/items.json <<'JSON'
{"items":[{"name":"one"},{"name":"two"},{"name":"three"},{"name":"four"},{"name":"five"}]}
JSON
cat > content/index.html <<'EOF2'
@json('data/items.json', d)
@item{before}
@paginate
@for(x : d.items){@item{<b>$[x.name]</b>}}
EOF2
cat > content/index.paginate.html <<'EOF2'
<section>$[paginate.items]</section><nav>$[paginate.current]/$[paginate.total] @if(!paginate.first){<a href="@pathtopage($[paginate.previous])">prev</a>} @if(!paginate.last){<a href="@pathtopage($[paginate.next])">next</a>}</nav>
EOF2
cat > content/index.separator.html <<'EOF2'
<span>|$[paginate.current]|</span>
EOF2
cat > content/blog.html <<'EOF2'
@json('data/items.json', d)
@for(x : d.items){@item{$[x.name]}}
@paginate
EOF2
cat > templates/shared-paginate.html <<'EOF2'
<div class="page-$[paginate.current]">$[paginate.items]</div>
EOF2
cat > templates/shared-separator.html <<'EOF2'
/
EOF2
"$NIFT_BIN" build-all >/dev/null
# Root/index naming: index.html, 2.html, 3.html; six items (one before + five loop) => 3 pages.
test -f public/index.html && test -f public/2.html && test -f public/3.html
grep -F '<section>before' public/index.html >/dev/null
grep -F '|1|' public/index.html >/dev/null
grep -F '1/3' public/index.html >/dev/null
grep -F 'href="./2.html"' public/index.html >/dev/null
grep -F '2/3' public/2.html >/dev/null
grep -F 'href="./"' public/2.html >/dev/null
grep -F '3/3' public/3.html >/dev/null
# Non-index naming and explicit reusable pagination files.
test -f public/blog.html && test -f public/blog-2.html && test -f public/blog-3.html
grep -F 'class="page-1"' public/blog.html >/dev/null
grep -F 'one' public/blog.html >/dev/null
grep -F '/' public/blog.html >/dev/null
# Exactly one @paginate is required.
cat > content/blog.html <<'EOF2'
@item{x}
EOF2
if "$NIFT_BIN" build-all >/dev/null 2>&1; then echo 'pagination without @paginate unexpectedly succeeded' >&2; exit 1; fi
cat > content/blog.html <<'EOF2'
@item{x}@paginate@paginate
EOF2
if "$NIFT_BIN" build-all >/dev/null 2>&1; then echo 'multiple @paginate unexpectedly succeeded' >&2; exit 1; fi
# Zero items is valid and emits the primary page with an empty paginate.items.
cat > content/blog.html <<'EOF2'
@paginate
EOF2
"$NIFT_BIN" build-all >/dev/null
grep -F 'class="page-1"></div>' public/blog.html >/dev/null
# Pagination directives without tracked pagination are rejected.
python3 - <<'PY'
import json
p='.nift/tracked.json'; d=json.load(open(p)); d['tracked'][1].pop('paginate'); json.dump(d,open(p,'w'))
PY
cat > content/blog.html <<'EOF2'
@paginate
EOF2
if "$NIFT_BIN" build-all >/dev/null 2>&1; then echo '@paginate without config unexpectedly succeeded' >&2; exit 1; fi

echo 'Pagination smoke test passed'

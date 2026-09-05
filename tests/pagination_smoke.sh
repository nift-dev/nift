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
@json(d, 'data/items.json')
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
@json(d, 'data/items.json')
@for(x : d.items){@item{$[x.name]}}
@paginate
EOF2
cat > templates/shared-paginate.html <<'EOF2'
<div class="page-$[paginate.current]">$[paginate.items]</div>
EOF2
cat > templates/shared-separator.html <<'EOF2'
/
EOF2
"$NIFT_BIN" build --all >/dev/null
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
if "$NIFT_BIN" build --all >/dev/null 2>&1; then echo 'pagination without @paginate unexpectedly succeeded' >&2; exit 1; fi
rm -f .nift/.unfinished
cat > content/blog.html <<'EOF2'
@item{x}@paginate@paginate
EOF2
if "$NIFT_BIN" build --all >/dev/null 2>&1; then echo 'multiple @paginate unexpectedly succeeded' >&2; exit 1; fi
rm -f .nift/.unfinished
# Zero items is valid and emits the primary page with an empty paginate.items.
cat > content/blog.html <<'EOF2'
@paginate
EOF2
"$NIFT_BIN" build --all >/dev/null
grep -F 'class="page-1"></div>' public/blog.html >/dev/null
# Pagination directives without tracked pagination are rejected.
python3 - <<'PY'
import json
p='.nift/tracked.json'; d=json.load(open(p)); d['tracked'][1].pop('paginate'); json.dump(d,open(p,'w'))
PY
cat > content/blog.html <<'EOF2'
@paginate
EOF2
if "$NIFT_BIN" build --all >/dev/null 2>&1; then echo '@paginate without config unexpectedly succeeded' >&2; exit 1; fi
rm -f .nift/.unfinished

echo 'Pagination smoke test passed'

# Lifecycle: page count changes remove stale owned outputs, missing secondary
# pages invalidate the whole tracked item, separator appearance/disappearance is
# part of effective pagination state, and disabling pagination removes old pages.
cd "$TMP"
rm -rf .nift content templates public data
mkdir -p .nift content templates public data
cat > .nift/config.json <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}
JSON
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}}]}
JSON
echo '@content' > templates/template.html
cat > content/blog.paginate.html <<'EOF2'
$[paginate.items]-$[paginate.current]/$[paginate.total]
EOF2
cat > content/blog.html <<'EOF2'
@item{a}@item{b}@item{c}@paginate
EOF2
"$NIFT_BIN" build --all >/dev/null
test -f public/blog.html && test -f public/blog-2.html && test -f public/blog-3.html
grep -F '"pagination-pages": 3' .nift/public/blog.info.json >/dev/null
rm public/blog-2.html
"$NIFT_BIN" status >status.log
grep -F 'generated pagination output is missing: public/blog-2.html' status.log >/dev/null
"$NIFT_BIN" build >/dev/null
cat > content/blog.html <<'EOF2'
@item{a}@paginate
EOF2
"$NIFT_BIN" build >/dev/null
test ! -e public/blog-2.html && test ! -e public/blog-3.html
grep -F '"pagination-pages": 1' .nift/public/blog.info.json >/dev/null
cat > content/blog.separator.html <<'EOF2'
--sep--
EOF2
"$NIFT_BIN" status >status.log
grep -F 'pagination separator changed' status.log >/dev/null
"$NIFT_BIN" build >/dev/null
rm content/blog.separator.html
"$NIFT_BIN" status >status.log
grep -F 'pagination separator changed' status.log >/dev/null
"$NIFT_BIN" build >/dev/null
python3 - <<'PY'
import json
p='.nift/tracked.json'; d=json.load(open(p)); d['tracked'][0].pop('paginate'); json.dump(d,open(p,'w'))
PY
cat > content/blog.html <<'EOF2'
plain
EOF2
"$NIFT_BIN" build >/dev/null
test ! -e public/blog-2.html && test ! -e public/blog-3.html
grep -F '"pagination": false' .nift/public/blog.info.json >/dev/null

# Failed pagination rendering must preserve the entire previous page set.
cd "$TMP"
rm -rf .nift content templates public data
mkdir -p .nift content templates public
cat > .nift/config.json <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}
JSON
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}}]}
JSON
echo '@content' > templates/template.html
cat > content/blog.html <<'EOF2'
@item{old-a}@item{old-b}@item{old-c}@paginate
EOF2
cat > content/blog.paginate.html <<'EOF2'
$[paginate.items]-$[paginate.current]
EOF2
"$NIFT_BIN" build --all >/dev/null
cp public/blog.html old1
cp public/blog-2.html old2
cp public/blog-3.html old3
cat > content/blog.paginate.html <<'EOF2'
@input('missing-pagination-fragment.html')
$[paginate.items]
EOF2
if "$NIFT_BIN" build >/dev/null 2>&1; then echo 'broken pagination template unexpectedly succeeded' >&2; exit 1; fi
rm -f .nift/.unfinished
cmp old1 public/blog.html
cmp old2 public/blog-2.html
cmp old3 public/blog-3.html

# Multi-threaded page rendering is deterministic for a large single tracked item.
cd "$TMP"
rm -rf .nift content templates public
mkdir -p .nift content templates public
cat > .nift/config.json <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":8,"incremental-mode":"modified"}}
JSON
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"archive","title":"Archive","template":"templates/template.html","paginate":{"items-per-page":2}}]}
JSON
echo '@content' > templates/template.html
cat > content/archive.paginate.html <<'EOF2'
[$[paginate.current]/$[paginate.total]]$[paginate.items]
EOF2
: > content/archive.html
for i in $(seq 1 200); do printf '@item{item-%03d}\n' "$i" >> content/archive.html; done
echo '@paginate' >> content/archive.html
"$NIFT_BIN" build --all >/dev/null
count=$(find public -maxdepth 1 -type f -name 'archive*.html' | wc -l)
test "$count" -eq 100
find public -maxdepth 1 -type f -name 'archive*.html' -print0 | sort -z | xargs -0 sha256sum > before.sha
"$NIFT_BIN" build --all >/dev/null
find public -maxdepth 1 -type f -name 'archive*.html' -print0 | sort -z | xargs -0 sha256sum > after.sha
cmp before.sha after.sha

# pathtopage accepts signed relative offsets, including interpolated offsets.
cd "$TMP"
rm -rf .nift content templates public data
mkdir -p .nift content templates public data
cat > .nift/config.json <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}
JSON
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}}]}
JSON
echo '@content' > templates/template.html
cat > data/nav.json <<'JSON'
{"offset":1}
JSON
cat > content/blog.html <<'EOF2'
@json(nav, 'data/nav.json')
@item{one}@item{two}@item{three}@paginate
EOF2
cat > content/blog.paginate.html <<'EOF2'
<section>$[paginate.items]</section><nav>@if(!paginate.first){<a class="prev" href="@pathtopage(-1)">prev</a>} @if(!paginate.last){<a class="next" href="@pathtopage(+1)">next</a>} @if(!paginate.last){<a class="skip" href="@pathtopage(+$[nav.offset])">skip</a>}</nav>
EOF2
"$NIFT_BIN" build --all >/dev/null
grep -F 'class="next" href="./blog-2.html"' public/blog.html >/dev/null
grep -F 'class="skip" href="./blog-2.html"' public/blog.html >/dev/null
grep -F 'class="prev" href="./blog.html"' public/blog-2.html >/dev/null

# --- Pagination lifecycle: stale-output cleanup ----------------------------
# When pagination is removed or its items-per-page changes so the page count
# decreases, the previous build's stale page-N outputs must be removed. The
# previous pagination state (historical .info.json) must be consulted even
# when the page is no longer paginated.
cd "$TMP"
rm -rf .nift content templates public data
mkdir -p .nift content templates public data
cat > .nift/config.json <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}
JSON
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}}]}
JSON
echo '@content' > templates/template.html
cat > content/blog.html <<'EOF2'
@item{one}@item{two}@item{three}@paginate
EOF2
cat > content/blog.paginate.html <<'EOF2'
<section>$[paginate.items]</section>
EOF2

# Paginated build produces page-2 and page-3.
"$NIFT_BIN" build --all >/dev/null
test -f public/blog.html && test -f public/blog-2.html && test -f public/blog-3.html

# Page count decreases: items-per-page 1 -> 3 means only one page, so the
# stale page-2/page-3 outputs must be removed.
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":3}}]}
JSON
"$NIFT_BIN" build --all >/dev/null
test -f public/blog.html && test ! -f public/blog-2.html && test ! -f public/blog-3.html

# Pagination removed entirely: the page is no longer paginated, but the
# previous pagination state must still be consulted so stale page-N outputs
# are cleaned up (regression: gating the historical read on the current
# paginate flag left stale pagination outputs behind).
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"blog","title":"Blog","template":"templates/template.html"}]}
JSON
cat > content/blog.html <<'EOF2'
<p>ordinary</p>
EOF2
"$NIFT_BIN" build --all >/dev/null
test -f public/blog.html && test ! -f public/blog-2.html && test ! -f public/blog-3.html

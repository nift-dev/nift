#!/usr/bin/env bash
# Frontend-surface dogfood (surface audit): two realistic Nift builds that
# exercise the audited mundane surface through real template rendering.
#
# Scenario A — documentation site with page hierarchy: breadcrumbs via
#   page.ancestors, title escaping via html_escape, title length/empty,
#   dynamic object lookup, and tag links built from a JSON data file
#   (ifstream/read_val) with url_encode + attr_escape per context.
# Scenario B — data-driven product catalog from a JSON data file loaded with
#   ifstream/read_val: object access, array contains, filter/map/join, numeric
#   round/to_string, generated URLs and attributes with context-appropriate
#   escaping.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="${NIFT_BIN:-$ROOT/nift}"
TMP=$(mktemp -d); trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP"

mkdir -p .nift content/docs/guides templates public content/data
printf '{"config":{"content-dir":"content/","content-ext":".md","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}' > .nift/config.json

# ---- Scenario A: docs site ----


mkpage(){ # name title body
  mkdir -p "content/$(dirname "$1")"
  printf -- '---\n{"title":"%s"}\n---\n%s\n' "$2" "$3" > "content/$1.md"
}
mkpage index 'Home' 'Welcome.'
mkpage docs 'Documentation' 'All guides.'
mkpage docs/guides 'Guides' 'Index.'
mkpage 'docs/guides/quoting' 'Quoting & Escaping' 'Quotes: "double" and '\''single'\''. <b>bold</b> & ampersand.'
mkpage 'docs/guides/urls' 'URLs & Paths' 'Search the docs.'
cat > .nift/tracked.json <<'TRK'
{"tracked":[
 {"name":"/","title":"Home","template":"templates/template.html"},
 {"name":"docs","title":"Documentation","template":"templates/template.html"},
 {"name":"docs/guides","title":"Guides","template":"templates/template.html"},
 {"name":"docs/guides/quoting","title":"Quoting & Escaping","template":"templates/template.html"},
 {"name":"docs/guides/urls","title":"URLs & Paths","template":"templates/template.html"}
]}
TRK
cat > content/data/tags.json <<'JSON'
{"/":["home","Nift"],
 "docs":["docs"],
 "docs/guides":["guides"],
 "docs/guides/quoting":["quoting","HTML & entities"],
 "docs/guides/urls":["urls","paths"]}
JSON
cat > content/data/info.json <<'JSON'
{"/":{"title":"Home"},
 "docs":{"title":"Documentation"},
 "docs/guides":{"title":"Guides"},
 "docs/guides/quoting":{"title":"Quoting & Escaping"},
 "docs/guides/urls":{"title":"URLs & Paths"}}
JSON

# ---- Scenario B: data-driven product catalog ----
cat > content/data/products.json <<'JSON'
[{"name":"Nift Mug","price":12.5,"tags":["drink","gift"]},
 {"name":"Nift Tee (L)","price":24.0,"tags":["clothing"]},
 {"name":"Nift Sticker Pack","price":4.5,"tags":["stickers","gift"]}]
JSON
cat > templates/products.html <<'TPL'
<ul>
$[stream := ifstream("content/data/products.json")]
$[products := stream.read_val()]
$[close(stream)]
@for(p : products){
<li><a href="/products/$[url_encode(p.name.to_lower().replace(" ","-"))]">$[html_escape(p.name)]</a>: $[p.price.to_string()] ($[p.price.round().to_string()] rounded, $[p.tags.contains("gift") ? "gift" : "plain"])</li>
}
</ul>@content
TPL
printf -- '---\n{"title":"Products"}\n---\n# Products\n' > content/products.md
python3 - <<'PY'
import json
tr=json.load(open('.nift/tracked.json'))
tr['tracked'].append({'name':'products','title':'Products','template':'templates/products.html'})
json.dump(tr,open('.nift/tracked.json','w'))
PY

# add page_info binding via a per-page JSON read in template (info.json read above)
cat > templates/template.html <<'TPL'
<!doctype html><html><head><title>$[html_escape(page.title)] | Nift Docs</title></head>
<body>
<nav>$[page.ancestors.map(a => a.title).join(" &gt; ")] &gt; $[html_escape(page.title)]</nav>
$[istream := ifstream("content/data/info.json")]
$[info := istream.read_val()]
$[close(istream)]
$[title_key := "title"]
<header>Dynamic object: $[html_escape(info[page.name][title_key])] ($[page.title.length()] chars, empty=$[page.title.empty()])</header>
$[stream := ifstream("content/data/tags.json")]
$[all_tags := stream.read_val()]
$[close(stream)]
$[page_tags := all_tags[page.name]]
<footer>Tags: $[page_tags.filter(t => !t.empty()).map(t => "<a href=\"/tags/" + url_encode(t.to_lower().trim()) + "\">" + attr_escape(t) + "</a>").join(" &middot; ")]</footer>
<main>@content</main>
</body></html>
TPL

"$NIFT" build --all >/dev/null 2>&1 || { echo "build failed" >&2; exit 1; }

# ---- Scenario A assertions ----
grep -q '<title>Quoting &amp; Escaping | Nift Docs</title>' public/docs/guides/quoting.html
grep -q 'Home &gt; Documentation &gt; Guides &gt; Quoting &amp; Escaping' public/docs/guides/quoting.html
grep -q 'Dynamic object: Quoting &amp; Escaping (18 chars, empty=false)' public/docs/guides/quoting.html
grep -q '<a href="/tags/quoting">quoting</a>' public/docs/guides/quoting.html
grep -q '<a href="/tags/html%20%26%20entities">HTML &amp; entities</a>' public/docs/guides/quoting.html
grep -q 'Home &gt; Documentation &gt; Guides' public/docs/guides/urls.html
grep -q 'Dynamic object: URLs &amp; Paths (12 chars, empty=false)' public/docs/guides/urls.html

# ---- Scenario B assertions ----
grep -q '<a href="/products/nift-mug">Nift Mug</a>: 12.5 (13 rounded, gift)' public/products.html
grep -q '<a href="/products/nift-tee-%28l%29">Nift Tee (L)</a>: 24 (24 rounded, plain)' public/products.html
grep -q '<a href="/products/nift-sticker-pack">Nift Sticker Pack</a>: 4.5 (5 rounded, gift)' public/products.html

echo "frontend surface dogfood passed"
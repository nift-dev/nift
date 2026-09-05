#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-cross-feature.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

mkproj() {
  local d="$1"
  mkdir -p "$d/.nift" "$d/content" "$d/templates" "$d/public" "$d/data" "$d/schemas"
  cat >"$d/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}}
JSON
  cat >"$d/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"},{"name":"about","title":"About","template":"templates/template.html"}]}
JSON
  printf '<p>about</p>\n' >"$d/content/about.html"
}

# Schema-driven conditional pathto: only the rendered branch becomes a req.
P="$TMP/branch-req"; mkproj "$P"
mkdir -p "$P/public/assets"
printf x >"$P/public/assets/a.txt"
printf y >"$P/public/assets/b.txt"
cat >"$P/data/site.json" <<'JSON'
{"choice":"a"}
JSON
cat >"$P/schemas/site.schema.json" <<'JSON'
{"type":"object","required":["choice"],"properties":{"choice":{"enum":["a","b"]}}}
JSON
cat >"$P/templates/template.html" <<'EOF'
@json(site, "schemas/site.schema.json", "data/site.json")
@if(site.choice == "a"){
<a href="@pathto('public/assets/a.txt')">A</a>
}else{
<a href="@pathto('public/assets/b.txt')">B</a>
}
@content
EOF
printf '\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
grep -Fq '"public/assets/a.txt"' "$P/.nift/public/index.info.json"
! grep -Fq '"public/assets/b.txt"' "$P/.nift/public/index.info.json"

# Changing data swaps req sets on rebuild.
printf '{"choice":"b"}\n' >"$P/data/site.json"
python3 - "$P/data/site.json" "$P/.nift/public/index.info.json" <<'PYMTIME'
import os,sys
data,info=sys.argv[1:3]
st=os.stat(info)
os.utime(data,ns=(st.st_atime_ns,st.st_mtime_ns+1000000))
PYMTIME
(cd "$P" && "$NIFT_BIN" build >/dev/null)
grep -Fq '"public/assets/b.txt"' "$P/.nift/public/index.info.json"
! grep -Fq '"public/assets/a.txt"' "$P/.nift/public/index.info.json"

# A missing req in a branch that is no longer selected must not poison the repair.
rm "$P/public/assets/b.txt"
printf '{"choice":"a"}\n' >"$P/data/site.json"
python3 - "$P/data/site.json" "$P/.nift/public/index.info.json" <<'PYMTIME'
import os,sys
data,info=sys.argv[1:3]
st=os.stat(info)
os.utime(data,ns=(st.st_atime_ns,st.st_mtime_ns+1000000))
PYMTIME
(cd "$P" && "$NIFT_BIN" build >/dev/null)
grep -Fq '"public/assets/a.txt"' "$P/.nift/public/index.info.json"
! grep -Fq '"public/assets/b.txt"' "$P/.nift/public/index.info.json"

# Sorting + schema + nested input + interpolated req collection.
P="$TMP/sorted-input"; mkproj "$P"
mkdir -p "$P/templates/parts" "$P/public/assets"
printf A >"$P/public/assets/a.txt"; printf B >"$P/public/assets/b.txt"
cat >"$P/data/items.json" <<'JSON'
{"items":[{"name":"b","rank":2},{"name":"a","rank":1}]}
JSON
cat >"$P/schemas/items.json" <<'JSON'
{"type":"object","properties":{"items":{"type":"array","items":{"type":"object","required":["name","rank"],"properties":{"name":{"type":"string"},"rank":{"type":"integer"}}}}}}
JSON
cat >"$P/templates/template.html" <<'EOF'
@json(data, "schemas/items.json", "data/items.json")
@for(item : data.items by item.rank asc){
@input("templates/parts/item.html")
}
@content
EOF
cat >"$P/templates/parts/item.html" <<'EOF'
<span>$[loop.index]:$[item.name]:@pathto('public/assets/$[item.name].txt')</span>
EOF
printf '\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
grep -Fq '<span>1:a:assets/a.txt</span>' "$P/public/index.html"
grep -Fq '<span>2:b:assets/b.txt</span>' "$P/public/index.html"
grep -Fq '"public/assets/a.txt"' "$P/.nift/public/index.info.json"
grep -Fq '"public/assets/b.txt"' "$P/.nift/public/index.info.json"

# Tracked output req disappears, both producer and consumer should be candidates;
# build must recreate producer and then allow consumer to succeed.
P="$TMP/tracked-repair"; mkproj "$P"
cat >"$P/templates/template.html" <<'EOF'
<a href="@pathto('about')">about</a>
@content
EOF
printf '\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
rm "$P/public/about.html"
(cd "$P" && "$NIFT_BIN" build >/dev/null)
test -f "$P/public/about.html"
grep -Fq '"public/about.html"' "$P/.nift/public/index.info.json"

# Corrupt page metadata reqs path should be data-only, not executable/path traversal.
P="$TMP/corrupt-meta"; mkproj "$P"
printf '@content\n' >"$P/templates/template.html"
printf '<p>x</p>\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
python3 -S - "$P/.nift/public/index.info.json" <<'PY'
import json,sys,os
p=sys.argv[1]; os.chmod(p,0o644)
d=json.load(open(p)); d["reqs"]=["../../definitely-outside"]; json.dump(d,open(p,"w"))
PY
(cd "$P" && "$NIFT_BIN" status >status.log)
grep -Eq 'needs rebuilding|required path missing|invalid requirement' "$P/status.log"
(cd "$P" && "$NIFT_BIN" build >/dev/null)

echo "Cross-feature smoke test passed"

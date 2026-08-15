#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/sift-integration.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

mkproj() {
  local d="$1"
  mkdir -p "$d/.nift" "$d/content/assets" "$d/templates" "$d/public"
  cat >"$d/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":2,"incremental-mode":"modified","minify-exts":[]}}
JSON
  cat >"$d/.nift/tracked.json" <<'JSON'
{"tracked":[
 {"name":"/","title":"Home","template":"templates/template.html"},
 {"name":"assets/style","title":"Style","template":"templates/style.css","content-ext":".css","output-ext":".css"}
]}
JSON
  printf '@content\n' >"$d/templates/template.html"
  printf '@content\n' >"$d/templates/style.css"
  printf '<div>   Hello   world </div>\n' >"$d/content/index.html"
  printf '.x { color : red ; margin : 0  1rem ; }\n' >"$d/content/assets/style.css"
}

# Global extension minification applies only to matching outputs.
P="$TMP/global"; mkproj "$P"
python3 -S - "$P/.nift/config.json" <<'PY'
import json,sys
p=sys.argv[1]; d=json.load(open(p)); d["config"]["minify-exts"]=[".html"]; json.dump(d,open(p,"w"))
PY
(cd "$P" && "$NIFT_BIN" build-all >/dev/null)
grep -Fq '<div> Hello world </div>' "$P/public/index.html"
grep -Fq '.x { color : red ; margin : 0  1rem ; }' "$P/public/assets/style.css"
grep -Fq '"minify": true' "$P/.nift/public/index.info.json"
grep -Fq '"minify": false' "$P/.nift/public/assets/style.info.json"

# Per-file true opts CSS in; false opts HTML out even with global HTML minification.
python3 -S - "$P/.nift/tracked.json" <<'PY'
import json,sys,os
p=sys.argv[1]; os.chmod(p,0o644); d=json.load(open(p))
for x in d["tracked"]:
    if x["name"]=="/": x["minify"]=False
    if x["name"]=="assets/style": x["minify"]=True
json.dump(d,open(p,"w"))
PY
(cd "$P" && "$NIFT_BIN" build-updated >/dev/null)
grep -Fq '<div>   Hello   world </div>' "$P/public/index.html"
grep -Fq '.x{color:red;margin:0 1rem;}' "$P/public/assets/style.css"
grep -Fq '"minify": false' "$P/.nift/public/index.info.json"
grep -Fq '"minify": true' "$P/.nift/public/assets/style.info.json"

# Changing only minification config must be a rebuild reason.
python3 -S - "$P/.nift/tracked.json" <<'PY'
import json,sys,os
p=sys.argv[1]; os.chmod(p,0o644); d=json.load(open(p))
for x in d["tracked"]: x.pop("minify",None)
json.dump(d,open(p,"w"))
PY
python3 -S - "$P/.nift/config.json" <<'PY'
import json,sys,os
p=sys.argv[1]; os.chmod(p,0o644); d=json.load(open(p)); d["config"]["minify-exts"]=[".html",".css"]; json.dump(d,open(p,"w"))
PY
(cd "$P" && "$NIFT_BIN" status >status.log)
grep -Fq 'minification setting changed' "$P/status.log"
(cd "$P" && "$NIFT_BIN" build-updated >/dev/null)
grep -Fq '.x{color:red;margin:0 1rem;}' "$P/public/assets/style.css"

# Minified page metadata records a minifier format version so future safer/
# stronger minifier revisions can force regeneration without changing templates.
grep -Fq '"minify-version": 1' "$P/.nift/public/index.info.json"
python3 -S - "$P/.nift/public/index.info.json" <<'PY'
import json,sys,os
p=sys.argv[1]; os.chmod(p,0o644); d=json.load(open(p)); d["minify-version"]=999; json.dump(d,open(p,"w"))
PY
(cd "$P" && "$NIFT_BIN" status >version-status.log)
grep -Fq 'minifier version changed' "$P/version-status.log"
(cd "$P" && "$NIFT_BIN" build-updated >/dev/null)

# Removing a global extension should rebuild back to unminified source output.
python3 -S - "$P/.nift/config.json" <<'PY'
import json,sys,os
p=sys.argv[1]; os.chmod(p,0o644); d=json.load(open(p)); d["config"]["minify-exts"]=[]; json.dump(d,open(p,"w"))
PY
(cd "$P" && "$NIFT_BIN" build-updated >/dev/null)
grep -Fq '<div>   Hello   world </div>' "$P/public/index.html"

# Invalid config and tracked overrides fail cleanly.
P="$TMP/badconfig"; mkproj "$P"
python3 -S - "$P/.nift/config.json" <<'PY'
import json,sys
p=sys.argv[1]; d=json.load(open(p)); d["config"]["minify-exts"]=".html"; json.dump(d,open(p,"w"))
PY
if (cd "$P" && "$NIFT_BIN" status >bad.log 2>&1); then echo "string minify-exts accepted" >&2; exit 1; fi
grep -Fq 'minify-exts must be an array' "$P/bad.log"

P="$TMP/badext"; mkproj "$P"
python3 -S - "$P/.nift/config.json" <<'PY'
import json,sys
p=sys.argv[1]; d=json.load(open(p)); d["config"]["minify-exts"]=[".wat"]; json.dump(d,open(p,"w"))
PY
if (cd "$P" && "$NIFT_BIN" status >bad.log 2>&1); then echo "unsupported minify extension accepted" >&2; exit 1; fi
grep -Fq 'unsupported minify-exts entry' "$P/bad.log"

P="$TMP/badtracked"; mkproj "$P"
python3 -S - "$P/.nift/tracked.json" <<'PY'
import json,sys
p=sys.argv[1]; d=json.load(open(p)); d["tracked"][0]["minify"]="yes"; json.dump(d,open(p,"w"))
PY
if (cd "$P" && "$NIFT_BIN" status >bad.log 2>&1); then echo "non-bool tracked minify accepted" >&2; exit 1; fi
grep -Fq 'tracked minify override must be a boolean' "$P/bad.log"

# Per-file minify true on unsupported output extension fails before overwriting prior output.
P="$TMP/failure-preserve"; mkproj "$P"
(cd "$P" && "$NIFT_BIN" build-all >/dev/null)
cp "$P/public/index.html" "$P/old"
cp "$P/.nift/public/index.info.json" "$P/oldinfo"
python3 -S - "$P/.nift/tracked.json" <<'PY'
import json,sys,os
p=sys.argv[1]; os.chmod(p,0o644); d=json.load(open(p))
d["tracked"][0]["output-ext"]=".txt"; d["tracked"][0]["minify"]=True
json.dump(d,open(p,"w"))
PY
# This output path has changed, so old index.html remains untouched; build must fail.
if (cd "$P" && "$NIFT_BIN" build-updated >fail.log 2>&1); then echo "unsupported forced minify succeeded" >&2; exit 1; fi
grep -Fq 'no minifier is available for output extension .txt' "$P/fail.log"
cmp "$P/public/index.html" "$P/old"

# Standalone command is project-independent, supports absolute paths, processes
# valid files even if another argument fails, and writes sibling .min files by default.
D="$TMP/standalone"; mkdir -p "$D"
cat >"$D/a.html" <<'EOF'
<div>   hello   world </div>
EOF
cat >"$D/b.css" <<'EOF'
.x { color : red ; }
EOF
printf 'not supported\n' >"$D/c.txt"
(
  cd /
  if "$NIFT_BIN" minify "$D/a.html" "$D/b.css" "$D/c.txt" >"$D/min.log" 2>&1; then
    echo "mixed supported/unsupported minify returned success" >&2
    exit 1
  fi
)
grep -Fq '<div>   hello   world </div>' "$D/a.html"
grep -Fq '.x { color : red ; }' "$D/b.css"
grep -Fq '<div> hello world </div>' "$D/a.min.html"
grep -Fq '.x{color:red;}' "$D/b.min.css"
grep -Fq 'unsupported extension .txt' "$D/min.log"

# Standalone XML/SVG/JSON/JSX paths.
printf ' { "a" : 1 } \n' >"$D/x.json"
printf '<root><!--x--><a> v </a></root>\n' >"$D/x.xml"
printf '<svg><text>a  b</text></svg>\n' >"$D/x.svg"
printf 'const  x = <div>{ a +  1 }</div>;\n' >"$D/x.jsx"
"$NIFT_BIN" minify "$D/x.json" "$D/x.xml" "$D/x.svg" "$D/x.jsx" >/dev/null
grep -Fxq '{"a":1}' "$D/x.min.json"
! grep -Fq '<!--x-->' "$D/x.min.xml"
grep -Fq 'a  b' "$D/x.min.svg"
grep -Fq '{a+1}' "$D/x.min.jsx"
# Explicit in-place mode is destructive by request.
"$NIFT_BIN" minify --in-place "$D/x.json" >/dev/null
grep -Fxq '{"a":1}' "$D/x.json"


# Minifier failure is transactional for a tracked output: last good output and
# page metadata survive if rendered text cannot be minified.
P="$TMP/json-transaction"; mkdir -p "$P/.nift" "$P/content" "$P/templates" "$P/public"
cat >"$P/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".json","output-dir":"public/","output-ext":".json","default-template":"templates/template.json","build-threads":1,"incremental-mode":"modified","minify-exts":[".json"]}}
JSON
cat >"$P/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"data","template":"templates/template.json"}]}
JSON
printf '@content\n' >"$P/templates/template.json"
printf '{ "ok" : true }\n' >"$P/content/index.json"
(cd "$P" && "$NIFT_BIN" build-all >/dev/null)
cp "$P/public/index.json" "$P/old-output"
cp "$P/.nift/public/index.info.json" "$P/old-info"
printf '{ "broken": }\n' >"$P/content/index.json"
if (cd "$P" && "$NIFT_BIN" build-updated >bad-json.log 2>&1); then
  echo "invalid JSON minification unexpectedly succeeded" >&2; exit 1
fi
grep -Fq 'minification failed: invalid JSON' "$P/bad-json.log"
cmp "$P/public/index.json" "$P/old-output"
cmp "$P/.nift/public/index.info.json" "$P/old-info"

# Parallel minification uses no shared mutable minifier state.
P="$TMP/parallel"; mkdir -p "$P/.nift" "$P/content/page" "$P/templates" "$P/public"
cat >"$P/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":12,"incremental-mode":"modified","minify-exts":[".html"]}}
JSON
python3 -S - "$P/.nift/tracked.json" <<'PY'
import json,sys
json.dump({"tracked":[{"name":f"page/{i}","title":f"P{i}","template":"templates/template.html"} for i in range(80)]},open(sys.argv[1],"w"))
PY
printf '<main>   @content   </main>\n' >"$P/templates/template.html"
for i in $(seq 0 79); do printf '<span>   %s   </span>\n' "$i" >"$P/content/page/$i.html"; done
(cd "$P" && "$NIFT_BIN" build-all >/dev/null)
for i in 0 7 31 79; do
  grep -Fq "<main> <span> $i </span> </main>" "$P/public/page/$i.html"
done

# Standalone malformed input must fail without truncating the original.
D="$TMP/standalone-failure"; mkdir -p "$D"
printf '{ "broken": }\n' >"$D/bad.json"
cp "$D/bad.json" "$D/original"
if "$NIFT_BIN" minify "$D/bad.json" >"$D/log" 2>&1; then
  echo "standalone invalid JSON minification unexpectedly succeeded" >&2; exit 1
fi
cmp "$D/bad.json" "$D/original"
test ! -e "$D/bad.min.json"
if "$NIFT_BIN" minify >"$D/noargs.log" 2>&1; then
  echo "minify without files unexpectedly succeeded" >&2; exit 1
fi
grep -Fq 'minify requires at least one file' "$D/noargs.log"
printf 'const  q =  1 ;\n' >"$D/inplace.js"
"$NIFT_BIN" minify -i "$D/inplace.js" >/dev/null
grep -Fq 'const q=1;' "$D/inplace.js"
test ! -e "$D/inplace.min.js"
if "$NIFT_BIN" minify --wat "$D/inplace.js" >"$D/badopt.log" 2>&1; then
  echo "unknown minify option unexpectedly succeeded" >&2; exit 1
fi
grep -Fq "unknown minify option '--wat'" "$D/badopt.log"

echo "Minifier integration smoke test passed"

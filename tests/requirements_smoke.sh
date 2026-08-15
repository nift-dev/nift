#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-reqs-test.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

mkproj() {
  local d="$1"
  mkdir -p "$d/.nift" "$d/content" "$d/templates" "$d/public/assets"
  cat >"$d/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}}
JSON
  cat >"$d/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"}]}
JSON
  printf '@content\n' >"$d/templates/template.html"
}

# Repeated pathto calls dedupe into one req; skipped branches do not create reqs.
P="$TMP/dedupe"; mkproj "$P"
printf 'x\n' >"$P/public/assets/a.txt"
printf 'y\n' >"$P/public/assets/skipped.txt"
cat >"$P/content/index.html" <<'EOF'
@json("data.json", data)
<a href="@pathto('public/assets/a.txt')">A</a>
<a href="@pathto('public/assets/a.txt')">A again</a>
@if(false){<a href="@pathto('public/assets/skipped.txt')">skip</a>}
@for(x : data.items){<i>@pathto('public/assets/a.txt')</i>}
EOF
printf '{"items":[1,2,3]}\n' >"$P/data.json"
(cd "$P" && "$NIFT_BIN" build-all >/dev/null)
python3 -S - "$P/.nift/public/index.info.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))
assert d["reqs"] == ["public/assets/a.txt"], d["reqs"]
PY

# Removing a req marks for rebuild, but modifying source to remove the reference repairs it.
rm "$P/public/assets/a.txt"
(cd "$P" && "$NIFT_BIN" status >status.log)
grep -Fq 'required path missing: public/assets/a.txt' "$P/status.log"
printf '<p>repaired</p>\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build-updated >/dev/null)
python3 -S - "$P/.nift/public/index.info.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))
assert d["reqs"] == [], d["reqs"]
PY

# A malformed req entry is a rebuild reason, not a crash.
P="$TMP/malformed"; mkproj "$P"
printf '<p>ok</p>\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build-all >/dev/null)
python3 -S - "$P/.nift/public/index.info.json" <<'PY'
import json,sys,os
p=sys.argv[1]
os.chmod(p,0o644)
d=json.load(open(p)); d["reqs"]=[123]; json.dump(d,open(p,"w"))
PY
(cd "$P" && "$NIFT_BIN" status >status.log)
grep -Fq 'page build metadata has an invalid requirement' "$P/status.log"
(cd "$P" && "$NIFT_BIN" build-updated >/dev/null)

# A tracked-name pathto records the target output, and deleting that output only
# selects the referring page for rebuild. Rebuilding the target restores validity.
P="$TMP/tracked"; mkproj "$P"
python3 -S - "$P/.nift/tracked.json" <<'PY'
import json,sys
p=sys.argv[1]; d=json.load(open(p)); d["tracked"].append({"name":"about","title":"About","template":"templates/template.html"}); json.dump(d,open(p,"w"))
PY
printf '<a href="@pathto('"'"'about'"'"')">About</a>\n' >"$P/content/index.html"
printf '<p>about</p>\n' >"$P/content/about.html"
(cd "$P" && "$NIFT_BIN" build-all >/dev/null)
grep -Fq '"public/about.html"' "$P/.nift/public/index.info.json"
rm "$P/public/about.html"
(cd "$P" && "$NIFT_BIN" status >status.log)
grep -Fq 'required path missing: public/about.html' "$P/status.log"
(cd "$P" && "$NIFT_BIN" build-updated >/dev/null)
test -f "$P/public/about.html"


# Corrupt internal metadata must not be allowed to point dependency/req checks
# outside the project root.
P="$TMP/poisoned"; mkproj "$P"
printf '<p>ok</p>\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build-all >/dev/null)
printf 'outside\n' >"$TMP/outside.txt"
python3 -S - "$P/.nift/public/index.info.json" <<'PY'
import json,sys,os
p=sys.argv[1]; os.chmod(p,0o644)
d=json.load(open(p)); d["reqs"]=["../outside.txt"]; json.dump(d,open(p,"w"))
PY
(cd "$P" && "$NIFT_BIN" status >poison-req.log)
grep -Fq 'page build metadata has an invalid requirement' "$P/poison-req.log"

(cd "$P" && "$NIFT_BIN" build-all >/dev/null)
python3 -S - "$P/.nift/public/index.info.json" <<'PY'
import json,sys,os
p=sys.argv[1]; os.chmod(p,0o644)
d=json.load(open(p)); d["dependencies"]=["../outside.txt"]; json.dump(d,open(p,"w"))
PY
(cd "$P" && "$NIFT_BIN" status >poison-dep.log)
grep -Fq 'page build metadata has an invalid dependency' "$P/poison-dep.log"

echo "Requirements smoke test passed"

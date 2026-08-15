#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-meta-safety.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

mkproj(){
  local d="$1"
  mkdir -p "$d/.nift" "$d/content" "$d/templates" "$d/public/assets" "$d/data"
  cat >"$d/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}}
JSON
  cat >"$d/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"}]}
JSON
  printf '@content\n' >"$d/templates/template.html"
}

# Corrupt page-info must not make status trust paths outside the project.
P="$TMP/corrupt"; mkproj "$P"
printf '<p>ok</p>\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build-all >/dev/null)
chmod u+w "$P/.nift/public/index.info.json"
python3 -S - "$P/.nift/public/index.info.json" <<'PY'
import json,sys
p=sys.argv[1]; d=json.load(open(p))
d["dependencies"]=["../../outside-secret.txt"]
d["reqs"]=["../../outside-required.txt"]
json.dump(d,open(p,"w"))
PY
printf 'secret\n' >"$TMP/outside-secret.txt"
printf 'required\n' >"$TMP/outside-required.txt"
(cd "$P" && "$NIFT_BIN" status >status.log)
grep -Fq 'invalid dependency' "$P/status.log"
grep -Fq 'invalid requirement' "$P/status.log"

# A req symlink that was safe at build time but is later retargeted outside the
# project must stop satisfying the project-local @pathto invariant.
P="$TMP/retarget"; mkproj "$P"
printf 'inside\n' >"$P/public/assets/inside.txt"
printf 'outside\n' >"$TMP/outside-target.txt"
ln -s "inside.txt" "$P/public/assets/link.txt"
printf '<a href="@pathto('"'"'public/assets/link.txt'"'"')">x</a>\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build-all >/dev/null)
rm "$P/public/assets/link.txt"
ln -s "$TMP/outside-target.txt" "$P/public/assets/link.txt"
(cd "$P" && "$NIFT_BIN" status >status.log)
grep -Fq 'page build metadata has an invalid requirement' "$P/status.log"

echo "Metadata safety smoke test passed"

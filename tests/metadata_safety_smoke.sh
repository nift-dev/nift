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

# Runtime 2.0 decision D-002: page-info paths outside the project are ordinary
# external dependencies/requirements and are checked, not rejected.
P="$TMP/corrupt"; mkproj "$P"
printf '<p>ok</p>\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
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
if grep -Fq 'invalid dependency' "$P/status.log"; then echo "unexpected match: 'invalid dependency' "$P/status.log"" >&2; exit 1; fi
if grep -Fq 'invalid requirement' "$P/status.log"; then echo "unexpected match: 'invalid requirement' "$P/status.log"" >&2; exit 1; fi
rm "$TMP/outside-required.txt"
(cd "$P" && "$NIFT_BIN" status >status.log)
grep -Fq 'required path missing: ../../outside-required.txt' "$P/status.log"

# CP03 R2: @path targets are ordinary filesystem paths. Retargeting a leaf or
# directory symlink outside the project is allowed (the deployment warning may
# be recorded, but it is not a containment failure or a rebuild loop).
P="$TMP/retarget"; mkproj "$P"
printf 'inside\n' >"$P/public/assets/inside.txt"
printf 'outside\n' >"$TMP/outside-target.txt"
ln -s "inside.txt" "$P/public/assets/link.txt"
printf '<a href="@path('"'"'public/assets/link.txt'"'"')">x</a>\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
rm "$P/public/assets/link.txt"
ln -s "$TMP/outside-target.txt" "$P/public/assets/link.txt"
(cd "$P" && "$NIFT_BIN" status >status.log)
grep -Fq 'up to date' "$P/status.log"
(cd "$P" && "$NIFT_BIN" build >/dev/null 2>&1)

# Directory symlink retargeting is likewise an ordinary path now.
P="$TMP/retarget-parent"; mkproj "$P"
mkdir -p "$P/public/inside-dir" "$TMP/outside-dir"
printf 'inside\n' >"$P/public/inside-dir/file.txt"
printf 'outside\n' >"$TMP/outside-dir/file.txt"
ln -s "inside-dir" "$P/public/linkdir"
printf '<a href="@path('"'"'public/linkdir/file.txt'"'"')">x</a>\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
rm "$P/public/linkdir"
ln -s "$TMP/outside-dir" "$P/public/linkdir"
(cd "$P" && "$NIFT_BIN" status >status.log)
grep -Fq 'up to date' "$P/status.log"

echo "Metadata safety smoke test passed"

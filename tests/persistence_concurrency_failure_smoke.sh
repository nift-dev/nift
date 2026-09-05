#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-state-concurrency.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

mkproj() {
  local d="$1" threads="${2:-4}"
  mkdir -p "$d/.nift" "$d/content" "$d/templates" "$d/public" "$d/data" "$d/schemas"
  cat >"$d/.nift/config.json" <<JSON
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":$threads,"incremental-mode":"modified"}}
JSON
}

# Failed render must preserve last successful output + page metadata.
P="$TMP/preserve"; mkproj "$P" 1
cat >"$P/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"}]}
JSON
printf '@content\n' >"$P/templates/template.html"
printf '<p>GOOD-V1</p>\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
cp "$P/public/index.html" "$P/old-output"
cp "$P/.nift/public/index.info.json" "$P/old-info"

cat >"$P/templates/template.html" <<'EOF'
@json(broken, "data/missing.json")
@content
EOF
if (cd "$P" && "$NIFT_BIN" build >failed.log 2>&1); then
  echo "broken render unexpectedly succeeded" >&2; exit 1
fi
cmp "$P/public/index.html" "$P/old-output"
cmp "$P/.nift/public/index.info.json" "$P/old-info"
(cd "$P" && "$NIFT_BIN" status >status.log)
grep -Fq 'needs rebuilding' "$P/status.log"

# Repair after failure must work without deleting persistent metadata manually.
printf '@content\n' >"$P/templates/template.html"
printf '<p>GOOD-V2</p>\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --repair >/dev/null)
grep -Fq 'GOOD-V2' "$P/public/index.html"

# Multi-page build: one failed page must not stop independent pages succeeding,
# and the failed page must retain its old successful output/info.
P="$TMP/partial"; mkproj "$P" 8
python3 -S - "$P/.nift/tracked.json" <<'PY'
import json,sys
items=[]
for i in range(20):
    items.append({"name":f"p{i}","title":f"P{i}","template":"templates/template.html"})
json.dump({"tracked":items},open(sys.argv[1],"w"))
PY
printf '@content\n' >"$P/templates/template.html"
for i in $(seq 0 19); do printf '<p>OLD-%s</p>\n' "$i" >"$P/content/p$i.html"; done
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
cp "$P/public/p7.html" "$P/p7-old"
cp "$P/.nift/public/p7.info.json" "$P/p7-info-old"

# One page gets an invalid @json call via its content; others get valid changes.
printf '@json(x, "data/missing.json")\n' >"$P/content/p7.html"
for i in $(seq 0 19); do
  if [ "$i" != 7 ]; then printf '<p>NEW-%s</p>\n' "$i" >"$P/content/p$i.html"; fi
done
if (cd "$P" && "$NIFT_BIN" build >partial.log 2>&1); then
  echo "partial failed build returned success" >&2; exit 1
fi
cmp "$P/public/p7.html" "$P/p7-old"
cmp "$P/.nift/public/p7.info.json" "$P/p7-info-old"
for i in 0 1 2 6 8 12 19; do grep -Fq "NEW-$i" "$P/public/p$i.html"; done

# Repair only failed page; subsequent build must converge to clean state.
printf '<p>NEW-7</p>\n' >"$P/content/p7.html"
(cd "$P" && "$NIFT_BIN" build --repair >/dev/null)
grep -Fq 'NEW-7' "$P/public/p7.html"
(cd "$P" && "$NIFT_BIN" status >clean.log)
! grep -Fq 'needs rebuilding' "$P/clean.log"

# Shared JSON/schema/input caches under concurrency. All pages consume the same
# sources while page-local metadata remains independent.
P="$TMP/shared"; mkproj "$P" 12
python3 -S - "$P/.nift/tracked.json" <<'PY'
import json,sys
json.dump({"tracked":[{"name":f"page/{i}","title":f"T{i}","template":"templates/template.html"} for i in range(120)]},open(sys.argv[1],"w"))
PY
mkdir -p "$P/content/page" "$P/templates/parts"
for i in $(seq 0 119); do printf '\n' >"$P/content/page/$i.html"; done
cat >"$P/data/shared.json" <<'JSON'
{"items":[{"name":"c","rank":3},{"name":"a","rank":1},{"name":"b","rank":2}]}
JSON
cat >"$P/schemas/shared.json" <<'JSON'
{"type":"object","required":["items"],"properties":{"items":{"type":"array","items":{"type":"object","required":["name","rank"],"properties":{"name":{"type":"string"},"rank":{"type":"integer"}}}}}}
JSON
cat >"$P/templates/parts/list.html" <<'EOF'
@for(item : data.items by item.rank asc){$[item.name]}
EOF
cat >"$P/templates/template.html" <<'EOF'
@json(data, "schemas/shared.json", "data/shared.json")
<title>$[title]</title>
@input("templates/parts/list.html")
@content
EOF
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
for i in 0 1 17 63 119; do
  grep -Fq "<title>T$i</title>" "$P/public/page/$i.html"
  grep -Fq 'abc' "$P/public/page/$i.html"
  grep -Fq '"data/shared.json"' "$P/.nift/public/page/$i.info.json"
  grep -Fq '"schemas/shared.json"' "$P/.nift/public/page/$i.info.json"
done

# Repeat concurrent forced builds to exercise shared caches and readonly rewrites.
for n in 1 2 3; do (cd "$P" && "$NIFT_BIN" build --all >/dev/null); done

# Failed shared schema update: all selected pages fail, existing outputs remain.
cp "$P/public/page/0.html" "$P/shared-old"
printf '%s\n' '{"type":"object","properties":{"items":{"type":"string"}}}' >"$P/schemas/shared.json"
if (cd "$P" && "$NIFT_BIN" build >schema-many.log 2>&1); then
  echo "invalid shared schema unexpectedly succeeded" >&2; exit 1
fi
cmp "$P/public/page/0.html" "$P/shared-old"

# Repair shared schema and converge.
cat >"$P/schemas/shared.json" <<'JSON'
{"type":"object","required":["items"],"properties":{"items":{"type":"array"}}}
JSON
(cd "$P" && "$NIFT_BIN" build --repair >/dev/null)
(cd "$P" && "$NIFT_BIN" status >final.log)
! grep -Fq 'needs rebuilding' "$P/final.log"

echo "Persistence/concurrency/failed-build-state smoke test passed"

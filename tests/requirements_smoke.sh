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
@json(data, "data.json")
<a href="@pathto('public/assets/a.txt')">A</a>
<a href="@pathto('public/assets/a.txt')">A again</a>
@if(false){<a href="@pathto('public/assets/skipped.txt')">skip</a>}
@for(x : data.items){<i>@pathto('public/assets/a.txt')</i>}
EOF
printf '{"items":[1,2,3]}\n' >"$P/data.json"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
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
(cd "$P" && "$NIFT_BIN" build >/dev/null)
python3 -S - "$P/.nift/public/index.info.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))
assert d["reqs"] == [], d["reqs"]
PY

# A malformed req entry is a rebuild reason, not a crash.
P="$TMP/malformed"; mkproj "$P"
printf '<p>ok</p>\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
python3 -S - "$P/.nift/public/index.info.json" <<'PY'
import json,sys,os
p=sys.argv[1]
os.chmod(p,0o644)
d=json.load(open(p)); d["reqs"]=[123]; json.dump(d,open(p,"w"))
PY
(cd "$P" && "$NIFT_BIN" status >status.log)
grep -Fq 'page build metadata has an invalid requirement' "$P/status.log"
(cd "$P" && "$NIFT_BIN" build >/dev/null)

# A tracked-name pathto records the target output, but the producer owns its own
# build state. A missing tracked output must not make the referring page stale or
# emit a misleading "required path missing" reason. The target itself is selected.
P="$TMP/tracked"; mkproj "$P"
python3 -S - "$P/.nift/tracked.json" <<'PY'
import json,sys
p=sys.argv[1]; d=json.load(open(p)); d["tracked"].append({"name":"about","title":"About","template":"templates/template.html"}); json.dump(d,open(p,"w"))
PY
printf '<a href="@pathto('"'"'about'"'"')">About</a>\n' >"$P/content/index.html"
printf '<p>about</p>\n' >"$P/content/about.html"
# Build only the referrer first so its recorded requirement points at an output
# that has never existed. A normal incremental build should then build only about.
(cd "$P" && "$NIFT_BIN" build / >/dev/null)
grep -Fq '"public/about.html"' "$P/.nift/public/index.info.json"
test ! -f "$P/public/about.html"
(cd "$P" && "$NIFT_BIN" status >status.log)
grep -Fq 'about' "$P/status.log"
! grep -Fq 'required path missing: public/about.html' "$P/status.log"
! grep -Fxq '/' "$P/status.log"
(cd "$P" && "$NIFT_BIN" build >build.log)
test -f "$P/public/about.html"
! grep -Fq 'required path missing: public/about.html' "$P/build.log"

# If the tracked producer later fails with no output present, the referrer is still
# a successful/up-to-date artifact; the overall invocation fails because the
# producer itself failed. This keeps build success non-transitive and easy to explain.
rm "$P/public/about.html"
printf '@input("missing-partial.html")\n' >"$P/content/about.html"
if (cd "$P" && "$NIFT_BIN" build >producer-fail.log 2>&1); then
  echo "tracked producer failure unexpectedly succeeded" >&2
  exit 1
fi
grep -Fq 'while building about' "$P/producer-fail.log"
! grep -Fq 'while building /' "$P/producer-fail.log"
! grep -Fq 'required path missing: public/about.html' "$P/producer-fail.log"


# Corrupt internal metadata must not be allowed to point dependency/req checks
# outside the project root.
P="$TMP/poisoned"; mkproj "$P"
printf '<p>ok</p>\n' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
printf 'outside\n' >"$TMP/outside.txt"
python3 -S - "$P/.nift/public/index.info.json" <<'PY'
import json,sys,os
p=sys.argv[1]; os.chmod(p,0o644)
d=json.load(open(p)); d["reqs"]=["../outside.txt"]; json.dump(d,open(p,"w"))
PY
(cd "$P" && "$NIFT_BIN" status >poison-req.log)
grep -Fq 'page build metadata has an invalid requirement' "$P/poison-req.log"

(cd "$P" && "$NIFT_BIN" build --all >/dev/null)
python3 -S - "$P/.nift/public/index.info.json" <<'PY'
import json,sys,os
p=sys.argv[1]; os.chmod(p,0o644)
d=json.load(open(p)); d["dependencies"]=["../outside.txt"]; json.dump(d,open(p,"w"))
PY
(cd "$P" && "$NIFT_BIN" status >poison-dep.log)
grep -Fq 'page build metadata has an invalid dependency' "$P/poison-dep.log"

echo "Requirements smoke test passed"

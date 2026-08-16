#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-template-optional.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

P="$TMP/project"
mkdir -p "$P/.nift" "$P/content" "$P/templates" "$P/public"
cat >"$P/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/default.html","build-threads":1,"incremental-mode":"hash"}}
JSON
cat >"$P/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"Home"}]}
JSON
cat >"$P/content/index.html" <<'EOF'
@if(true){<main>$[title]</main>}
EOF

# Omission makes content the parsed top-level Nift source.
(cd "$P" && "$NIFT_BIN" build-all >/dev/null)
grep -Fq '<main>Home</main>' "$P/public/index.html"
python3 -S - "$P/.nift/public/index.info.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["template"] == "", d
assert d["dependencies"] == ["content/index.html"], d
PY

# Switching to a real template adds the dependency and preserves @content.
printf '<body>@content</body>\n' >"$P/templates/page.html"
python3 -S - "$P/.nift/tracked.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d["tracked"][0]["template"] = "templates/page.html"
json.dump(d, open(p, "w"))
PY
(cd "$P" && "$NIFT_BIN" build-updated >/dev/null)
grep -Fq '<body><main>Home</main>' "$P/public/index.html"
grep -Fq '"templates/page.html"' "$P/.nift/public/index.info.json"

# Switching back removes the old template relationship cleanly.
python3 -S - "$P/.nift/tracked.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
del d["tracked"][0]["template"]
json.dump(d, open(p, "w"))
PY
(cd "$P" && "$NIFT_BIN" build-updated >/dev/null)
! grep -Fq '"templates/page.html"' "$P/.nift/public/index.info.json"
printf '<body>changed</body>\n' >"$P/templates/page.html"
(cd "$P" && "$NIFT_BIN" status >status.log)
! grep -Fq 'needs rebuilding' "$P/status.log"

# Empty is invalid; omission is the unambiguous template-less form.
python3 -S - "$P/.nift/tracked.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d["tracked"][0]["template"] = ""
json.dump(d, open(p, "w"))
PY
if (cd "$P" && "$NIFT_BIN" status >empty.log 2>&1); then
  echo "empty tracked template was accepted" >&2
  exit 1
fi
grep -Fq 'template must be omitted' "$P/empty.log"

# New CSS and JavaScript entries no longer use identity templates.
S="$TMP/scaffold"
mkdir "$S"
(cd "$S" && "$NIFT_BIN" init >/dev/null)
python3 -S - "$S/.nift/tracked.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
assets = {item["name"]: item for item in d["tracked"] if item["name"].startswith("assets/")}
assert "template" not in assets["assets/css/style"], assets
assert "template" not in assets["assets/js/script"], assets
PY
test ! -e "$S/templates/template.css"
test ! -e "$S/templates/template.js"

echo "Optional tracked template smoke test passed"

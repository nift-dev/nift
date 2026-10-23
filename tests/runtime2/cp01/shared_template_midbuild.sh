#!/usr/bin/env bash
# shared_template_midbuild.sh <nift> <label> <mode> <pages> <edit-delay-s>...
# A shared template is edited (new bytes) while a build renders all pages, so
# pages rendered after the first read consume the build-wide cached OLD bytes.
# Then 3 ordinary builds with no edits. Reports pages still lacking the new
# template text after each build, plus `nift status`. Derived from the prior
# shared_cache_probe.sh, parameterised by incremental mode.
set -u -o pipefail; NIFT="$1"; LABEL="$2"; MODE="$3"; N="$4"; shift 4
BASE="$(mktemp -d)"
trap 'rm -rf "$BASE"' EXIT
for DELAY in "$@"; do
T="$(mktemp -d -p "$BASE")"; cd "$T"
python3 -I - "$N" "$MODE" <<'PY'
import json, os, sys
n, mode = int(sys.argv[1]), sys.argv[2]
for d in (".nift", "content", "templates", "public"): os.makedirs(d, exist_ok=True)
json.dump({"config": {"content-dir": "content/", "content-ext": ".html", "output-dir": "public/", "output-ext": ".html",
  "default-template": "templates/template.html", "build-threads": 1, "incremental-mode": mode, "minify-exts": []}}, open(".nift/config.json", "w"))
json.dump({"tracked": [{"name": f"p{i}", "title": f"P{i}", "template": "templates/template.html"} for i in range(n)]}, open(".nift/tracked.json", "w"))
open("templates/template.html", "w").write("<html>TPL-OLD @content</html>\n")
for i in range(n): open(f"content/p{i}.html", "w").write(f"<p>{i}</p>\n" + "x" * 2000 + "\n")
PY
"$NIFT" build --all >/dev/null 2>&1
sleep 0.05
for i in $(seq 0 $((N-1))); do echo "<p>$i</p> v2" >> content/p$i.html; done   # every page dirty
sleep 0.05
( sleep "$DELAY"; printf '<html>TPL-NEW @content</html>\n' > templates/template.html ) &
t0=$(date +%s.%N); "$NIFT" build >/dev/null 2>&1; t1=$(date +%s.%N); wait
dur=$(echo "$t1 - $t0" | bc)
stale() { grep -L 'TPL-NEW' public/p*.html | wc -l; }
res="during:$(stale)"
for k in 1 2 3; do "$NIFT" build >/dev/null 2>&1; res="$res next$k:$(stale)"; done
st=$("$NIFT" status 2>&1 | grep -E 'up to date|need' | head -1)
echo "$LABEL mode=$MODE pages=$N delay=$DELAY build_s=$dur stale $res status='$st'"
cd "$BASE"; rm -rf "$T"
done

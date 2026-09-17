#!/usr/bin/env bash
# Hierarchy pay-for-use gate: ordinary builds that never query hierarchy must
# construct no hierarchy state; a page that queries hierarchy builds the index
# and publishes the compact structural fingerprint once, unchanged on no-change.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="${NIFT_BIN:-$ROOT/nift}"
TMP=$(mktemp -d); trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP"

mkdir -p .nift content/posts templates public
printf '{"config":{"content-dir":"content/","content-ext":".md","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}' > .nift/config.json
printf 'Home\n' > content/index.md
for i in 0 1 2; do printf 'Body %s\n' "$i" > "content/posts/p$i.md"; done

# plain build: no hierarchy usage -> no hierarchy state
printf '@content' > templates/plain.html
printf 'SIB:<!--$[page.siblings.size()]-->|@content' > templates/hier.html
printf '{"tracked":[{"name":"/","title":"Home","template":"templates/plain.html"}' > .nift/tracked.json
for i in 0 1 2; do printf ',{"name":"posts/p%s","title":"P%s","template":"templates/plain.html"}' "$i" "$i" >> .nift/tracked.json; done
printf ']}\n' >> .nift/tracked.json
"$NIFT" build --all >/dev/null
[ ! -e .nift/hierarchy.fingerprint ] || { echo "FAIL: plain build created hierarchy fingerprint" >&2; exit 1; }
echo "PASS  plain-template-no-hierarchy-state"

# now make the home page use hierarchy (current page binding) and rebuild
printf 'SIB:<!--$[page.siblings.size()]-->|@content' > templates/plain.html
printf '{"tracked":[{"name":"/","title":"Home","template":"templates/hier.html"}' > .nift/tracked.json
for i in 0 1 2; do printf ',{"name":"posts/p%s","title":"P%s","template":"templates/hier.html"}' "$i" "$i" >> .nift/tracked.json; done
printf ']}\n' >> .nift/tracked.json
"$NIFT" build --all >/dev/null
[ -e .nift/hierarchy.fingerprint ] || { echo "FAIL: hierarchy build did not create fingerprint" >&2; exit 1; }
fp1=$(cat .nift/hierarchy.fingerprint)
grep -q 'SIB:<!--0-->' public/index.html || { echo "FAIL: current-page siblings wrong: $(cat public/index.html)" >&2; exit 1; }
# no-change build keeps the fingerprint stable and stays clean
out=$("$NIFT" build 2>&1)
[ "$(cat .nift/hierarchy.fingerprint)" = "$fp1" ] || { echo "FAIL: fingerprint changed on no-change build" >&2; exit 1; }
if printf '%s\n' "$out" | grep -q built; then echo "FAIL: no-change build rebuilt pages" >&2; exit 1; fi
echo "PASS  hierarchy-build-publishes-fingerprint-once"

echo "hierarchy pay-for-use gate passed"
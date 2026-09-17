#!/usr/bin/env bash
# Hierarchy incremental-correctness wall: hierarchy consumers must rebuild on
# structural change (add/remove/rename/reparent) via the compact structural
# fingerprint, must NOT rebuild on unrelated edits, no-change must stay clean,
# and incremental output must equal a clean build.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="${NIFT_BIN:-$ROOT/nift}"
TMP=$(mktemp -d); trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP"

mkdir -p .nift content/docs/advanced templates public
printf '{"config":{"content-dir":"content/","content-ext":".md","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}' > .nift/config.json
tracked='{"tracked":[{"name":"/","title":"Home","template":"templates/plain.html"},{"name":"docs","title":"Docs","template":"templates/template.html"},{"name":"docs/advanced","title":"Advanced","template":"templates/template.html"},{"name":"docs/basics","title":"Basics","template":"templates/plain.html"}]}'
printf '%s' "$tracked" > .nift/tracked.json
printf '# Home\n' > content/index.md
printf '# Docs\n' > content/docs.md
printf '# Advanced\n' > content/docs/advanced.md
printf '# Basics\n' > content/docs/basics.md
# hierarchy consumers render the current page's sibling count; plain pages do not
printf 'SIB:<!--$[page.siblings.size()]-->|@content' > templates/template.html
printf '@content' > templates/plain.html

"$NIFT" build --all >/dev/null 2>&1
[ -e .nift/hierarchy.fingerprint ] || { echo "FAIL: hierarchy fingerprint not created"; exit 1; }

rebuild_contains() { # $1 = substring
  local out; out=$("$NIFT" build 2>&1)
  printf '%s\n' "$out" | grep -q "built $1"
}

clean_state() { # hash all html outputs
  find public -name '*.html' -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | awk '{print $1}'
}

# no-change clean
sleep 0.01
out=$("$NIFT" build 2>&1)
if printf '%s\n' "$out" | grep -q "built"; then echo "FAIL: no-change build rebuilt pages" >&2; exit 1; fi
echo "PASS  no-change-clean"

# unrelated content edit must not rebuild hierarchy consumers (docs/advanced)
sleep 0.01
printf '# Home edited\n' > content/index.md
sleep 0.01
out=$("$NIFT" build 2>&1)
if printf '%s\n' "$out" | grep -q "built docs/advanced"; then echo "FAIL: unrelated edit rebuilt hierarchy consumer" >&2; exit 1; fi
echo "PASS  unrelated-edit-scope"

# add a child under docs -> docs (and its children's sibling counts) rebuild
sleep 0.01
python3 - <<'PY'
import json
tr=json.load(open('.nift/tracked.json'))
tr['tracked'].append({'name':'docs/newchild','title':'New','template':'templates/template.html'})
json.dump(tr,open('.nift/tracked.json','w'))
PY
printf '# New\n' > content/docs/newchild.md
sleep 0.01
rebuild_contains 'docs/advanced' || { echo "FAIL: add-child did not rebuild hierarchy consumer" >&2; exit 1; }
inc=$(cat public/docs/advanced.html)
[ "$inc" = "SIB:<!--2-->|# Advanced" ] || { echo "FAIL: add-child sibling count wrong: $inc" >&2; exit 1; }
echo "PASS  add-child"

# remove the new child
sleep 0.01
python3 - <<'PY'
import json
tr=json.load(open('.nift/tracked.json'))
tr['tracked']=[t for t in tr['tracked'] if t['name']!='docs/newchild']
json.dump(tr,open('.nift/tracked.json','w'))
PY
rm -f content/docs/newchild.md
sleep 0.01
rebuild_contains 'docs/advanced' || { echo "FAIL: remove-child did not rebuild hierarchy consumer" >&2; exit 1; }
[ "$(cat public/docs/advanced.html)" = "SIB:<!--1-->|# Advanced" ] || { echo "FAIL: remove-child sibling count wrong" >&2; exit 1; }
echo "PASS  remove-child"

# rename docs/basics -> docs/getting-started
sleep 0.01
python3 - <<'PY'
import json
tr=json.load(open('.nift/tracked.json'))
tr['tracked']=[({'name':'docs/getting-started','title':'Getting Started','template':'templates/template.html'} if t['name']=='docs/basics' else t) for t in tr['tracked']]
json.dump(tr,open('.nift/tracked.json','w'))
PY
rm -f content/docs/basics.md; printf '# Getting Started\n' > content/docs/getting-started.md
sleep 0.01
rebuild_contains 'docs/advanced' || { echo "FAIL: rename did not rebuild hierarchy consumer" >&2; exit 1; }
echo "PASS  rename"

# reparent getting-started under docs/advanced
sleep 0.01
python3 - <<'PY'
import json
tr=json.load(open('.nift/tracked.json'))
tr['tracked']=[({'name':'docs/advanced/getting-started','title':'Getting Started','template':'templates/template.html'} if t['name']=='docs/getting-started' else t) for t in tr['tracked']]
json.dump(tr,open('.nift/tracked.json','w'))
PY
rm -f content/docs/getting-started.md; printf '# Getting Started\n' > content/docs/advanced/getting-started.md
sleep 0.01
rebuild_contains 'docs/advanced' || { echo "FAIL: reparent did not rebuild hierarchy consumer" >&2; exit 1; }
[ "$(cat public/docs/advanced.html)" = "SIB:<!--0-->|# Advanced" ] || { echo "FAIL: reparent sibling count wrong: $(cat public/docs/advanced.html)" >&2; exit 1; }
echo "PASS  reparent"

# clean-vs-incremental parity
sleep 0.01
inc_state=$(clean_state)
"$NIFT" build --all >/dev/null 2>&1
clean_state2=$(clean_state)
[ "$inc_state" = "$clean_state2" ] || { echo "FAIL: incremental output differs from clean build" >&2; exit 1; }
echo "PASS  clean-vs-incremental-parity"

# ordinary pages carry no hierarchy dependency
python3 - <<'PY'
import json
d=json.load(open('.nift/public/index.info.json'))
assert '.nift/hierarchy.fingerprint' not in d['dependencies'], 'ordinary page should not depend on hierarchy'
PY
echo "PASS  ordinary-page-no-hierarchy-dep"

echo "hierarchy incremental-correctness wall passed"
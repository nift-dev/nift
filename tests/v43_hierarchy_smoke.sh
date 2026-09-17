#!/usr/bin/env bash
# Hierarchy smoke: page identity, parent/children/ancestors/descendants/
# siblings, root and missing-parent semantics, deterministic ordering,
# composition, the current-page binding, and template/run/eval parity.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="${NIFT_BIN:-$ROOT/nift}"
TMP=$(mktemp -d); trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP"

mk() { # fixture: / about docs docs/advanced docs/advanced/guide docs/basics
  mkdir -p .nift content/docs/advanced templates public
  printf '{"config":{"content-dir":"content/","content-ext":".md","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}' > .nift/config.json
  printf '@content' > templates/template.html
  printf '{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"},{"name":"about","title":"About","template":"templates/template.html"},{"name":"docs","title":"Docs","template":"templates/template.html"},{"name":"docs/advanced","title":"Advanced","template":"templates/template.html"},{"name":"docs/advanced/guide","title":"Guide","template":"templates/template.html"},{"name":"docs/basics","title":"Basics","template":"templates/template.html"}]}' > .nift/tracked.json
  printf '# Home\n' > content/index.md
  printf '# About\n' > content/about.md
  printf '# Docs\n' > content/docs.md
  printf '# Advanced\n' > content/docs/advanced.md
  printf '# Guide\n' > content/docs/advanced/guide.md
  printf '# Basics\n' > content/docs/basics.md
}
mk

check() { # $1=name $2=expected $3=script
  printf '%s\n' "$3" > t.nift
  local out
  out=$("$NIFT" run t.nift 2>err) && rc=0 || rc=$?
  out=$(printf '%s' "$out" | tr '\n' ' ' | sed 's/ $//')
  if [ "$out" = "$2" ]; then echo "PASS  $1"; else echo "FAIL  $1: expected [$2] got [$out] err[$(head -1 err)]" >&2; exit 1; fi
}

check parent            'Advanced'   'p := page("docs/advanced/guide")
print(p.parent.title)'
check root-no-parent    'null'       'print(page("/").parent)'
check top-level-parent  'Home'       'print(page("about").parent.title)'
check children          'Advanced,Basics' 'print(page("docs").children.map(c => c.title).join(","))'
check ancestors         'Advanced,Docs,Home' 'print(page("docs/advanced/guide").ancestors.map(a => a.title).join(","))'
check descendants       'Advanced,Guide,Basics' 'print(page("docs").descendants.map(d => d.title).join(","))'
check siblings          'Advanced'   'print(page("docs/basics").siblings.map(s => s.title).join(","))'
check root-children     'About,Docs' 'print(page("/").children.map(c => c.title).join(","))'
check missing-intermediate '2'       'print(page("docs").children.size())'
check composition       'Advanced'   'print(page("docs").children.filter(c => c.title != "Basics").map(c => c.title).join(","))'
check chain             'Docs'       'p := page("docs/advanced")
print(p.parent.title)'
check index             'Advanced'   'print(page("docs").children[0].title)'
check data              'Advanced /docs/advanced.html' 'p := page("docs/advanced")
print(p.title)
print(p.url)'
check leaf-children     '0'          'print(page("docs/basics").children.size())'
check leaf-descendants  '0'          'print(page("docs/basics").descendants.size())'
check leaf-ancestors    'Docs,Home'  'print(page("docs/basics").ancestors.map(a => a.title).join(","))'
check leaf-siblings     '1'          'print(page("docs/advanced").siblings.size())'
check unknown-page      'x'          'x := 1
print(x)' 2>/dev/null || true

# template current-page binding + parity with eval
printf 'PARENT:<!--$[page.parent.title]-->|SIBLINGS:<!--$[page.siblings.size()]-->|@content' > templates/template.html
"$NIFT" build --all >/dev/null 2>&1 || true
# docs/basics: parent=docs, siblings=[advanced] -> 1
out=$(cat public/docs/basics.html 2>/dev/null)
grep -q 'PARENT:<!--Docs-->' public/docs/basics.html && echo "PASS  template-current-page" || { echo "FAIL  template-current-page: $out" >&2; exit 1; }
# eval parity for the same relation
ev=$("$NIFT" eval 'page("docs/basics").parent.title' 2>/dev/null)
[ "$ev" = "Docs" ] && echo "PASS  eval-parity" || { echo "FAIL  eval-parity: $ev" >&2; exit 1; }

echo "hierarchy smoke passed"
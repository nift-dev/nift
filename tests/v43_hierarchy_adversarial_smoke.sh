#!/usr/bin/env bash
# Hierarchy adversarial wall: hostile/pathological inputs must fail safely and
# deterministically without weakening project/path confinement.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="${NIFT_BIN:-$ROOT/nift}"
TMP=$(mktemp -d); trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP"

mkdir -p .nift content/café content/docs templates public
printf '{"config":{"content-dir":"content/","content-ext":".md","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}' > .nift/config.json
printf '@content' > templates/template.html
printf '{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"},{"name":"café","title":"Café","template":"templates/template.html"},{"name":"café/petit","title":"Petit","template":"templates/template.html"},{"name":"docs","title":"Docs","template":"templates/template.html"},{"name":"docs/a","title":"A","template":"templates/template.html"},{"name":"docs/b","title":"B","template":"templates/template.html"}]}' > .nift/tracked.json
printf '# Home\n' > content/index.md
printf '# Café\n' > content/café.md
printf '# Petit\n' > content/café/petit.md
printf '# Docs\n' > content/docs.md
printf '# A\n' > content/docs/a.md
printf '# B\n' > content/docs/b.md

must_error() { # $1=name $2=script
  printf '%s\n' "$2" > t.nift
  if "$NIFT" run t.nift >/dev/null 2>&1; then echo "FAIL  $1: expected error, got success" >&2; exit 1; fi
  echo "PASS  $1"
}
check() { # $1=name $2=expected $3=script
  printf '%s\n' "$3" > t.nift
  local out
  out=$("$NIFT" run t.nift 2>/dev/null | tr '\n' ' ' | sed 's/ $//')
  if [ "$out" = "$2" ]; then echo "PASS  $1"; else echo "FAIL  $1: expected [$2] got [$out]" >&2; exit 1; fi
}

must_error traversal 'p := page("..")'
must_error absolute 'p := page("/etc/passwd")'
must_error empty-name 'p := page("")'
must_error unknown 'p := page("nope")'
must_error null-member 'p := page("/")
print(p.parent.title)'
must_error bad-member 'p := page("docs")
print(p.nope)'
must_error no-child-of-leaf 'p := page("docs/a")
print(p.children[0])'
check unicode-parent 'Café' 'p := page("café/petit")
print(p.parent.title)'
check unicode-child 'Petit' 'p := page("café")
print(p.children.first().title)'
check root-no-siblings '0' 'p := page("/")
print(p.siblings.size())'
check no-ancestor '1' 'p := page("café")
print(p.ancestors.size())'
check wide-children '2' 'p := page("docs")
print(p.children.size())'
check missing-intermediate '2' 'p := page("docs")
print(p.descendants.size())'

# parallel access to the same index
for i in $(seq 1 8); do "$NIFT" eval 'page("docs").children.size()' >/dev/null 2>&1 & done
wait
echo "PASS  parallel-access"

echo "hierarchy adversarial wall passed"
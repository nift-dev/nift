#!/usr/bin/env bash
# Pay-for-what-you-use smoke: an ordinary project must not construct the
# project-wide query model (no .nift/project.fingerprint), while a project
# that actually accesses project.* must still build it and keep the semantic
# fingerprint dependency. Also guards front-matter/type semantics and lazy
# `nift eval`.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="${NIFT_BIN:-$ROOT/nift}"
TMP=$(mktemp -d); trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP"

mk() { # $1=project dir, $2=template, $3=typed(0/1)
  local dir="$1" tpl="$2" typed="$3" i
  mkdir -p "$dir/.nift" "$dir/content/posts" "$dir/templates" "$dir/public"
  printf '{"config":{"content-dir":"content/","content-ext":".md","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}' > "$dir/.nift/config.json"
  printf '%s' "$tpl" > "$dir/templates/template.html"
  if [ "$typed" = 1 ]; then
    printf '{"tracked":[{"name":"/","title":"Home","template":"templates/template.html","type":"post"}' > "$dir/.nift/tracked.json"
  else
    printf '{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"}' > "$dir/.nift/tracked.json"
  fi
  printf 'Home\n' > "$dir/content/index.md"
  for i in 0 1 2; do
    if [ "$typed" = 1 ]; then printf ',{"name":"posts/p%s","title":"P%s","template":"templates/template.html","type":"post"}' "$i" "$i" >> "$dir/.nift/tracked.json"; else printf ',{"name":"posts/p%s","title":"P%s","template":"templates/template.html"}' "$i" "$i" >> "$dir/.nift/tracked.json"; fi
    printf 'Body %s\n' "$i" > "$dir/content/posts/p$i.md"
  done
  printf ']}\n' >> "$dir/.nift/tracked.json"
}

# 1. ordinary (typed, but no project.* use) build must NOT create the fingerprint
mk ordinary '<!--$[frontmatter.type]-->|@content' 1
( cd ordinary && "$NIFT" build --all >/dev/null )
[ ! -e ordinary/.nift/project.fingerprint ] || { echo "FAIL: ordinary build created project fingerprint"; exit 1; }
grep -q '<!--post-->' ordinary/public/posts/p0.html || { echo "FAIL: frontmatter.type lost on ordinary typed page"; exit 1; }

# 2. project-using build MUST create the fingerprint and render project data
mk proj '<!--$[project.files.size()]-->|@content' 1
( cd proj && "$NIFT" build --all >/dev/null )
[ -e proj/.nift/project.fingerprint ] || { echo "FAIL: project build did not create fingerprint"; exit 1; }
grep -q '<!--4-->' proj/public/posts/p0.html || { echo "FAIL: project.files.size() wrong"; exit 1; }

# 3. project-aware pages record the compact semantic dependency (O(1))
grep -q 'project.fingerprint' proj/.nift/public/posts/p0.info.json || { echo "FAIL: project page missing fingerprint dependency"; exit 1; }

# 4. nift eval: ordinary expression must not build the model; project expression must
mk evalmk '<!--@content-->' 1
( cd evalmk && "$NIFT" build --all >/dev/null )
rm -f evalmk/.nift/project.fingerprint
out=$(cd evalmk && "$NIFT" eval '1+1')
[ "$out" = "2" ] || { echo "FAIL: eval 1+1 = $out"; exit 1; }
[ ! -e evalmk/.nift/project.fingerprint ] || { echo "FAIL: ordinary eval built the project model"; exit 1; }
out=$(cd evalmk && "$NIFT" eval 'project.files.size()')
[ "$out" = "4" ] || { echo "FAIL: eval project.files.size() = $out"; exit 1; }

# 5. incremental: a project-aware page rebuilds when the model changes; no-change stays clean
mk inc '<!--$[project.content.post.size()]-->|@content' 1
( cd inc && "$NIFT" build --all >/dev/null )
before=$(cat inc/public/posts/p1.html)
printf 'Body X\n' > inc/content/posts/p1.md
sleep 0.01
( cd inc && "$NIFT" build >/dev/null )
after=$(cat inc/public/posts/p1.html)
[ "$before" != "$after" ] || { echo "FAIL: project page did not rebuild on model change"; exit 1; }
grep -q 'Body X' inc/public/posts/p1.html || { echo "FAIL: edited body not reflected"; exit 1; }
before_fp=$(cat inc/.nift/project.fingerprint)
sleep 0.01
( cd inc && "$NIFT" build >/dev/null )
[ "$before_fp" = "$(cat inc/.nift/project.fingerprint)" ] || { echo "FAIL: fingerprint changed on no-change build"; exit 1; }

echo "pay-for-use smoke passed: ordinary builds construct no project model; project-aware builds keep the semantic dependency"
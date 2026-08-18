#!/usr/bin/env bash
set -euo pipefail

NIFT_BIN=${NIFT_BIN:-"$(pwd)/nift"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

run_init() {
  local dir=$1
  shift
  mkdir "$TMP/$dir"
  (cd "$TMP/$dir" && "$NIFT_BIN" init "$@" >/dev/null)
}

run_init basic
test -f "$TMP/basic/content/index.html"
test -f "$TMP/basic/public/index.html"
grep -q '<title>index</title>' "$TMP/basic/public/index.html"

run_init html --ext=.html
cmp "$TMP/basic/.nift/config.json" "$TMP/html/.nift/config.json"
cmp "$TMP/basic/.nift/tracked.json" "$TMP/html/.nift/tracked.json"

run_init php --ext=.php
test -f "$TMP/php/content/index.php"
test -f "$TMP/php/public/index.php"
grep -q '<title>index</title>' "$TMP/php/public/index.php"
test -f "$TMP/php/content/assets/css/style.css"

run_init text --ext=.txt
test -f "$TMP/text/content/index.txt"
test -f "$TMP/text/public/index.txt"
test ! -e "$TMP/text/content/assets"
python3 - "$TMP/text/.nift/config.json" <<'PY'
import json, sys
with open(sys.argv[1], encoding='utf-8') as f:
    cfg=json.load(f)['config']
assert cfg['default-template'] == ''
assert cfg['content-ext'] == '.txt'
assert cfg['output-ext'] == '.txt'
PY

for target in vercel netlify amplify azure firebase render cloudflare github-pages; do
  run_init "$target" --target="$target"
  (cd "$TMP/$target" && "$NIFT_BIN" build >/dev/null)
done

mkdir "$TMP/vercel-existing-ignore"
printf 'node_modules/\n' > "$TMP/vercel-existing-ignore/.gitignore"
(cd "$TMP/vercel-existing-ignore" && "$NIFT_BIN" init --target=vercel >/dev/null)
grep -qxF 'node_modules/' "$TMP/vercel-existing-ignore/.gitignore"
grep -qxF '.vercel/output/static/' "$TMP/vercel-existing-ignore/.gitignore"

python3 - "$TMP/vercel/.nift/config.json" "$TMP/vercel/.vercel/output/config.json" <<'PY'
import json, sys
with open(sys.argv[1], encoding='utf-8') as f: cfg=json.load(f)['config']
with open(sys.argv[2], encoding='utf-8') as f: vercel=json.load(f)
assert cfg['output-dir'] == '.vercel/output/static/'
assert vercel == {'version': 3}
PY
test -f "$TMP/vercel/.vercel/output/static/index.html"
grep -qxF '.vercel/output/static/' "$TMP/vercel/.gitignore"

python3 - "$TMP/amplify/.nift/config.json" "$TMP/amplify/.amplify-hosting/deploy-manifest.json" <<'PY'
import json, sys
with open(sys.argv[1], encoding='utf-8') as f: cfg=json.load(f)['config']
with open(sys.argv[2], encoding='utf-8') as f: manifest=json.load(f)
assert cfg['output-dir'] == '.amplify-hosting/static/'
assert manifest['version'] == 1
assert manifest['routes'] == [{'path':'/*','target':{'kind':'Static'}}]
assert manifest['framework']['name'] == 'nift'
assert manifest['framework']['version'] == '4.0.2'
PY
test -f "$TMP/amplify/.amplify-hosting/static/index.html"
grep -qxF '.amplify-hosting/static/' "$TMP/amplify/.gitignore"

grep -q 'command = "nift build"' "$TMP/netlify/netlify.toml"
grep -q 'publish = "public"' "$TMP/netlify/netlify.toml"

test -f "$TMP/azure/content/staticwebapp.config.json"
test -f "$TMP/azure/public/staticwebapp.config.json"

python3 - "$TMP/firebase/firebase.json" <<'PY'
import json, sys
with open(sys.argv[1], encoding='utf-8') as f: cfg=json.load(f)
assert cfg['hosting']['public'] == 'public'
PY

grep -q 'runtime: static' "$TMP/render/render.yaml"
grep -q 'staticPublishPath: ./public' "$TMP/render/render.yaml"

grep -q 'pages_build_output_dir = "./public"' "$TMP/cloudflare/wrangler.toml"

# GitHub Pages intentionally uses the ordinary public/ output. Deployment
# workflow setup is documented because the runner must install Nift first.
test -f "$TMP/github-pages/public/index.html"
test ! -e "$TMP/github-pages/.github/workflows/pages.yml"

expect_fail() {
  local name=$1
  shift
  mkdir "$TMP/$name"
  if (cd "$TMP/$name" && "$NIFT_BIN" init "$@" >out 2>err); then
    echo "expected init to fail: $*" >&2
    exit 1
  fi
}

expect_fail positional .html
grep -q "positional init arguments are no longer supported" "$TMP/positional/err"
grep -q -- "--ext=.html" "$TMP/positional/err"

expect_fail init_html_marker --ext=.bad/path
grep -q "init extension must begin with" "$TMP/init_html_marker/err"

expect_fail unknown_target --target=does-not-exist
grep -q "unknown init target 'does-not-exist'" "$TMP/unknown_target/err"

expect_fail target_php --target=vercel --ext=.php
grep -q "extension '.php' is not supported by target 'vercel'" "$TMP/target_php/err"

expect_fail split_ext --ext .php
grep -q -- "--ext requires '=EXT'" "$TMP/split_ext/err"

expect_fail split_target --target vercel
grep -q -- "--target requires '=PLATFORM'" "$TMP/split_target/err"

mkdir "$TMP/init-html"
if (cd "$TMP/init-html" && "$NIFT_BIN" init-html >out 2>err); then
  echo "expected init-html to fail" >&2
  exit 1
fi
grep -q "command 'init-html' has been removed" "$TMP/init-html/err"
grep -q "use 'nift init' instead" "$TMP/init-html/err"

echo "init targets smoke test passed"

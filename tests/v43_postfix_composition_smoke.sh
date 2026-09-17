#!/usr/bin/env bash
# Postfix composition: any value-producing expression can feed the next
# postfix operator (.member, [index], .method(...)). Guards the general
# composition machinery (no per-method special cases) with valid chains and
# error cases.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="${NIFT_BIN:-$ROOT/nift}"
TMP=$(mktemp -d); trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP"

cat > data.nift <<'NIFT'
posts := [
    {"title":"One","published":true,"year":2024,"tags":["nift","docs"]},
    {"title":"Two","published":false,"year":2025,"tags":["nift"]},
    {"title":"Three","published":true,"year":2025,"tags":["nift","markup"]}
]
export(posts)
NIFT

check() { # $1=name $2=expected $3=expr (must print a scalar)
  cat > t.nift <<NIFT
@import("data.nift")
NIFT
  printf 'print(%s)\n' "$3" >> t.nift
  local out
  out=$("$NIFT" run t.nift 2>err) && rc=0 || rc=$?
  out=$(printf '%s' "$out" | tr '\n' ' ' | sed 's/ $//')
  if [ "$out" = "$2" ]; then echo "PASS  $1"; else echo "FAIL  $1: expected [$2] got [$out] err[$(head -1 err)]" >&2; exit 1; fi
}

check_err() { # $1=name $2=error-substr $3=expr
  cat > t.nift <<NIFT
@import("data.nift")
NIFT
  printf 'print(%s)\n' "$3" >> t.nift
  local out
  if "$NIFT" run t.nift >/dev/null 2>err; then echo "FAIL  $1: expected error, got success" >&2; exit 1; fi
  if grep -q "$2" err; then echo "PASS  $1"; else echo "FAIL  $1: expected [$2] got [$(head -1 err)]" >&2; exit 1; fi
}

# --- valid composition chains ---
check member-after-first       'One'   'posts.filter(p => p.published).first().title'
check member-after-map-index   'Three' 'posts.filter(p => p.published).map(p => p.title)[1]'
check index-after-method       'One'   'posts.map(p => p.title)[0]'
check member-after-find        'Two'   'posts.find(p => !p.published).title'
check member-after-first-tags  'nift'  'posts.filter(p => p.published).first().tags[0]'
check member-after-last        'Three' 'posts.filter(p => p.published).last().title'
check paren-index-member       'One'   '(posts)[0].title'
check paren-call-member        'One'   '(posts.filter(p => p.published).first()).title'
check method-after-index       'ONE'   'posts.map(p => p.title)[0].to_upper()'
check deep-chain               'ONE'   'posts.filter(p => p.published).first().title.to_upper()'
check method-after-method      '2'     'posts.filter(p => p.published).last().tags.size()'
check object-get-member        'One'   '{"k":{"title":"One"}}.get("k").title'
check values-first-member      'One'   '{"a":{"title":"One"},"b":{"title":"Two"}}.values().first().title'
check filter-count-chain       '2'     'posts.filter(p => p.published).map(p => p.title).size()'

# --- error cases ---
check_err missing-member       'has no member'         'posts.first().missing'
check_err out-of-range         'out of range'          'posts.filter(p => p.published)[5]'
check_err non-object-member    'not an object'         'posts.map(p => p.published)[0].name'
check_err nested-nonobject     'not an object'         'posts.first().tags.missing'
check_err index-scalar         'cannot index'          'posts[0].title[0]'
check_err object-missing-get   'cannot index'          '{"a":1}.get("b")[0]'

echo "postfix-composition smoke passed"
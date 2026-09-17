#!/usr/bin/env bash
# Validates the collection/JSON operations documented on the Collection
# operations reference and the JSON data "working with JSON collections"
# section. Guards the docs' executable examples against drift.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="${NIFT_BIN:-$ROOT/nift}"
TMP=$(mktemp -d); trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP"

cat > data.nift <<'NIFT'
posts := [
    {"title":"One","draft":false,"year":2024,"tags":["nift","docs"]},
    {"title":"Two","draft":true,"year":2025,"tags":["nift"]},
    {"title":"Three","draft":false,"year":2025,"tags":["nift","markup"]}
]
export(posts)
NIFT

check() { # $1=name $2=expected $3=expr
  cat > t.nift <<NIFT
@import("data.nift")
NIFT
  printf 'print(%s)\n' "$3" >> t.nift
  local out
  out=$("$NIFT" run t.nift 2>err) && rc=0 || rc=$?
  out=$(printf '%s' "$out" | tr '\n' ' ' | sed 's/ $//')
  if [ "$out" = "$2" ]; then echo "PASS  $1"; else echo "FAIL  $1: expected [$2] got [$out] err[$(head -1 err)]" >&2; exit 1; fi
}

check_script() { # $1=name $2=expected $3=body (runs after @import, must print)
  cat > t.nift <<NIFT
@import("data.nift")
NIFT
  printf '%s\n' "$3" >> t.nift
  local out
  out=$("$NIFT" run t.nift 2>err) && rc=0 || rc=$?
  out=$(printf '%s' "$out" | tr '\n' ' ' | sed 's/ $//')
  if [ "$out" = "$2" ]; then echo "PASS  $1"; else echo "FAIL  $1: expected [$2] got [$out] err[$(head -1 err)]" >&2; exit 1; fi
}

# --- higher-order transformation ---
check map-double        '2 4 6'      '[1,2,3].map(x => x * 2).join(" ")'
check filter-evens      '2 4'        '[1,2,3,4].filter(x => x % 2 == 0).join(" ")'
check reduce-total      '10'         '[1,2,3,4].reduce((acc, x) => acc + x, 0)'
check any-true          'true'       '[1,2,3].any(x => x == 3)'
check any-false         'false'      '[1,2,3].any(x => x == 9)'
check all-true          'true'       '[1,2,3].all(x => x > 0)'
check all-empty-true    'true'       '[].all(x => x > 0)'
check any-empty-false   'false'      '[].any(x => x > 0)'
check find              '3'          '[1,2,3,4].find(x => x > 2)'
check find-none         'null'       '[1,2].find(x => x > 9)'
check find_index        '2'          '[3,1,4].find_index(x => x == 4)'
check find_index-none   '-1'         '[3,1,4].find_index(x => x == 9)'
check count             '2'          '[1,2,3,4].count(x => x % 2 == 0)'
check sort_by-titles    'One Two Three'  'posts.sort_by(p => p.year).map(p => p.title).join(" ")'
check group_by-years    '2'        'posts.group_by(p => p.year).size()'
# --- selection and aggregation ---
check unique            '1 2 3'      '[1,2,2,3,3,3].unique().join(" ")'
check flatten           '1 2 3 4'    '[[1,2],[3,4]].flatten().join(" ")'
check sum               '6'          '[1,2,3].sum()'
check sum-empty         '0'          '[].sum()'
check min               '1'          '[3,1,2].min()'
check max               '3'          '[3,1,2].max()'
check max-year          '2025'       'posts.map(p => p.year).max()'
check_script first             'One'        'p := posts.filter(p => !p.draft).first()
print(p.title)'
check_script last              'Two'      'p := posts.sort_by(p => p.title).last()
print(p.title)'
check slice             '2 3'        '[1,2,3,4].slice(1,3).join(" ")'
check join-csv          'One, Two, Three' 'posts.map(p => p.title).join(", ")'
check contains-true     'true'       '[1,2,3].contains(2)'
check contains-nested   'true'       '[[1,2],[3]].contains([3])'
check index_of          '1'          '[1,2,3].indexOf(2)'
check size              '3'          '[1,2,3].size()'
check length            '3'          '[1,2,3].length()'
check empty-false       'false'      '[1].empty()'
check empty-true        'true'       '[].empty()'
# --- object data ---
check keys              'a b c'      '{"a":1,"b":2,"c":3}.keys().join(" ")'
check values            '1 2 3'      '{"a":1,"b":2,"c":3}.values().join(" ")'
check entries-size      '3'          '{"a":1,"b":2,"c":3}.entries().size()'
check has-true          'true'       '{"a":1}.has("a")'
check has-false         'false'      '{"a":1}.has("b")'
check get               '1'          '{"a":1}.get("a")'
check get-default       '8080'       '{"a":1}.get("port", 8080)'
check get-missing-null  'null'       '{"a":1}.get("port")'
check merge             '2'        '{"a":1}.merge({"b":2}).size()'
check obj-size          '2'          '{"a":1,"b":2}.size()'
# --- mutation ---
# --- JSON collections section (typed content pattern) ---
check published-titles  'One Three'  'posts.filter(p => !p.draft).map(p => p.title).join(" ")'
check all-tags-unique   'nift docs markup' 'posts.map(p => p.tags).flatten().unique().join(" ")'
check recent-count      '2'          'posts.count(p => p.year == 2025)'
check has-markup        'true'       'posts.map(p => p.tags).flatten().contains("markup")'

echo "collection-operations smoke passed"
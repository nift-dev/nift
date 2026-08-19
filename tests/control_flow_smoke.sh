#!/usr/bin/env bash
set -euo pipefail

NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-control-flow.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

make_project(){
    local d="$1"
    mkdir -p "$d/.nift" "$d/content" "$d/templates" "$d/public" "$d/data"
    cat >"$d/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}
JSON
    cat >"$d/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"Control Flow","template":"templates/template.html"}]}
JSON
    printf 'CONTENT\n' >"$d/content/index.html"
}

D="$TMP/happy"
make_project "$D"
cat >"$D/data/site.json" <<'JSON'
{
  "enabled": true,
  "disabled": false,
  "type": "article",
  "count": 3,
  "low": 2,
  "high": 7,
  "word_a": "alpha",
  "word_b": "beta",
  "empty": "",
  "nonempty_array": [1],
  "empty_array": [],
  "nonempty_object": {"x":1},
  "empty_object": {},
  "lhs": "same",
  "rhs": "same",
  "items": [
    {"name":"one","show":true,"type":"article","tags":["a","b"]},
    {"name":"two","show":false,"type":"note","tags":[]},
    {"name":"three","show":true,"type":"article","tags":["c"]}
  ],
  "object": {
    "alpha":{"value":1},
    "beta":{"value":2}
  }
}
JSON

cat >"$D/templates/template.html" <<'EOF'
@json("data/site.json", site)
@if(site.enabled){IF_TRUE
}
@if(!site.disabled){NEG_TRUE
}
@if(site.type == "article"){STRING_EQ
}
@if(site.type != "note"){STRING_NE
}
@if(site.count == 3){NUMBER_EQ
}
@if(site.low < site.high){NUMBER_LT
}
@if(site.low <= 2){NUMBER_LE_EQ
}
@if(site.high > site.low){NUMBER_GT
}
@if(site.high >= 7){NUMBER_GE_EQ
}
@if(site.low >= -2){NUMBER_GE_NEGATIVE
}
@if(site.word_a < site.word_b){STRING_LT
}
@if(site.word_a <= "alpha"){STRING_LE_EQ
}
@if(site.word_b > site.word_a){STRING_GT
}
@if(site.word_b >= "beta"){STRING_GE_EQ
}
@if("a<b" == "a<b"){QUOTED_LT_LITERAL
}
@if("a>=b" == "a>=b"){QUOTED_GE_LITERAL
}
@if(title == "Control Flow"){META_TITLE
}
@if(site.disabled){BAD
}else if(site.type == "note"){BAD2
}else if(site.count == 3){ELSE_IF_THREE
}else{BAD3
}
@if(site.disabled){BAD
}else{PLAIN_ELSE
}
@for(item : site.items){
ITEM=$[item.name]
@if(item.show){SHOW=$[item.name]
}else{HIDE=$[item.name]
}
@if(item.type == "article"){ARTICLE=$[item.name]
}
@for(tag : item.tags){TAG=$[item.name]:$[tag]
}
}
@for((key, val) : site.object){
OBJ=$[key]:$[val.value]
}
@if(site.nonempty_array){NONEMPTY_ARRAY_TRUE
}
@if(!site.empty_array){EMPTY_ARRAY_FALSEY
}
@if(site.nonempty_object){NONEMPTY_OBJECT_TRUE
}
@if(!site.empty_object){EMPTY_OBJECT_FALSEY
}
@if(site.lhs == site.rhs){PATH_COMPARE
}
@if(false){@input("this-file-deliberately-does-not-exist.html")
}else{SKIPPED_INVALID_BRANCH
}
@if(true){QUOTED_BRACE="}"
}
@if(true){<#-- } raw comment brace --#>
@# } line comment brace
<!-- } html comment brace -->
COMMENT_BRACES_OK
}
@for(item:site.items){NOSPACE=$[item.name]
}
@for((key,val):site.object){NOSPACE_OBJ=$[key]
}
@content
EOF

(cd "$D" && "$NIFT_BIN" build-all >/dev/null)

OUT="$D/public/index.html"
grep -Fq 'IF_TRUE' "$OUT"
grep -Fq 'NEG_TRUE' "$OUT"
grep -Fq 'STRING_EQ' "$OUT"
grep -Fq 'STRING_NE' "$OUT"
grep -Fq 'NUMBER_EQ' "$OUT"
grep -Fq 'NUMBER_LT' "$OUT"
grep -Fq 'NUMBER_LE_EQ' "$OUT"
grep -Fq 'NUMBER_GT' "$OUT"
grep -Fq 'NUMBER_GE_EQ' "$OUT"
grep -Fq 'NUMBER_GE_NEGATIVE' "$OUT"
grep -Fq 'STRING_LT' "$OUT"
grep -Fq 'STRING_LE_EQ' "$OUT"
grep -Fq 'STRING_GT' "$OUT"
grep -Fq 'STRING_GE_EQ' "$OUT"
grep -Fq 'QUOTED_LT_LITERAL' "$OUT"
grep -Fq 'QUOTED_GE_LITERAL' "$OUT"
grep -Fq 'META_TITLE' "$OUT"
grep -Fq 'ELSE_IF_THREE' "$OUT"
grep -Fq 'PLAIN_ELSE' "$OUT"
! grep -Fq 'BAD' "$OUT"

grep -Fq 'ITEM=one' "$OUT"
grep -Fq 'ITEM=two' "$OUT"
grep -Fq 'ITEM=three' "$OUT"
grep -Fq 'SHOW=one' "$OUT"
grep -Fq 'SHOW=three' "$OUT"
grep -Fq 'HIDE=two' "$OUT"
grep -Fq 'ARTICLE=one' "$OUT"
grep -Fq 'ARTICLE=three' "$OUT"
grep -Fq 'TAG=one:a' "$OUT"
grep -Fq 'TAG=one:b' "$OUT"
grep -Fq 'TAG=three:c' "$OUT"
grep -Fq 'OBJ=alpha:1' "$OUT"
grep -Fq 'OBJ=beta:2' "$OUT"
grep -Fq 'NONEMPTY_ARRAY_TRUE' "$OUT"
grep -Fq 'EMPTY_ARRAY_FALSEY' "$OUT"
grep -Fq 'NONEMPTY_OBJECT_TRUE' "$OUT"
grep -Fq 'EMPTY_OBJECT_FALSEY' "$OUT"
grep -Fq 'PATH_COMPARE' "$OUT"
grep -Fq 'SKIPPED_INVALID_BRANCH' "$OUT"
grep -Fq 'QUOTED_BRACE="}"' "$OUT"
grep -Fq 'COMMENT_BRACES_OK' "$OUT"
grep -Fq 'NOSPACE=one' "$OUT"
grep -Fq 'NOSPACE_OBJ=alpha' "$OUT"
grep -Fq 'CONTENT' "$OUT"

# Loop bindings do not leak outside their scope.
D2="$TMP/leak"
make_project "$D2"
printf '{"items":[1]}\n' >"$D2/data/site.json"
cat >"$D2/templates/template.html" <<'EOF'
@json("data/site.json", site)
@for(item : site.items){$[item]}
AFTER=$[item]
@content
EOF
(cd "$D2" && "$NIFT_BIN" build-all >/dev/null)
grep -Fq 'AFTER=$[item]' "$D2/public/index.html"

# Nested loops may shadow the same variable and restore the outer binding.
D3="$TMP/shadow"
make_project "$D3"
cat >"$D3/data/site.json" <<'JSON'
{"groups":[{"name":"g1","items":[{"name":"a"},{"name":"b"}]},{"name":"g2","items":[{"name":"c"}]}]}
JSON
cat >"$D3/templates/template.html" <<'EOF'
@json("data/site.json", site)
@for(item : site.groups){
OUTER1=$[item.name]
@for(item : item.items){INNER=$[item.name]
}
OUTER2=$[item.name]
}
@content
EOF
(cd "$D3" && "$NIFT_BIN" build-all >/dev/null)
grep -Fq 'OUTER1=g1' "$D3/public/index.html"
grep -Fq 'OUTER2=g1' "$D3/public/index.html"
grep -Fq 'OUTER1=g2' "$D3/public/index.html"
grep -Fq 'OUTER2=g2' "$D3/public/index.html"
grep -Fq 'INNER=a' "$D3/public/index.html"
grep -Fq 'INNER=b' "$D3/public/index.html"
grep -Fq 'INNER=c' "$D3/public/index.html"

# Multiline control-flow blocks align rendered content to the directive insertion
# point rather than preserving the extra indentation used to format the source.
D4="$TMP/indentation"
make_project "$D4"
mkdir -p "$D4/templates/partials"
printf '{"enabled":true,"items":[{"name":"one","show":true},{"name":"two","show":false}]}\n' >"$D4/data/site.json"
printf 'PARTIAL-ONE\nPARTIAL-TWO\n' >"$D4/templates/partials/two-lines.html"
cat >"$D4/templates/template.html" <<'EOF'
@json("data/site.json", site)
<div>
    @for(item : site.items) {
        <p>FOR=$[item.name]</p>
    }
</div>
<section>
    @if(site.enabled) {
        <h2>IF-TRUE</h2>
    }
</section>
<main>
    @for(item : site.items) {
        <article>
            @if(item.show) {
                <span>NESTED=$[item.name]</span>
            }
        </article>
    }
</main>
<div>
    @for(item : site.items) {
        @input("templates/partials/two-lines.html")
    }
</div>
<div class="inline">@for(item : site.items) {
    <b>$[item.name]</b>
}</div>
@content
EOF
(cd "$D4" && "$NIFT_BIN" build-all >/dev/null)
OUT4="$D4/public/index.html"
grep -Eq '^    <p>FOR=one</p>$' "$OUT4"
grep -Eq '^    <p>FOR=two</p>$' "$OUT4"
! grep -Fq '        <p>FOR=one</p>' "$OUT4"
grep -Eq '^    <h2>IF-TRUE</h2>$' "$OUT4"
grep -Eq '^        <span>NESTED=one</span>$' "$OUT4"
grep -Eq '^    PARTIAL-ONE$' "$OUT4"
grep -Eq '^    PARTIAL-TWO$' "$OUT4"
grep -Eq '^ {20}<b>two</b></div>$' "$OUT4"

expect_failure(){
    local name="$1" expected="$2" template="$3" data="{}"
    if [[ $# -ge 4 ]]; then data="$4"; fi
    local d="$TMP/fail-$name"
    make_project "$d"
    printf '%s\n' "$data" >"$d/data/site.json"
    printf '%s\n' "$template" >"$d/templates/template.html"
    if (cd "$d" && "$NIFT_BIN" build-all >log 2>&1); then
        echo "$name unexpectedly succeeded" >&2
        exit 1
    fi
    grep -Fq -- "$expected" "$d/log" || {
        echo "$name did not report expected error: $expected" >&2
        cat "$d/log" >&2
        exit 1
    }
}

expect_failure if-no-close "@if has no matching ')'" '@if(site.enabled{hello' '{"enabled":true}'
expect_failure if-no-block "@if(...) must be followed by a '{...}' block" '@if(site.enabled) hello' '{"enabled":true}'
expect_failure if-unclosed-block "@if block has no matching '}'" '@if(site.enabled){hello' '{"enabled":true}'
expect_failure if-missing-value "JSON value 'site' has no member 'missing'" $'@json("data/site.json", site)\n@if(site.missing){x}' '{}'
expect_failure if-object-comparison '@if comparisons are only supported for scalar JSON values' $'@json("data/site.json", site)\n@if(site.obj == site.obj){x}' '{"obj":{"x":1}}'
expect_failure if-order-mixed '@if ordering comparisons require two numbers or two strings of the same type' $'@json("data/site.json", site)\n@if(site.value < "3"){x}' '{"value":3}'
expect_failure if-order-bool '@if ordering comparisons require two numbers or two strings of the same type' $'@json("data/site.json", site)\n@if(site.value >= false){x}' '{"value":true}'
expect_failure for-no-in "@for header must contain ':'" $'@json("data/site.json", site)\n@for(item site.items){x}' '{"items":[]}'
expect_failure for-array-bad-binding 'array @for syntax is @for(item : array)' $'@json("data/site.json", site)\n@for((a,b) : site.items){x}' '{"items":[]}'
expect_failure for-object-bad-binding 'object @for syntax is @for((key, val) : object)' $'@json("data/site.json", site)\n@for(item : site.obj){x}' '{"obj":{"a":1}}'
expect_failure for-scalar '@for can only iterate over JSON arrays or objects' $'@json("data/site.json", site)\n@for(item : site.value){x}' '{"value":1}'
expect_failure for-unclosed-block "@for block has no matching '}'" $'@json("data/site.json", site)\n@for(item : site.items){x' '{"items":[]}'
expect_failure for-reserved-binding "conflicts with built-in metadata" $'@json("data/site.json", site)\n@for(title : site.items){x}' '{"items":[1]}'
expect_failure duplicate-plain-else "plain else must be the final branch" $'@json("data/site.json", site)\n@if(false){a}else{b}else{c}' '{}'

echo "Control-flow smoke test passed"

# Loop metadata is lexical, collision-safe, and follows the rendered/sorted order.
D5="$TMP/loop-metadata-sort"
make_project "$D5"
cat >"$D5/data/site.json" <<'JSON_META'
{
  "posts": [
    {"name":"older","date":"2025-01-01","score":3},
    {"name":"new-a","date":"2026-08-01","score":9},
    {"name":"middle","date":"2026-01-01","score":5},
    {"name":"new-b","date":"2026-08-01","score":9}
  ],
  "numbers": [10, 2, 30],
  "object": {
    "c":{"rank":3},
    "a":{"rank":1},
    "b":{"rank":2}
  }
}
JSON_META
cat >"$D5/templates/template.html" <<'TMPL_META'
@json("data/site.json", site)
@for(post : site.posts by post.date desc){$[loop.index]/$[loop.length]:$[post.name]:first=$[loop.first]:last=$[loop.last]
}
--ASC--
@for(post : site.posts by post.score asc){$[loop.index0]:$[post.name]:$[post.score]
}
--SCALAR--
@for(n : site.numbers by n asc){$[n]
}
--OBJECT--
@for((key,val) : site.object by val.rank desc){$[loop.index]:$[key]:$[val.rank]
}
AFTER=$[loop.index]
@content
TMPL_META
(cd "$D5" && "$NIFT_BIN" build-all >/dev/null)
OUT="$D5/public/index.html"
grep -Fq '1/4:new-a:first=true:last=false' "$OUT"
grep -Fq '2/4:new-b:first=false:last=false' "$OUT"
grep -Fq '3/4:middle:first=false:last=false' "$OUT"
grep -Fq '4/4:older:first=false:last=true' "$OUT"
python3 - "$OUT" <<'PY_META'
from pathlib import Path
import sys
text=Path(sys.argv[1]).read_text()
assert '0:older:31:middle:52:new-a:93:new-b:9' in text
assert '--SCALAR--\n21030\n--OBJECT--' in text
assert '1:c:32:b:23:a:1' in text
PY_META
grep -Fq 'AFTER=$[loop.index]' "$OUT"

# Nested loops get their own loop metadata and restore the outer metadata.
D6="$TMP/loop-metadata-nested"
make_project "$D6"
printf '%s\n' '{"groups":[{"name":"g1","items":["a","b"]},{"name":"g2","items":["c"]}]}' >"$D6/data/site.json"
cat >"$D6/templates/template.html" <<'TMPL_NEST'
@json("data/site.json", site)
@for(group : site.groups){OUTER-BEFORE=$[loop.index]/$[loop.length]:$[group.name]
@for(item : group.items){INNER=$[loop.index]/$[loop.length]:$[item]
}
OUTER-AFTER=$[loop.index]/$[loop.length]:$[group.name]
}
@content
TMPL_NEST
(cd "$D6" && "$NIFT_BIN" build-all >/dev/null)
grep -Fq 'OUTER-BEFORE=1/2:g1' "$D6/public/index.html"
grep -Fq 'INNER=1/2:a' "$D6/public/index.html"
grep -Fq 'INNER=2/2:b' "$D6/public/index.html"
grep -Fq 'OUTER-AFTER=1/2:g1' "$D6/public/index.html"
grep -Fq 'OUTER-BEFORE=2/2:g2' "$D6/public/index.html"
grep -Fq 'INNER=1/1:c' "$D6/public/index.html"
grep -Fq 'OUTER-AFTER=2/2:g2' "$D6/public/index.html"

expect_cf_failure() {
    local name="$1" expected="$2" template="$3" json_source="{}"
    if [[ $# -ge 4 ]]; then json_source="$4"; fi
    local d="$TMP/$name"
    make_project "$d"
    printf '%s\n' "$json_source" >"$d/data/site.json"
    printf '%s\n' "$template" >"$d/templates/template.html"
    if (cd "$d" && "$NIFT_BIN" build-all >log 2>&1); then
        echo "$name unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$expected" "$d/log" >/dev/null || { echo "$name missing expected error: $expected" >&2; cat "$d/log" >&2; exit 1; }
}

expect_cf_failure loop-json-reserved "conflicts with built-in metadata/reserved bindings" \
    '@json("data/site.json", loop)' '{}'
expect_cf_failure loop-array-reserved "conflicts with built-in metadata" \
    $'@json("data/site.json", site)\n@for(loop : site.items){x}' '{"items":[1]}'
expect_cf_failure loop-object-reserved "bindings cannot conflict with built-in metadata" \
    $'@json("data/site.json", site)\n@for((loop,val) : site.object){x}' '{"object":{"a":1}}'
expect_cf_failure sort-missing-direction "sorting syntax is" \
    $'@json("data/site.json", site)\n@for(item : site.items by item.name){x}' '{"items":[{"name":"a"}]}'
expect_cf_failure sort-wrong-root "sort key must begin with loop binding" \
    $'@json("data/site.json", site)\n@for(item : site.items by site.name asc){x}' '{"name":"x","items":[{"name":"a"}]}'
expect_cf_failure sort-missing-member "has no member 'missing'" \
    $'@json("data/site.json", site)\n@for(item : site.items by item.missing asc){x}' '{"items":[{"name":"a"}]}'
expect_cf_failure sort-mixed-types "sort keys must have the same type" \
    $'@json("data/site.json", site)\n@for(item : site.items by item.key asc){x}' '{"items":[{"key":1},{"key":"2"}]}'
expect_cf_failure sort-object-key "sort keys must all be numbers or all be strings" \
    $'@json("data/site.json", site)\n@for(item : site.items by item.key asc){x}' '{"items":[{"key":{"x":1}}]}'


# Empty loops render nothing and do not leak loop metadata.
D7="$TMP/loop-empty"
make_project "$D7"
printf '%s\n' '{"empty":[],"obj":{}}' >"$D7/data/site.json"
cat >"$D7/templates/template.html" <<'TMPL_EMPTY'
@json("data/site.json", site)
A@for(item : site.empty){BAD}B
C@for((key,val) : site.obj){BAD}D
LOOP=$[loop.index]
@content
TMPL_EMPTY
(cd "$D7" && "$NIFT_BIN" build-all >/dev/null)
grep -Fq 'AB' "$D7/public/index.html"
grep -Fq 'CD' "$D7/public/index.html"
grep -Fq 'LOOP=$[loop.index]' "$D7/public/index.html"
! grep -Fq 'BAD' "$D7/public/index.html"

# Sorting is stable for duplicate keys and supports object-key sorting.
D8="$TMP/sort-stability-keys"
make_project "$D8"
printf '%s\n' '{"items":[{"id":"a","k":1},{"id":"b","k":1},{"id":"c","k":2}],"obj":{"z":1,"a":2,"m":3}}' >"$D8/data/site.json"
cat >"$D8/templates/template.html" <<'TMPL_STABLE'
@json("data/site.json", site)
@for(item : site.items by item.k asc){$[item.id]}
@for((key,val) : site.obj by key asc){$[key]}
@content
TMPL_STABLE
(cd "$D8" && "$NIFT_BIN" build-all >/dev/null)
grep -Fq 'abc' "$D8/public/index.html"
grep -Fq 'amz' "$D8/public/index.html"

# False branches must remain lazy even when they contain invalid JSON paths.
D9="$TMP/branch-laziness-new"
make_project "$D9"
printf '%s\n' '{"ok":true,"items":[1]}' >"$D9/data/site.json"
cat >"$D9/templates/template.html" <<'TMPL_LAZY'
@json("data/site.json", site)
@if(false){$[site.missing.deep]}
@if(site.ok){GOOD}else{$[site.missing]}
@for(item : site.items){@if(false){$[item.nope]}OK}
@content
TMPL_LAZY
(cd "$D9" && "$NIFT_BIN" build-all >/dev/null)
grep -Fq 'GOOD' "$D9/public/index.html"
grep -Fq 'OK' "$D9/public/index.html"

expect_cf_failure sort-bad-direction "sorting syntax is" \
    $'@json("data/site.json", site)\n@for(item : site.items by item ascx){x}' '{"items":[1]}'
expect_cf_failure sort-extra-token "sorting syntax is" \
    $'@json("data/site.json", site)\n@for(item : site.items by item asc extra){x}' '{"items":[1]}'
expect_cf_failure object-same-binding "must be distinct identifiers" \
    $'@json("data/site.json", site)\n@for((x,x) : site.obj){x}' '{"obj":{"a":1}}'
expect_cf_failure object-sort-wrong-root "object sort key must begin with key/value binding" \
    $'@json("data/site.json", site)\n@for((key,val) : site.obj by site.x asc){x}' '{"obj":{"a":1},"x":1}'

echo "Control-flow loop metadata/sorting extensions passed"

# Additional adversarial control-flow boundaries.
D7="$TMP/control-boundaries"
make_project "$D7"
cat >"$D7/data/site.json" <<'JSON_BOUND'
{
  "empty": [],
  "strings": ["z","aa","a"],
  "rows": [
    {"name":"b","meta":{"rank":2},"values":[9,1]},
    {"name":"a","meta":{"rank":1},"values":[8,3]}
  ],
  "object": {"z":1,"aa":2,"a":3},
  "zero": 0,
  "negative": -1,
  "emptyString": "",
  "nonemptyString": "x"
}
JSON_BOUND
cat >"$D7/templates/template.html" <<'TMPL_BOUND'
@json("data/site.json", site)
EMPTY-BEFORE
@for(item : site.empty){SHOULD-NOT-RENDER=$[loop.index]}
EMPTY-AFTER=$[loop.index]
--STRINGS--
@for(item : site.strings by item asc){$[item],}
--NESTED-SORT--
@for(row : site.rows by row.meta.rank desc){$[row.name]:$[row.values[1]],}
--OBJECT-KEY--
@for((key,val) : site.object by key asc){$[key]=$[val],}
@if(site.zero){BAD-ZERO}else{ZERO-FALSE}
@if(site.negative){NEG-TRUE}else{BAD-NEG}
@if(site.emptyString){BAD-EMPTY}else{EMPTYSTR-FALSE}
@if(site.nonemptyString){STRING-TRUE}else{BAD-STRING}
@content
TMPL_BOUND
(cd "$D7" && "$NIFT_BIN" build-all >/dev/null)
OUT7="$D7/public/index.html"
! grep -Fq 'SHOULD-NOT-RENDER=' "$OUT7"
grep -Fq 'EMPTY-AFTER=$[loop.index]' "$OUT7"
grep -Fq 'a,aa,z,' "$OUT7"
grep -Fq 'b:1,a:3,' "$OUT7"
grep -Fq 'a=3,aa=2,z=1,' "$OUT7"
grep -Fq 'ZERO-FALSE' "$OUT7"
grep -Fq 'NEG-TRUE' "$OUT7"
grep -Fq 'EMPTYSTR-FALSE' "$OUT7"
grep -Fq 'STRING-TRUE' "$OUT7"

expect_cf_failure sort-direction-case "sorting syntax is" \
    $'@json("data/site.json", site)\n@for(item : site.items by item ASC){x}' '{"items":[1]}'
expect_cf_failure sort-extra-token "sorting syntax is" \
    $'@json("data/site.json", site)\n@for(item : site.items by item asc nope){x}' '{"items":[1]}'
expect_cf_failure object-same-bindings "distinct identifiers" \
    $'@json("data/site.json", site)\n@for((x,x) : site.object){x}' '{"object":{"a":1}}'
expect_cf_failure object-third-binding "exactly two bindings" \
    $'@json("data/site.json", site)\n@for((a,b,c) : site.object){x}' '{"object":{"a":1}}'

echo "Control-flow adversarial boundary extensions passed"



# Logical condition composition: !, &&, ||, parentheses, precedence, and short-circuiting.
cd "$TMP"
rm -rf .nift content templates public data
mkdir -p .nift content templates public data
cat > .nift/config.json <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}
JSON
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"/","title":"logic","template":"templates/template.html"}]}
JSON
cat > templates/template.html <<'EOF'
@content
EOF
cat > data/logic.json <<'JSON'
{"a":true,"b":false,"n":5}
JSON
cat > content/index.html <<'EOF'
@json('data/logic.json', d)
@if(d.a && !d.b){AND}
@if(d.b || d.a){OR}
@if(d.a || missing.value){SHORT_OR}
@if(d.b && missing.value){BAD}
@if((d.b || d.a) && d.n >= 5){PARENS}
@if(d.a || d.b && false){PRECEDENCE}
EOF
"$NIFT_BIN" build-all >/dev/null
grep -F 'AND' public/index.html >/dev/null
grep -F 'OR' public/index.html >/dev/null
grep -F 'SHORT_OR' public/index.html >/dev/null
grep -F 'PARENS' public/index.html >/dev/null
grep -F 'PRECEDENCE' public/index.html >/dev/null
if grep -F 'BAD' public/index.html >/dev/null; then echo '&& short circuit failed' >&2; exit 1; fi

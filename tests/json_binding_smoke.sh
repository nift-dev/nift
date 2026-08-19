#!/usr/bin/env bash
set -euo pipefail

NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-json-binding.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

make_project() {
    local d="$1"
    mkdir -p "$d/.nift" "$d/content" "$d/templates" "$d/public" "$d/data"
    cat > "$d/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}
JSON
    cat > "$d/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"JSON Test","template":"templates/template.html"}]}
JSON
    printf 'CONTENT\n' > "$d/content/index.html"
}

D="$TMP/happy"
make_project "$D"
cat > "$D/data/site.json" <<'JSON'
{
  "name": "Nift",
  "empty": "",
  "version": 4,
  "enabled": true,
  "disabled": false,
  "nothing": null,
  "example": [
    {"test": "zero"},
    {"test": "one"},
    {"test": "two"},
    {
      "test": "three",
      "deep": {
        "items": [
          {"value": 10},
          {"value": 20},
          {"value": 30, "flags": [false, true]}
        ]
      }
    }
  ]
}
JSON

cat > "$D/templates/template.html" <<'EOF'
@json("data/site.json", site)
NAME=$[site.name]
VERSION=$[site.version]
TRUE=$[site.enabled]
FALSE=$[site.disabled]
NULL=$[site.nothing]
EMPTY=<$[site.empty]>
CHAIN=$[site.example[3].deep.items[2].value]
CHAINBOOL=$[site.example[3].deep.items[2].flags[1]]
@input("nested.html")
@content
EOF

cat > "$D/templates/nested.html" <<'EOF'
NESTED=$[site.example[3].test]
EOF

(cd "$D" && "$NIFT_BIN" build-all >/dev/null)

grep -Fx 'NAME=Nift' "$D/public/index.html" >/dev/null
grep -Fx 'VERSION=4' "$D/public/index.html" >/dev/null
grep -Fx 'TRUE=true' "$D/public/index.html" >/dev/null
grep -Fx 'FALSE=false' "$D/public/index.html" >/dev/null
grep -Fx 'NULL=null' "$D/public/index.html" >/dev/null
grep -Fx 'EMPTY=<>' "$D/public/index.html" >/dev/null
grep -Fx 'CHAIN=30' "$D/public/index.html" >/dev/null
grep -Fx 'CHAINBOOL=true' "$D/public/index.html" >/dev/null
grep -Fx 'NESTED=three' "$D/public/index.html" >/dev/null
grep -F '"data/site.json"' "$D/.nift/public/index.info.json" >/dev/null

expect_failure() {
    local name="$1" expected="$2" template="$3" json_source="{}"
    if [[ $# -ge 4 ]]; then json_source="$4"; fi
    local d="$TMP/$name"
    make_project "$d"
    printf '%s\n' "$json_source" > "$d/data/test.json"
    printf '%s\n' "$template" > "$d/templates/template.html"
    if (cd "$d" && "$NIFT_BIN" build-all >log 2>&1); then
        echo "$name unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$expected" "$d/log" >/dev/null || {
        echo "$name did not report expected error: $expected" >&2
        cat "$d/log" >&2
        exit 1
    }
}

expect_failure malformed-json 'json: failed to parse data/test.json' \
    '@json("data/test.json", data)' '{"broken":'
expect_failure duplicate-alias "json: name 'data' is already bound" \
    $'@json("data/test.json", data)\n@json("data/test.json", data)'
expect_failure invalid-alias 'json: name must be an identifier' \
    '@json("data/test.json", bad-name)'
expect_failure reserved-alias "conflicts with built-in metadata" \
    '@json("data/test.json", title)' '{}'
expect_failure missing-member "has no member 'missing'" \
    $'@json("data/test.json", data)\n$[data.missing]' '{"present":1}'
expect_failure out-of-range 'JSON array index 3 is out of range' \
    $'@json("data/test.json", data)\n$[data.items[3]]' '{"items":[1]}'
expect_failure wrong-index-type 'because it is not an array' \
    $'@json("data/test.json", data)\n$[data.item[0]]' '{"item":{"x":1}}'
expect_failure wrong-member-type "because the current JSON value is not an object" \
    $'@json("data/test.json", data)\n$[data.items.foo]' '{"items":[1]}'
expect_failure render-array 'cannot render JSON array' \
    $'@json("data/test.json", data)\n$[data.items]' '{"items":[1,2]}'
expect_failure render-object 'cannot render JSON object' \
    $'@json("data/test.json", data)\n$[data.item]' '{"item":{"x":1}}'

D="$TMP/missing-file"
make_project "$D"
printf '%s\n' '@json("data/nope.json", data)' > "$D/templates/template.html"
if (cd "$D" && "$NIFT_BIN" build-all >log 2>&1); then
    echo "missing-file unexpectedly succeeded" >&2
    exit 1
fi
grep -F 'json: file does not exist: data/nope.json' "$D/log" >/dev/null

D="$TMP/traversal"
make_project "$D"
printf '{}\n' > "$TMP/outside.json"
printf '%s\n' '@json("../../outside.json", data)' > "$D/templates/template.html"
if (cd "$D" && "$NIFT_BIN" build-all >log 2>&1); then
    echo "traversal unexpectedly succeeded" >&2
    exit 1
fi
grep -F 'json: path must stay inside the Nift project' "$D/log" >/dev/null

echo "JSON binding smoke test passed"


# Optional third @json parameter validates the document against a JSON Schema
# and tracks both the data and schema as dependencies.
D="$TMP/schema-happy"
make_project "$D"
cat >"$D/data/products.json" <<'JSON_PRODUCTS'
{"products":[{"name":"Widget","price":12.5,"status":"published"}]}
JSON_PRODUCTS
cat >"$D/data/products.schema.json" <<'JSON_SCHEMA'
{
  "$schema":"https://json-schema.org/draft/2020-12/schema",
  "$defs":{"product":{"type":"object","required":["name","price"],"properties":{"name":{"type":"string","minLength":1},"price":{"type":"number","minimum":0},"status":{"enum":["draft","published"]}},"additionalProperties":false}},
  "type":"object","required":["products"],"properties":{"products":{"type":"array","items":{"$ref":"#/$defs/product"}}},"additionalProperties":false
}
JSON_SCHEMA
cat >"$D/templates/template.html" <<'TMPL_SCHEMA'
@json("data/products.json", products, "data/products.schema.json")
SCHEMA=$[products.products[0].name]:$[products.products[0].price]
@content
TMPL_SCHEMA
(cd "$D" && "$NIFT_BIN" build-all >/dev/null)
grep -Fq 'SCHEMA=Widget:12.5' "$D/public/index.html"
grep -Fq '"data/products.json"' "$D/.nift/public/index.info.json"
grep -Fq '"data/products.schema.json"' "$D/.nift/public/index.info.json"

schema_failure() {
    local name="$1" expected="$2" data="$3" schema="$4"
    local d="$TMP/$name"
    make_project "$d"
    printf '%s\n' "$data" >"$d/data/test.json"
    printf '%s\n' "$schema" >"$d/data/test.schema.json"
    printf '%s\n' '@json("data/test.json", data, "data/test.schema.json")' >"$d/templates/template.html"
    if (cd "$d" && "$NIFT_BIN" build-all >log 2>&1); then
        echo "$name unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$expected" "$d/log" >/dev/null || { echo "$name missing expected schema error: $expected" >&2; cat "$d/log" >&2; exit 1; }
}

schema_failure schema-invalid-data 'does not satisfy schema data/test.schema.json (at $.price: expected number' \
    '{"price":"free"}' '{"type":"object","properties":{"price":{"type":"number"}}}'
schema_failure schema-required "required property 'name' is missing" \
    '{}' '{"type":"object","required":["name"]}'
schema_failure schema-unsupported "unsupported JSON Schema keyword 'format'" \
    '{"email":"a@example.com"}' '{"type":"object","properties":{"email":{"type":"string","format":"email"}}}'
schema_failure schema-ref-required "required property 'name' is missing" \
    '{"author":{}}' '{"$defs":{"person":{"type":"object","required":["name"]}},"properties":{"author":{"$ref":"#/$defs/person"}}}'

D="$TMP/schema-malformed"
make_project "$D"
printf '%s\n' '{}' >"$D/data/test.json"
printf '%s\n' '{"type":' >"$D/data/test.schema.json"
printf '%s\n' '@json("data/test.json", data, "data/test.schema.json")' >"$D/templates/template.html"
if (cd "$D" && "$NIFT_BIN" build-all >log 2>&1); then echo "schema-malformed unexpectedly succeeded" >&2; exit 1; fi
grep -F 'json: failed to parse schema data/test.schema.json' "$D/log" >/dev/null

D="$TMP/schema-missing"
make_project "$D"
printf '%s\n' '{}' >"$D/data/test.json"
printf '%s\n' '@json("data/test.json", data, "data/missing.schema.json")' >"$D/templates/template.html"
if (cd "$D" && "$NIFT_BIN" build-all >log 2>&1); then echo "schema-missing unexpectedly succeeded" >&2; exit 1; fi
grep -F 'json: schema file does not exist: data/missing.schema.json' "$D/log" >/dev/null

D="$TMP/schema-traversal"
make_project "$D"
printf '%s\n' '{}' >"$D/data/test.json"
printf '%s\n' '{}' >"$TMP/outside.schema.json"
printf '%s\n' '@json("data/test.json", data, "../../outside.schema.json")' >"$D/templates/template.html"
if (cd "$D" && "$NIFT_BIN" build-all >log 2>&1); then echo "schema-traversal unexpectedly succeeded" >&2; exit 1; fi
grep -F 'json: schema path must stay inside the Nift project' "$D/log" >/dev/null

echo "JSON Schema binding extensions passed"

# More schema integration boundaries through the public @json syntax.
schema_failure schema-false-root 'value is rejected by a false schema' \
    '{"x":1}' 'false'
schema_failure schema-type-union 'expected null or string' \
    '3' '{"type":["null","string"]}'
schema_failure schema-additional-schema 'at $.extra: expected number' \
    '{"known":1,"extra":"bad"}' '{"properties":{"known":{"type":"number"}},"additionalProperties":{"type":"number"}}'
schema_failure schema-contains-count 'matching items' \
    '[1,2,3]' '{"contains":{"type":"number"},"maxContains":2}'
schema_failure schema-invalid-pattern 'pattern is not a valid regular expression' \
    '"abc"' '{"pattern":"["}'

D="$TMP/schema-pointer-escape"
make_project "$D"
printf '%s\n' '3' >"$D/data/test.json"
printf '%s\n' '{"$defs":{"a/b":{"type":"number"}},"$ref":"#/$defs/a~1b"}' >"$D/data/test.schema.json"
printf '%s\n' '@json("data/test.json", data, "data/test.schema.json")' '@content' >"$D/templates/template.html"
(cd "$D" && "$NIFT_BIN" build-all >/dev/null)

echo "JSON Schema integration adversarial extensions passed"


# @join renders scalar JSON arrays with a textual/interpolated separator.
cd "$TMP"
rm -rf .nift content templates public data
mkdir -p .nift content templates public data
cat > .nift/config.json <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}
JSON
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"/","title":"join","template":"templates/template.html"}]}
JSON
cat > templates/template.html <<'EOF2'
@content
EOF2
cat > data/join.json <<'JSON'
{"tags":["C++","Nift","tooling"],"mixed":[1,true,null,"x"],"bad":[{"x":1}]}
JSON
cat > content/index.html <<'EOF2'
@json('data/join.json', d)
@join(d.tags, ', ')
@join($[d.mixed], '|')
EOF2
"$NIFT_BIN" build-all >/dev/null
grep -F 'C++, Nift, tooling' public/index.html >/dev/null
grep -F '1|true|null|x' public/index.html >/dev/null
cat > content/index.html <<'EOF2'
@json('data/join.json', d)
@join(d.bad, ',')
EOF2
if "$NIFT_BIN" build-all >/dev/null 2>&1; then echo '@join accepted object item' >&2; exit 1; fi

# @substr is zero-based, length-based, and slices on UTF-8 code-point boundaries.
cd "$TMP"
rm -rf .nift content templates public data
mkdir -p .nift content templates public data
cat > .nift/config.json <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}
JSON
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"/","title":"substr","template":"templates/template.html"}]}
JSON
cat > templates/template.html <<'EOF2'
@content
EOF2
cat > data/substr.json <<'JSON'
{"text":"café 😄 tooling"}
JSON
cat > content/index.html <<'EOF2'
@json('data/substr.json', d)
@substr($[d.text], 0, 4)|@substr($[d.text], 5, 1)|@substr($[d.text], 7, 99)|@substr($[d.text], 99, 3)|@substr($[d.text], 0, 0)
EOF2
"$NIFT_BIN" build-all >/dev/null
grep -F 'café|😄|tooling||' public/index.html >/dev/null
cat > content/index.html <<'EOF2'
@substr('abc', -1, 2)
EOF2
if "$NIFT_BIN" build-all >/dev/null 2>&1; then echo '@substr accepted negative position' >&2; exit 1; fi

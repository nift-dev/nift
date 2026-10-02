#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT_BIN:-${NIFT:-./nift}}
case "$NIFT" in /*) ;; *) NIFT="$(pwd)/$NIFT" ;; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/wd"
cd "$TMP/wd"

# ---------------------------------------------------------------- validate() schema.rejected
# valid schema rejects a candidate -> recoverable schema.rejected
[[ "$("$NIFT" -e 'try { validate({"type":"string"}, 42) } catch(e) { print(e.code); print(e.category); print(e.source != ""); print(e.line > 0); print(e.message) }')" == $'schema.rejected\nschema\ntrue\ntrue\nvalidate: at $: expected string, received number' ]]
# candidate passes -> returns candidate
[[ "$("$NIFT" -e 'print(validate({"type":"number"}, 42))')" == '42' ]]
# nested object validation
[[ "$("$NIFT" -e 'v := validate({"type":"object","properties":{"a":{"type":"integer"}},"required":["a"]}, {"a": 3}); print(v.a)')" == '3' ]]
# object with required-field missing -> schema.rejected
[[ "$("$NIFT" -e 'try { validate({"type":"object","required":["a"]}, {}) } catch(e) { print(e.code) }')" == 'schema.rejected' ]]

# ---------------------------------------------------------------- validate() invalid schema fatal
if "$NIFT" -e 'try { validate({"type":"banana"}, 42) } catch(e) { print("CAUGHT:" + e.code) }' >"$TMP/v.out" 2>"$TMP/v.err"; then
    echo 'invalid-schema validate unexpectedly succeeded' >&2; exit 1
fi
grep -q 'CAUGHT' "$TMP/v.out" && { echo 'invalid schema was caught' >&2; exit 1; }
grep -q 'validate: unknown JSON Schema type' "$TMP/v.err" || { echo 'invalid-schema message mismatch' >&2; cat "$TMP/v.err" >&2; exit 1; }

# ---------------------------------------------------------------- validate() uncaught compatibility
if "$NIFT" -e 'validate({"type":"string"}, 42)' >"$TMP/u.out" 2>"$TMP/u.err"; then
    echo 'uncaught validation rejection unexpectedly succeeded' >&2; exit 1
fi
grep -q 'expected string, received number' "$TMP/u.err" || { echo 'uncaught rejection message mismatch' >&2; exit 1; }
if grep -q '"code"\|"cause"\|"category"' "$TMP/u.err"; then
    echo 'uncaught rejection exposed Error serialization' >&2; exit 1
fi

# ---------------------------------------------------------------- @json malformed file -> json.parse_failed
printf '{ "a": 1, }' > bad.json
[[ "$("$NIFT" -e '@script { try { @json(x, "bad.json") } catch(e) { print(e.code); print(e.category) } }')" == $'json.parse_failed\njson' ]]
# @json valid file works
printf '{"a": 1}' > good.json
[[ "$("$NIFT" -e '@json(x, "good.json"); print(x.a)')" == '1' ]]

# ---------------------------------------------------------------- @json schema rejection / invalid schema
printf '42' > num.json
cat > schema-reject.f <<'NIFT'
schema := {"type":"string"}
@script {
    try { @json(x, schema, "num.json") } catch(e) { print(e.code); print(e.category) }
}
NIFT
[[ "$("$NIFT" "$TMP/wd/schema-reject.f")" == $'schema.rejected\nschema' ]]

cat > schema-invalid.f <<'NIFT'
schema := {"type":"banana"}
@script {
    try { @json(x, schema, "num.json") } catch(e) { print("CAUGHT") }
}
NIFT
if "$NIFT" "$TMP/wd/schema-invalid.f" >"$TMP/si.out" 2>"$TMP/si.err"; then
    echo 'invalid @json schema unexpectedly succeeded' >&2; exit 1
fi
grep -q 'CAUGHT' "$TMP/si.out" && { echo 'invalid @json schema was caught' >&2; exit 1; }
grep -q 'is not a valid schema' "$TMP/si.err" || { echo 'invalid @json schema message mismatch' >&2; cat "$TMP/si.err" >&2; exit 1; }

# ---------------------------------------------------------------- @json inline malformed stays fatal (authoring)
if "$NIFT" -e '@json(x) { "a": 1, }' >"$TMP/il.out" 2>"$TMP/il.err"; then
    echo 'malformed inline @json unexpectedly succeeded' >&2; exit 1
fi
grep -q 'failed to parse inline JSON' "$TMP/il.err" || { echo 'inline @json message mismatch' >&2; cat "$TMP/il.err" >&2; exit 1; }

# ---------------------------------------------------------------- fatal stays fatal (argument/authority)
for script in \
    'try { validate(1) } catch(e) { print("caught") }' \
    'try { validate({"type":"number"}, timer()) } catch(e) { print("caught") }' \
    'try { validate({"type":"number"}, open_bytes("bad.json")) } catch(e) { print("caught") }'; do
    if "$NIFT" -e "$script" >"$TMP/ff.out" 2>"$TMP/ff.err"; then
        echo "fatal validate misuse unexpectedly succeeded: $script" >&2; exit 1
    fi
    grep -q 'caught' "$TMP/ff.out" && { echo "fatal validate misuse was caught: $script" >&2; exit 1; }
done

echo 'v4.6 Batch 4 CP4c JSON/schema: PASS'
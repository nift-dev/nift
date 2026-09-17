#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"
td="$(mktemp -d)"; trap 'rm -rf "$td"' EXIT
run(){ printf '%s\n' "$1" > "$td/t.nift"; "$NIFT_BIN" run "$td/t.nift"; }
[[ "$(run 'x := {"a":1,"b":2}; print(x.entries().from_entries().stringify())')" == '{"a":1,"b":2}' ]]
[[ "$(run 'print([{"key":"x","value":1},{"key":2,"value":3}].from_entries().stringify())')" == '{"x":1,"2":3}' ]]
if run 'print([{"key":"x","value":1},{"key":"x","value":2}].from_entries())' >/dev/null 2>&1; then exit 1; fi
if run 'print([{"key":[],"value":1}].from_entries())' >/dev/null 2>&1; then exit 1; fi
if run 'print([{"key":"x"}].from_entries())' >/dev/null 2>&1; then exit 1; fi
echo 'CP172 from_entries smoke passed'

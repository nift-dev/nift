#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"; td="$(mktemp -d)"; trap 'rm -rf "$td"' EXIT
run(){ printf '%s\n' "$1" > "$td/t.nift"; "$NIFT_BIN" run "$td/t.nift"; }
[[ "$(run 'a := {"theme":{"font":"sans","sizes":[1,2]},"x":1}; b := {"theme":{"font":"serif","sizes":[9],"dark":true},"x":{"n":2}}; print(a.merge_deep(b).stringify())')" == '{"theme":{"font":"serif","sizes":[9],"dark":true},"x":{"n":2}}' ]]
[[ "$(run 'a := {"nested":{"a":1}}; b := {"nested":{"b":2}}; c := a.merge_deep(b); print(a.stringify()); print(c.stringify())')" == $'{"nested":{"a":1}}\n{"nested":{"a":1,"b":2}}' ]]
if run 'print({"a":1}.merge_deep([1]))' >/dev/null 2>&1; then exit 1; fi
echo 'CP175 merge_deep smoke passed'

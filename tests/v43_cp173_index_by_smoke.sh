#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"; td="$(mktemp -d)"; trap 'rm -rf "$td"' EXIT
run(){ printf '%s\n' "$1" > "$td/t.nift"; "$NIFT_BIN" run "$td/t.nift"; }
[[ "$(run 'xs := [{"id":"a","v":1},{"id":"b","v":2}]; print(xs.index_by(x => x.id).stringify())')" == '{"a":{"id":"a","v":1},"b":{"id":"b","v":2}}' ]]
[[ "$(run 'xs := [{"id":2},{"id":1}]; print(xs.index_by(x => x.id).keys().stringify())')" == '["2","1"]' ]]
if run 'xs := [{"id":"a"},{"id":"a"}]; print(xs.index_by(x => x.id))' >/dev/null 2>&1; then exit 1; fi
if run 'xs := [{"id":[]}]; print(xs.index_by(x => x.id))' >/dev/null 2>&1; then exit 1; fi
echo 'CP173 index_by smoke passed'

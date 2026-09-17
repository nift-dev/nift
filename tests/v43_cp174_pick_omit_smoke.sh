#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"; td="$(mktemp -d)"; trap 'rm -rf "$td"' EXIT
run(){ printf '%s\n' "$1" > "$td/t.nift"; "$NIFT_BIN" run "$td/t.nift"; }
[[ "$(run 'x := {"a":1,"b":2,"c":3}; print(x.pick(["c","a","missing"]).stringify()); print("|"); print(x.stringify())')" == '{"c":3,"a":1}
|
{"a":1,"b":2,"c":3}' ]]
[[ "$(run 'x := {"a":1,"b":2,"c":3}; print(x.omit(["b","missing"]).stringify())')" == '{"a":1,"c":3}' ]]
[[ "$(run 'x := {"a.b":1,"a":{"b":2}}; print(x.pick(["a.b"]).stringify())')" == '{"a.b":1}' ]]
if run 'print({"a":1}.pick([1]))' >/dev/null 2>&1; then exit 1; fi
echo 'CP174 pick/omit smoke passed'

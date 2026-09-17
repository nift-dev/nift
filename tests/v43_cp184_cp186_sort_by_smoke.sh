#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"
td="$(mktemp -d)"; trap 'rm -rf "$td"' EXIT
run(){ printf '%s\n' "$1" > "$td/t.nift"; "$NIFT_BIN" run "$td/t.nift"; }
A='[{"n":"a","featured":false,"date":2},{"n":"b","featured":true,"date":1},{"n":"c","featured":true,"date":3},{"n":"d","featured":true,"date":3}]'
[[ "$(run "a := $A; print(a.sort_by(x => x.date).map(x => x.n).join(\",\"))")" == 'b,a,c,d' ]]
[[ "$(run "a := $A; print(a.sort_by(x => x.featured, \"desc\", x => x.date, \"desc\").map(x => x.n).join(\",\"))")" == 'c,d,b,a' ]]
# c/d tie on every supplied key: stable source order must survive.
[[ "$(run "a := $A; print(a.sort_by(x => x.featured, \"desc\", x => x.date, \"desc\").map(x => x.n).join(\",\"))")" == 'c,d,b,a' ]]
if run "a := $A; print(a.sort_by(x => x.date, \"sideways\"))" >/dev/null 2>&1; then exit 1; fi
if run "a := $A; print(a.sort_by(x => x.date, \"asc\", x => x.n))" >/dev/null 2>&1; then exit 1; fi
echo 'CP184-CP186 stable compound sort_by smoke passed'

#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"
td="$(mktemp -d)"; trap 'rm -rf "$td"' EXIT
run(){ printf '%s\n' "$1" > "$td/t.nift"; "$NIFT_BIN" run "$td/t.nift"; }
A='[{"x":1,"tag":"a","tags":["a","b"]},{"x":2,"tag":"b","tags":["b"]},{"x":1,"tag":"a","tags":["a","a"]}]'
[[ "$(run "a := $A; print(a.partition(v => v.x > 1).stringify())")" == '{"matched":[{"x":2,"tag":"b","tags":["b"]}],"unmatched":[{"x":1,"tag":"a","tags":["a","b"]},{"x":1,"tag":"a","tags":["a","a"]}]}' ]]
[[ "$(run "a := $A; print(a.unique_by(v => v.x).stringify())")" == '[{"x":1,"tag":"a","tags":["a","b"]},{"x":2,"tag":"b","tags":["b"]}]' ]]
[[ "$(run "a := $A; print(a.min_by(v => v.x).x); print(a.max_by(v => v.x).x)")" == $'1\n2' ]]
[[ "$(run "a := $A; print(a.count_by(v => v.tag).stringify())")" == '{"a":2,"b":1}' ]]
[[ "$(run 'print([1,2,3,4].take(2).stringify()); print([1,2,3,4].drop(2).stringify()); print([1,2,3,4,5].chunk(2).stringify())')" == $'[1,2]\n[3,4]\n[[1,2],[3,4],[5]]' ]]
[[ "$(run "a := $A; print(a.group_by_each(v => v.tags).stringify())")" == '{"a":[{"x":1,"tag":"a","tags":["a","b"]},{"x":1,"tag":"a","tags":["a","a"]}],"b":[{"x":1,"tag":"a","tags":["a","b"]},{"x":2,"tag":"b","tags":["b"]}]}' ]]
if run 'print([1].partition(x => 1))' >/dev/null 2>&1; then exit 1; fi
if run 'print([1].min_by(x => []))' >/dev/null 2>&1; then exit 1; fi
if run 'print([1].count_by(x => []))' >/dev/null 2>&1; then exit 1; fi
if run 'print([1].take(-1))' >/dev/null 2>&1; then exit 1; fi
if run 'print([1].chunk(0))' >/dev/null 2>&1; then exit 1; fi
if run 'print([1].group_by_each(x => "a"))' >/dev/null 2>&1; then exit 1; fi
echo 'CP176-CP182 collection algebra smoke passed'

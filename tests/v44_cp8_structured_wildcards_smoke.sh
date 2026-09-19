#!/bin/sh
set -eu
NIFT=${NIFT:-./nift}; t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat >"$t/test.f" <<'F'
$[o := {"user_name":"n","user_id":3,"internal":true,"user_x":false}]
print(o.pick(["user_*"]).size())
print(o.omit(["user_?"]).size())
print(o.pick(["internal"]).size())
F
[ "$($NIFT run "$t/test.f")" = "3
3
1" ]
echo 'PASS v4.4 CP8 structured wildcards'

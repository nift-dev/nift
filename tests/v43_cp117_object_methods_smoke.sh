#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}
out="$($NIFT run <(printf '%s\n' '$[x := json_parse("{\\"b\\":2,\\"a\\":1}")]' '$[x.keys().join(",")]' '$[x.values().size()]' '$[x.entries().size()]' '$[x.has("a")]'))"
grep -q 'a,b' <<<"$out"
grep -q '2' <<<"$out"
grep -q 'true' <<<"$out"

#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}
out="$($NIFT eval --json '[1,2,3]')"
python3 -c 'import json,sys; assert json.loads(sys.stdin.read()) == [1,2,3]' <<<"$out"

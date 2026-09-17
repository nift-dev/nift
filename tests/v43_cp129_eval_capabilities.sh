#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}
"$NIFT" eval --capabilities | python3 -c 'import json,sys; x=json.load(sys.stdin); assert x["contract"]==1 and "keys" in x["value_methods"]'

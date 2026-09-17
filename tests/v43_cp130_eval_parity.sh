#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}
for expr in '1 + 2 * 3' '"42".to_int()' '[1,2,3].size()' '[1,2,1].contains(2)'; do
  "$NIFT" eval --json "$expr" >/dev/null
done

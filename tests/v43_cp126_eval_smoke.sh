#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}
[[ "$($NIFT eval '1 + 2')" == "3" ]]
[[ "$($NIFT eval '"hello".to_upper()')" == "HELLO" ]]

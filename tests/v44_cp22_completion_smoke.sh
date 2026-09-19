#!/usr/bin/env bash
set -euo pipefail
bin=${1:-./nift}
"$bin" complete pw | grep -qx pwd
"$bin" complete ./src/Par | grep -q './src/Parser'

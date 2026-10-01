#!/usr/bin/env bash
set -euo pipefail
bin=${1:-./nift}
"$bin" complete pw | grep -qx pwd
"$bin" complete tim | grep -qx timer
"$bin" complete module_p | grep -qx module_path
"$bin" complete package_p | grep -qx package_path
"$bin" complete ./src/Par | grep -q './src/Parser'

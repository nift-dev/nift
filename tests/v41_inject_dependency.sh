#!/usr/bin/env bash
set -euo pipefail
N=${NIFT_BIN:-./nift}; R=$(mktemp -d); trap 'rm -rf "$R"' EXIT; cd "$R"; "$N" init >/dev/null; echo '@content' > templates/template.html; echo '{"n":1}' > data.expr; echo '$[j := inject("data.expr")]$[j.n]' > content/index.html
"$N" build --all >/dev/null; grep -q 1 public/index.html; sleep 1; echo '{"n":2}' > data.expr; "$N" build >/dev/null; grep -q 2 public/index.html

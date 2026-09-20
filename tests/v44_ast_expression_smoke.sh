#!/usr/bin/env bash
set -euo pipefail
cxx=${CXX:-g++}
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
"$cxx" -std=c++17 -O2 -Isrc -Ijsonic/include tests/ast_expression_unit.cpp src/Ast.cpp -o "$t/ast-test"
"$t/ast-test"
echo 'v4.4 AST expression smoke: PASS'

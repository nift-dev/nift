#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cat > /tmp/nift_ast_fold_test.cpp <<'CPP'
#include "Ast.h"
#include <cassert>
int main(){
 auto a=nift::ast::parse_expression("1 + 2 * 3"); assert(a.supported); assert(a.expr->kind==nift::ast::Kind::Literal); assert(a.expr->literal.is_number()&&a.expr->literal.num==7);
 auto b=nift::ast::parse_expression("x + 2 * 3"); assert(b.supported); assert(b.expr->kind==nift::ast::Kind::Binary); assert(b.expr->right->kind==nift::ast::Kind::Literal); assert(b.expr->right->literal.num==6);
 auto c=nift::ast::parse_expression("1 / 0"); assert(c.supported); assert(c.expr->kind==nift::ast::Kind::Binary); // preserve observable runtime error
}
CPP
${CXX:-g++} -std=c++17 -I"$ROOT/src" -I"$ROOT/jsonic/include" /tmp/nift_ast_fold_test.cpp "$ROOT/src/Ast.cpp" -o /tmp/nift_ast_fold_test
/tmp/nift_ast_fold_test
echo 'v4.4 AST constant-fold smoke: PASS'

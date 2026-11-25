#pragma once
// Collection expression-view and ordinary lambda body lowering.
#include "rt/compiler_expr.h"

namespace nift::rt::detail {

// Legacy rejects a dot assignment to a plain object before reading the RHS.
// This only covers a whole synchronous lambda expression with a bare receiver
// and its parameter as the value. It cannot perform a store.
inline const Node* invalid_object_member_lambda(const Ast& ast, const UnitKinds& kinds, const Node& lambda) {
    if (lambda.kind != NodeKind::Lambda || lambda.count != 1 ||
        (lambda.flags & (kFlagBlockBody | kFlagAsync))) return nullptr;
    const Node& body = ast.at(lambda.b);
    if (body.kind != NodeKind::Assign) return nullptr;
    const Node& target = ast.at(body.a);
    const Node& param = ast.at(ast.list(lambda)[0]);
    if (target.kind != NodeKind::Member || (param.flags & kFlagVariadic) ||
        ast.at(body.b).kind != NodeKind::Ident || ast.text(ast.at(body.b)) != ast.text(param)) return nullptr;
    const Node& receiver = ast.at(target.a);
    return receiver.kind == NodeKind::Ident && kinds.of(target.a) == kKindObject &&
           ast.text(receiver) != ast.text(param) ? &receiver : nullptr;
}

} // namespace nift::rt::detail

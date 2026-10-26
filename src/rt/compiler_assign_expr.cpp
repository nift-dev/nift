#include "rt/compiler_assign_expr.h"
// rt-lint: cold-file assignment values, with each indexed operand evaluated once
namespace nift::rt::detail {
bool UnitCompiler::assignment_expr(const Node& node, std::uint16_t dst) {
    const auto target = ast_.at(node.a).kind;
    const auto mark = fb_->top();
    if (node.kind == NodeKind::Assign && target == NodeKind::Index) {
        if (!assign(node, &dst)) return false;
#if defined(NIFT_SESSION_MUTANT) && NIFT_SESSION_MUTANT == 6
        if (!assign(node, &dst)) return false;  // M6: evaluate and commit the assignment twice
#endif
    } else {
        if (target != NodeKind::Ident) return fail(node, "expression:assignment-target");
        if (!statement(node_id(node))) return false;
        std::uint16_t value = 0;
        if (!local_register(ast_.at(node.a), value)) return false;
        if (value != dst) fb_->emit(OpCode::Move, dst, value);
    }
    fb_->release_to(mark);
    return builder_ok();
}
} // namespace nift::rt::detail

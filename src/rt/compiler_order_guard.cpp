#include "rt/compiler_order_guard.h"

#include "rt/compiler_expr.h"

// OrderGuard (package ecosystem, B). Legacy evaluates an ordering comparison
// (<, <=, >, >=) of any two values: two numbers or two strings compare, and
// every other pair is a fatal Error that try/catch never catches, raised when
// the statement is reached, after the output of every earlier statement. Its
// text and report site depend only on the statement the comparison is in:
//
//   canonical `if` / `else if` condition: "@if ordering comparisons require two
//       numbers or two strings of the same type", at the `if`;
//   canonical `return` value: "return: ordering comparisons ...", at the return;
//   a declaration or assignment value, a `while` condition, and every
//       statement of a prepared loop region: "ordering comparisons require two
//       numbers or two strings", at the comparison.
//
// Each caller frame whose call is a return value adds one "return: " (as for
// SessionNumeric, vm_report.cpp). Where the compiler cannot prove the operand
// kinds it now emits that check (OrderGuard) instead of refusing the unit, in
// exactly these contexts, and only for a comparison reached from the context's
// expression through `&&`, `||` and `!`. Everything else keeps its `type:<op>`
// refusal: sessions, lambdas, coroutine units, call arguments, literals,
// ternaries, and code legacy prepares outside a loop region (a function
// called from a loop), whose wording has not been established.

namespace nift::rt::detail {

namespace {

constexpr char kIfWording[] = "@if ordering comparisons require two numbers or two strings of the same type";
constexpr char kPlainWording[] = "ordering comparisons require two numbers or two strings";

bool reaches(const Ast& ast, NodeId from, NodeId target, int depth) {
    if (from == kNoNode || depth > 64) return false;
    if (from == target) return true;
    const Node& node = ast.at(from);
    if (node.kind == NodeKind::Logical && (node.op == Op::And || node.op == Op::Or))
        return reaches(ast, node.a, target, depth + 1) || reaches(ast, node.b, target, depth + 1);
    if (node.kind == NodeKind::Unary && node.op == Op::Not) return reaches(ast, node.a, target, depth + 1);
    return false;
}

} // namespace

bool UnitCompiler::order_guarded(const Node& node) const {
#if defined(NIFT_ORDER_GUARD_MUTANT) && NIFT_ORDER_GUARD_MUTANT == 1  // test-only: the guard is never admitted
    return false;
#endif
    if (session_ || in_lambda_ || plan_.coroutines || order_ctx_.root == kNoNode) return false;
    // A struct method is not prepared by legacy: its body keeps the canonical
    // statement wording even when a caller invokes it from a loop. Only a loop
    // region inside the method changes the wording. Free functions called from
    // a loop are prepared and keep the refusal (the call context is dynamic).
    if (loop_reachable() && !loop_region() && !plan_.is_method(function_)) return false;
    const NodeId id = static_cast<NodeId>(&node - &ast_.at(0));
    return reaches(ast_, order_ctx_.root, id, 0);
}

bool UnitCompiler::order_guard(const Node& node, std::uint16_t left, std::uint16_t right) {
    std::string message = order_ctx_.wording == 1 ? kIfWording : kPlainWording;
    if (order_ctx_.wording == 2) message = "return: " + message;
    fb_->set_offset(order_ctx_.offset != 0 ? order_ctx_.offset : site(node));
    fb_->note(fb_->emit(OpCode::OrderGuard, 0, left, right), std::move(message));
    return builder_ok();
}

} // namespace nift::rt::detail

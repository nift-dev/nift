#include "rt/compiler_thread.h"

#include <string>

// rt-lint: cold-file compile-time thread() calls (CP22, cooperative opt-in)

namespace nift::rt::detail {

// `thread(f, args…)` in a coroutine unit (legacy's thread built-in): f, a
// synchronous top-level function named directly with matching arity, runs as
// a task with the arguments (CallValueAny with the thread bit, c & 0x8000);
// the value is its thread handle. Other forms are refused (legacy reports a
// wrong count against its internal `__thread_callable` at 1:1, a defect).
bool UnitCompiler::thread_call(const Node& node, std::string_view name, std::uint16_t dst) {
    const Node& callee = ast_.at(node.a);
    if (name != "thread" || !plan_.coroutines) return fail(callee, "callable:" + std::string(name));
    if (node.count == 0) return fail(node, "coro-thread-callee");
    const NodeId* args = ast_.list(node);
    const Node& f = ast_.at(args[0]);
    const bool named = f.kind == NodeKind::Ident && find_binding(text(f)) == nullptr;
    const auto fn = named ? plan_.index.find(text(f)) : plan_.index.end();
    if (fn == plan_.index.end() || (ast_.at(plan_.functions[fn->second - 1]).flags & kFlagAsync))
        return fail(f, "coro-thread-callee");
    if (ast_.at(plan_.functions[fn->second - 1]).count != node.count - 1) return fail(node, "coro-thread-arity");
    const std::uint16_t mark = fb_->top();
    std::uint16_t callee_reg = 0;
    if (!temp(callee_reg) || !expr_to(args[0], callee_reg)) return false;
    const std::uint16_t base = fb_->top();
    for (std::uint32_t k = 1; k < node.count; ++k) {
        if (ast_.at(args[k]).kind == NodeKind::Spread) return fail(ast_.at(args[k]), "spread");
        std::uint16_t reg = 0;
        if (!temp(reg) || !expr_to(args[k], reg)) return false;
    }
    std::uint16_t result = 0;
    if (node.count == 1 && !temp(result)) return false;
    fb_->set_offset(site(node));
    const std::size_t at = fb_->emit(OpCode::CallValueAny, base, callee_reg,
                                     static_cast<std::uint16_t>((node.count - 1u) | 0x8000u));
    fb_->note(at, std::string(text(f)));
    if (dst != base) fb_->emit(OpCode::Move, dst, base);
    fb_->release_to(mark);
    return builder_ok();
}

}  // namespace nift::rt::detail

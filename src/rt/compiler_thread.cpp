#include "rt/compiler_thread.h"

#include <string>

// rt-lint: cold-file compile-time thread() calls (CP22, cooperative opt-in)

namespace nift::rt::detail {

// `thread(callee, args…)` in a coroutine unit (legacy's thread built-in): the
// callee is any expression (a named function, closure or other function value)
// and is called by the task with the arguments (CallValueAny with the thread
// bit, c & 0x8000); the value is its thread handle. Being callable and having
// the right arity are checked by the VM at runtime, where legacy reports them.
bool UnitCompiler::thread_call(const Node& node, std::string_view name, std::uint16_t dst) {
    const Node& callee = ast_.at(node.a);
    // CP23 compatibility: the old public library call spellings stay removed,
    // with legacy's diagnostic (not an opaque parse fallback).
    if (name == "async")  // rt-lint: allow-string-compare compile time
        return fail(node, "async(callable, ...) was removed; declare an async function/lambda and call it directly");
    if (name == "await")  // rt-lint: allow-string-compare compile time
        return fail(node, "await(...) was removed; use 'await future' or 'await async_fn(args)'");
    if (name != "thread" || !plan_.coroutines) return fail(callee, "callable:" + std::string(name));
    if (node.count == 0) return fail(node, "coro-thread-callee");
    const NodeId* args = ast_.list(node);
    const Node& f = ast_.at(args[0]);
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

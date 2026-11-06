#include "rt/compiler_invocation.h"
#include "rt/mutation.h"
#include "rt/aggregate.h"
#include "rt/session_invocation.h"

// rt-lint: cold-file compile-time prologue, once per unit

namespace nift::rt::detail {

// A script unit's invocation bindings are the session's (set_session): the
// same readonly symbols, read wherever the unit runs (main, functions,
// lambdas, tasks) as globals. A script's globals are main's frame slots, so
// main's first registers hold them, set before its first statement and
// marked initialised; the isolate wrapper copies them like any global.
// A unit that never names a binding compiles exactly as without it; a
// top-level declaration of the name in the entry replaces it (entry.shadow).
bool UnitCompiler::invocation_prologue() {
    if (session_ || !invocation_) return true;
    for (const auto& entry : *invocation_) {
        const SessionSymbol& symbol = entry.symbol;
#if defined(NIFT_ARGSCMD_MUTANT) && NIFT_ARGSCMD_MUTANT == 6  // test-only: no top-level shadowing (mutants.sh)
        const NodeId shadow = kNoNode;
#else
        const NodeId shadow = entry.shadow;
#endif
        if (shadow != kNoNode) { invocation_shadows_.insert(shadow); continue; }
        bool named = false;
        for (NodeId id = 0; id < ast_.size() && !named; ++id)
            named = ast_.at(id).kind == NodeKind::Ident && text(ast_.at(id)) == symbol.name;
        if (!named) continue;
        Binding binding;
        binding.global = binding.typed = true;
        binding.invocation = true;
        binding.decl_kinds = symbol.kinds;
        binding.const_array = symbol.const_array; binding.readonly = symbol.readonly;
        if (!fb_->alloc(binding.type_reg) || !fb_->alloc(binding.reg)) return builder_ok();
        std::uint16_t k = 0;
        if (entry.value.is(Kind::Array)) {
            const auto& items = array_items(entry.value);
            if (items.size() > 0xffffu) {  // a limit of the unit, not of a source position
                if (reason_.empty()) { reason_ = "limit:invocation-arguments"; offset_ = 0; positioned_ = false; }
                return false;
            }
            const std::uint16_t mark = fb_->top();
            std::uint16_t first = mark;
            for (std::size_t i = 0; i < items.size(); ++i) {
                std::uint16_t reg = 0;
                if (!fb_->alloc(reg) || !fb_->constant(items[i], k)) return builder_ok();
                if (i == 0) first = reg;
                fb_->emit(OpCode::LoadK, reg, static_cast<std::uint16_t>(k & ~kConstantBit));
            }
            fb_->emit(OpCode::NewArray, binding.reg, first, static_cast<std::uint16_t>(items.size()));
            // B2 (owner F3): the script's `args` is immutable through every
            // alias, as the session's (session_invocation.cpp) already is.
            if (mutation::is_immutable(entry.value)) fb_->emit(OpCode::MakeImmutable, binding.reg);
            fb_->release_to(mark);
        } else {
            if (!fb_->constant(entry.value, k)) return builder_ok();
            fb_->emit(OpCode::LoadK, binding.reg, static_cast<std::uint16_t>(k & ~kConstantBit));
        }
        if (!fb_->constant(Value::integer(entry.type), k)) return builder_ok();
        fb_->emit(OpCode::LoadK, binding.type_reg, static_cast<std::uint16_t>(k & ~kConstantBit));
        fb_->keep_type(binding.type_reg);
        fb_->emit(OpCode::MarkGlobal, binding.reg);
        session_bindings_[symbol.name] = binding;
    }
    return builder_ok();
}

// D-062 #2: a write whose target is rooted at an injected binding (`args = …`,
// `args[0] += …`, `args[0][0] = …`, `args[0].x = …`, `cmd++`) is a program
// error rejected before execution, whatever the write form.
bool UnitCompiler::invocation_write(NodeId id) {
    NodeId at = ast_.at(id).a;
    while (at != kNoNode && (ast_.at(at).kind == NodeKind::Index || ast_.at(at).kind == NodeKind::Member ||
                             ast_.at(at).kind == NodeKind::SafeMember))
        at = ast_.at(at).a;
    if (at == kNoNode || ast_.at(at).kind != NodeKind::Ident) return false;
    const Binding* binding = find_binding(text(ast_.at(at)));
    if (!binding || !binding->invocation) return false;
    fail(ast_.at(ast_.at(id).a), "invocation-assign:" + std::string(text(ast_.at(at))));
    return true;
}

} // namespace nift::rt::detail

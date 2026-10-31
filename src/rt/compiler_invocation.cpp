#include "rt/compiler_invocation.h"
#include "rt/aggregate.h"

// rt-lint: cold-file compile-time prologue, once per unit

namespace nift::rt::detail {

// A script unit's invocation bindings are the session's (set_session): the
// same readonly symbols, read wherever the unit runs (main, functions,
// lambdas, tasks) as globals. A script's globals are main's frame slots, so
// main's first registers hold them, set before its first statement and
// marked initialised; the isolate wrapper copies them like any global.
// A unit that never names a binding compiles exactly as without it.
bool UnitCompiler::invocation_prologue() {
    if (session_ || !invocation_) return true;
    for (const auto& entry : *invocation_) {
        const SessionSymbol& symbol = entry.symbol;
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
            if (items.size() > 0xffffu) return fail(ast_.at(root_), "limit:invocation-arguments");
            const std::uint16_t mark = fb_->top();
            std::uint16_t first = mark;
            for (std::size_t i = 0; i < items.size(); ++i) {
                std::uint16_t reg = 0;
                if (!fb_->alloc(reg) || !fb_->constant(items[i], k)) return builder_ok();
                if (i == 0) first = reg;
                fb_->emit(OpCode::LoadK, reg, static_cast<std::uint16_t>(k & ~kConstantBit));
            }
            fb_->emit(OpCode::NewArray, binding.reg, first, static_cast<std::uint16_t>(items.size()));
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

} // namespace nift::rt::detail

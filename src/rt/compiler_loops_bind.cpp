#include "rt/compiler_loops_bind.h"

#include "rt/atom.h"

// Binding, value and loop-metadata emission for the default `for` (CP12C
// slice 2).

namespace nift::rt::detail {

bool UnitCompiler::for_read_source(const Binding& b, std::uint16_t& reg, bool& temp_owned) {
    temp_owned = false;
    if (b.lazy_for) {
        if (!for_lazy_read(b, reg)) return false;
        temp_owned = true;
        return true;
    }
    temp_owned = b.cell || b.upvalue || b.global;
    return read_binding(b, reg);
}

bool UnitCompiler::for_lazy_read(const Binding& b, std::uint16_t& reg) {
    if (b.selector) {
        if (!temp(reg)) return false;
        const std::size_t assigned = fb_->emit(OpCode::JmpFalse, b.lazy_active);
        emit_binding_read(OpCode::CellLoad, reg, b.selector_reg);
        const std::size_t done = fb_->emit(OpCode::Jmp);
        fb_->patch(assigned, fb_->here());
        emit_binding_read(OpCode::CellLoad, reg, b.reg);
        fb_->patch(done, fb_->here());
        return true;
    }
    if (b.lazy_source_binding) {
        Binding source = *b.lazy_source_binding;
        std::uint16_t src = 0, value = 0, found = 0;
        bool src_temp = false;
        if (!for_read_source(source, src, src_temp)) return false;
        if (b.lazy_pair) {
            std::uint16_t key = 0;
            if (!temp(key) || !temp(value) || !temp(found) || found != value + 1) return false;
            fb_->emit(OpCode::GetIndex, key, b.lazy_keys, b.lazy_index);
            fb_->emit(OpCode::ObjectLookup, value, src, key);
            for_clear_register(key);
        } else {
            if (!temp(value) || !temp(found) || found != value + 1) return false;
            fb_->emit(OpCode::ArrayElem, value, src, b.lazy_index);
        }
        if (src_temp) for_clear_register(src);
        const std::size_t have = fb_->emit(OpCode::JmpTrue, found);
        const std::size_t at = fb_->emit(OpCode::IterMissing);
        fb_->note(at, std::string(b.lazy_name.empty() ? b.lazy_source_name : b.lazy_name));
        fb_->patch(have, fb_->here());
        const std::size_t assigned = fb_->emit(OpCode::JmpFalse, b.lazy_active);
        const std::size_t done = fb_->emit(OpCode::Jmp);
        fb_->patch(assigned, fb_->here());
        if (b.cell) fb_->emit(OpCode::CellLoad, value, b.reg);
        else if (b.upvalue) fb_->emit(OpCode::GetUpvalue, value, b.upvalue_index);
        else fb_->emit(OpCode::Move, value, b.reg);
        fb_->patch(done, fb_->here());
        reg = value;
        return true;
    }
    return fail(ast_.at(root_), "for-lazy-source");
}

bool UnitCompiler::for_source_binding(const Node& iterable, const Binding*& binding) const {
    binding = nullptr;
    if (iterable.kind == NodeKind::Ident) binding = find_binding(text(iterable));
    return true;
}

bool UnitCompiler::for_declare_binding(std::string_view name, const Node& at, bool captured, KindSet decl_kinds,
                                       Binding& binding) {
    binding = Binding();
    binding.loop_binding = true;
    binding.typed = true;
    binding.decl_kinds = decl_kinds;
    binding.decl_may_double = false;
    binding.decl_text_double = false;
    // The type register first so the value register is the newest one.
    if (!fb_->alloc(binding.type_reg) || !fb_->alloc(binding.reg)) return builder_ok();
    // Persistent aggregate iteration can retain or mutate its source location.
    // Use the existing selector-cell path rather than an unmaterialised lazy register.
    if (captured || (session_ && (decl_kinds & kKindAggregate))) {
        if (!fb_->alloc(binding.cell_reg)) return builder_ok();
        binding.cell = true;
        binding.reg = binding.cell_reg;
    }
    if (!scopes_.declare(name, binding)) return fail(at, "static-error:redeclared");
    return true;
}

bool UnitCompiler::for_clear_register(std::uint16_t reg) {
    fb_->emit(OpCode::LoadNull, reg);
    return builder_ok();
}

bool UnitCompiler::for_bind_value(const Binding& binding, std::uint16_t value, std::uint16_t type_reg,
                                  bool first, std::uint16_t flag) {
    static_cast<void>(first);
    static_cast<void>(flag);
#if defined(NIFT_FOR_MUTANT) && NIFT_FOR_MUTANT == 3
    // M2: one cell for the whole loop instead of one per iteration.
    if (binding.cell) {
        std::uint16_t untyped = 0;
        if (!fb_->constant(Value::integer(-1), untyped)) return builder_ok();
        if (flag != 0) {
            const std::size_t have = fb_->emit(OpCode::JmpTrue, flag);
            fb_->emit(binding.location_source ? OpCode::NewSourceSlot : OpCode::NewCell,
                  binding.reg, value, type_reg);
            fb_->emit(OpCode::LoadI, flag, 1);
            const std::size_t done = fb_->emit(OpCode::Jmp);
            fb_->patch(have, fb_->here());
            // M3: reuse the first cell without rebinding later iterations.
            fb_->patch(done, fb_->here());
        } else {
            fb_->emit(OpCode::CellStore, binding.reg, value, untyped);
        }
        return builder_ok();
    }
#endif
    if (binding.cell) {
        fb_->emit(binding.location_source ? OpCode::NewSourceSlot : OpCode::NewCell,
                  binding.reg, value, type_reg);
        return builder_ok();
    }
    if (binding.reg != value) fb_->emit(OpCode::Move, binding.reg, value);
    return builder_ok();
}

bool UnitCompiler::for_emit_loop_metadata(const Binding& binding, std::uint16_t index, std::uint16_t count) {
    const Atom index_key = atom("index");
    const Atom index0_key = atom("index0");
    const Atom first_key = atom("first");
    const Atom last_key = atom("last");
    const Atom length_key = atom("length");
    for (const Atom* key : {&index_key, &index0_key, &first_key, &last_key, &length_key})
        if (key->id > 0xffff) return fail(ast_.at(root_), "limit:atoms");
    std::uint16_t object = 0, scratch = 0, zero = 0, untyped = 0;
    if (!temp(object) || !temp(scratch) || !temp(zero)) return false;
    fb_->emit(OpCode::NewObject, object);
    fb_->emit(OpCode::LoadI, zero, 0);
    fb_->emit(OpCode::AddI, scratch, index, jump_operand(1));
    fb_->emit(OpCode::ObjectSet, object, static_cast<std::uint16_t>(index_key.id), scratch);
    fb_->emit(OpCode::ObjectSet, object, static_cast<std::uint16_t>(index0_key.id), index);
    fb_->emit(OpCode::Eq, scratch, index, zero);
    fb_->emit(OpCode::ObjectSet, object, static_cast<std::uint16_t>(first_key.id), scratch);
    fb_->emit(OpCode::AddI, scratch, index, jump_operand(1));
    fb_->emit(OpCode::Eq, scratch, scratch, count);
    fb_->emit(OpCode::ObjectSet, object, static_cast<std::uint16_t>(last_key.id), scratch);
    fb_->emit(OpCode::ObjectSet, object, static_cast<std::uint16_t>(length_key.id), count);
    if (!fb_->constant(Value::integer(-1), untyped)) return builder_ok();
    fb_->emit(OpCode::BindType, binding.type_reg, object, untyped);
    if (binding.cell) fb_->emit(OpCode::NewCell, binding.reg, object, binding.type_reg);
    else if (binding.reg != object) fb_->emit(OpCode::Move, binding.reg, object);
    fb_->emit(OpCode::LoadNull, object);  // drop the construction temp
    return builder_ok();
}


} // namespace nift::rt::detail

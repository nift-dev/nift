#include "rt/compiler_cells.h"

// rt-lint: cold-file binding-slot and selector access emission, once per unit

namespace nift::rt::detail {

bool UnitCompiler::binding_cell(const Binding& b, std::uint16_t& reg) {
    if (b.upvalue) {
        if (!temp(reg)) return false;
        fb_->emit(OpCode::GetUpvalueCell, reg, b.upvalue_index);
    } else if (b.global) {
        if (!global_read(b.reg, reg)) return false;
    } else reg = b.reg;
    return true;
}

bool UnitCompiler::indexed_binding(const Binding& b, std::uint16_t& reg) {
    if (!b.selector && !(b.cell && b.loop_binding)) return read_binding(b, reg);
    if (!temp(reg)) return false;
    if (b.selector) {
        const std::size_t assigned = fb_->emit(OpCode::JmpFalse, b.lazy_active);
        fb_->emit(OpCode::CellLoadCached, reg, b.selector_reg);
        const std::size_t done = fb_->emit(OpCode::Jmp);
        fb_->patch(assigned, fb_->here());
        fb_->emit(OpCode::CellLoadCached, reg, b.reg);
        fb_->patch(done, fb_->here());
    } else if (b.cell && b.loop_binding) {
        fb_->emit(OpCode::CellLoadCached, reg, b.reg);
    } else return read_binding(b, reg);
    return true;
}

bool UnitCompiler::read_binding(const Binding& b, std::uint16_t& reg) {
    if (b.upvalue) {
        if (!temp(reg)) return false;
        fb_->emit(OpCode::GetUpvalue, reg, b.upvalue_index);
    } else if (b.cell) {
        std::uint16_t cell = 0;
        if (!binding_cell(b, cell) || !temp(reg)) return false;
        fb_->emit(OpCode::CellLoad, reg, cell);
    } else if (b.global) return global_read(b.reg, reg);
    else reg = b.reg;
    return true;
}

bool UnitCompiler::store_indexed_binding(const Binding& b, std::uint16_t value) {
    std::uint16_t untyped = 0;
    if (!fb_->constant(Value::integer(-1), untyped)) return false;
    if (b.selector) {
        const std::size_t assigned = fb_->emit(OpCode::JmpFalse, b.lazy_active);
        fb_->emit(OpCode::CellStoreCached, b.selector_reg, value);
        const std::size_t done = fb_->emit(OpCode::Jmp);
        fb_->patch(assigned, fb_->here());
        fb_->emit(OpCode::CellStore, b.reg, value, untyped);
        fb_->patch(done, fb_->here());
    } else if (b.cell && (b.loop_binding || b.location_source)) fb_->emit(OpCode::CellStoreCached, b.reg, value);
    else return store_binding(b, value, untyped);
    return true;
}

bool UnitCompiler::store_binding(const Binding& b, std::uint16_t value, std::uint16_t text_type) {
    if (b.upvalue) fb_->emit(OpCode::SetUpvalue, b.upvalue_index, value, text_type);
    else if (b.cell) {
        std::uint16_t cell = 0;
        if (!binding_cell(b, cell)) return false;
        fb_->emit(OpCode::CellStore, cell, value, text_type);
    } else if (b.global) global_put(b, value);
    else if (b.reg != value) fb_->emit(OpCode::Move, b.reg, value);
    return true;
}

} // namespace nift::rt::detail

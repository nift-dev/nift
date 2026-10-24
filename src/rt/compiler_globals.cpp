#include "rt/compiler_globals.h"

// CP12b globals in the compiler (rt/globals.h): a function's free name that
// resolves to a top-level binding (G1-G3 hold for every use) is that
// binding. Main's frame is at stack base 0, so the binding's value and type
// registers in main are absolute slots; functions read them with GetGlobal
// and write them with SetGlobal. Assignments keep the global's binding-type
// check (D-020); one meaning everywhere (D-018): the same rules as for a
// local, with the binding's analyses (decl kinds, may-double) taken from
// main's declaration.

namespace nift::rt::detail {

const Binding* UnitCompiler::find_binding(std::string_view name) const {
    if (const Binding* local = scopes_.find(name)) return local;
    if (in_lambda_) {
        const auto top = top_level_bindings_.find(name);
        return top == top_level_bindings_.end() ? nullptr : &top->second;
    }
    if (!in_function_ || globals_ == nullptr || function_ >= globals_->free.size()) return nullptr;
    // CP12b structs: methods run wherever an instance is (not ordered by G3): no globals there.
    if (plan_.is_method(function_)) return nullptr;
    const auto& free = globals_->free[function_];
    const auto ref = free.find(name);
    if (ref == free.end() || !ref->second.ok) return nullptr;
    const auto global = global_bindings_.find(name);
    return global == global_bindings_.end() ? nullptr : &global->second;
}

// Main's declaration of a global: remembered for the functions (compiled
// after main) and its binding type kept for them (FunctionBuilder::keep_type).
void UnitCompiler::note_global_declaration(const Node& node, const Binding& binding) {
    if (in_function_) return;
    const std::string_view name = text(ast_.at(node.a));
    if (!top_level_decl_ids_.count(node_id(node))) return;
    Binding global = binding;
    global.global = true;
    top_level_bindings_[name] = global;
    fb_->keep_type(binding.type_reg);
    fb_->emit(OpCode::MarkGlobal, binding.reg);
    if (globals_ == nullptr) return;
    const auto decl = globals_->globals.find(name);
    if (decl == globals_->globals.end() || &ast_.at(decl->second) != &node) return;
    global_bindings_[name] = global;
}

// The binding-type analyses of a global are main's: a function that reads
// it as a free name sees main's may-double / always-double verdicts.
bool UnitCompiler::propagate_global_doubles() {
    if (globals_ == nullptr) return false;
    const std::size_t main = plan_.functions.size();
    bool changed = false;
    for (std::size_t f = 0; f < globals_->free.size() && f < main; ++f)
        for (const auto& entry : globals_->free[f]) {
            if (!entry.second.ok) continue;
            if (may_double_[main].count(entry.first) && may_double_[f].insert(entry.first).second) changed = true;
            if (always_double_[main].count(entry.first)) always_double_[f].insert(entry.first);
        }
    return changed;
}

bool UnitCompiler::global_read(std::uint16_t slot, std::uint16_t& reg) {
    if (!temp(reg)) return false;
    fb_->emit(OpCode::GetGlobal, reg, slot);
    return true;
}

bool UnitCompiler::global_read_value(std::uint16_t slot, std::uint16_t& reg, std::string_view name) {
    if (!temp(reg)) return false;
    const std::uint32_t saved_offset = fb_->offset();
    if (global_offset_set_) fb_->set_offset(global_offset_);
    const std::size_t at = fb_->emit(OpCode::GetGlobalChecked, reg, slot);
    if (global_offset_set_) fb_->set_offset(saved_offset);
    fb_->note(at, std::string(name));
    return true;
}

bool UnitCompiler::global_check_defined(std::uint16_t slot, std::string_view name) {
    const std::size_t at = fb_->emit(OpCode::CheckGlobal, slot);
    fb_->note(at, std::string(name));
    return true;
}

// HAZARD (CP13 phase B requirement): between global_take and global_put the
// global's slot holds null. Today a fault there (SetIndex, Push) ends the
// program, so it is never observed; once the VM catches errors (try/catch),
// a caught fault would leave the global null, unlike legacy. CP13B must
// restore the slot on unwind, or update the global in place without taking.
bool UnitCompiler::global_take(const Binding& b, std::uint16_t& reg, std::string_view name) {
    if (b.cell) {
        if (b.selector_root_only) {
            std::uint16_t cell = 0;
            if (!global_read(b.reg, cell) || !temp(reg)) return false;
            fb_->emit(OpCode::CellTake, reg, cell);
            return true;
        }
        return read_binding(b, reg);
    }
    std::uint16_t empty = 0;
    if (name.empty() ? !global_read(b.reg, reg) : !global_read_value(b.reg, reg, name)) return false;
    if (!temp(empty)) return false;
    fb_->emit(OpCode::LoadNull, empty);
    fb_->emit(OpCode::SetGlobal, b.reg, empty);
    return true;
}

void UnitCompiler::global_put(const Binding& b, std::uint16_t reg) {
    if (b.cell) {
        std::uint16_t cell = 0, untyped = 0;
        if (!global_read(b.reg, cell) || !fb_->constant(Value::integer(-1), untyped)) return;
        fb_->emit(OpCode::CellStoreCached, cell, reg);
    } else fb_->emit(OpCode::SetGlobal, b.reg, reg);
}

} // namespace nift::rt::detail

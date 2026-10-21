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
    if (!in_function_ || globals_ == nullptr || function_ >= globals_->free.size()) return nullptr;
    const auto& free = globals_->free[function_];
    const auto ref = free.find(name);
    if (ref == free.end() || !ref->second.ok) return nullptr;
    const auto global = global_bindings_.find(name);
    return global == global_bindings_.end() ? nullptr : &global->second;
}

// Main's declaration of a global: remembered for the functions (compiled
// after main) and its binding type kept for them (FunctionBuilder::keep_type).
void UnitCompiler::note_global_declaration(const Node& node, const Binding& binding) {
    if (in_function_ || globals_ == nullptr) return;
    const std::string_view name = text(ast_.at(node.a));
    const auto decl = globals_->globals.find(name);
    if (decl == globals_->globals.end() || &ast_.at(decl->second) != &node) return;
    Binding global = binding;
    global.global = true;
    global_bindings_[name] = global;
    fb_->keep_type(binding.type_reg);
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

bool UnitCompiler::global_take(const Binding& b, std::uint16_t& reg) {
    std::uint16_t empty = 0;
    if (!global_read(b.reg, reg) || !temp(empty)) return false;
    fb_->emit(OpCode::LoadNull, empty);
    fb_->emit(OpCode::SetGlobal, b.reg, empty);
    return true;
}

} // namespace nift::rt::detail

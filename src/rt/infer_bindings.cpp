#include "rt/infer_bindings.h"
#include "rt/compiler.h"
// rt-lint: cold-file compile-time analysis, once per unit
namespace nift::rt::infer_detail {

bool Inference::slot_changed(const TyRef* var) {
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 32
    return false;  // A32: no reader of a binding that grows after its walk is walked again (U1)
#endif
    if (var < vars_.data() || var >= vars_.data() + vars_.size()) return false;
    const auto found = slot_readers_.find(static_cast<std::size_t>(var - vars_.data()));
    if (found == slot_readers_.end()) return false;
    notify(found->second, current_unit());  // the unit being walked: self_changed_ (merge_local, F1)
    return true;
}

// B4b 26 (review of 4c9632b8, H-1): the general dependency invariant for binding slots. A unit's
// walk reads a slot only through find() or struct_field(), and both record the unit here; every
// write that grows a slot goes through merge_local() or add_element(), and both queue the
// slot's readers (slot_changed). So whichever unit grows a slot (the declaring function, a
// lambda capturing it, main, a method, a module function, a callback), every unit whose last
// walk read the slot is walked again. U1 (B4b 15) recorded lambda readers only: a function
// whose local a lambda wrote kept the kind it read before the write grew (exit 70 on B4a too).
TyRef* Inference::note_read(TyRef* var) {
    if (!var) return var;
    const auto slot = static_cast<std::size_t>(var - vars_.data());
    if (slot >= slot_read_.size()) slot_read_.resize(slot + 1, 0);
    slot_read_[slot] = walk_serial_;  // F1: this walk has read the binding (declare)
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 36
    // A36: only lambdas are recorded (B4b 15's U1): the reverse edge, a lambda's write to its
    // owner's binding, is lost again (review of 4c9632b8, H-1)
    if (current_ <= 0 || static_cast<std::size_t>(current_) <= plan_.functions.size()) return var;
#endif
    add_reader(slot_readers_[slot], current_unit());
    return var;
}

TyRef* Inference::find(std::string_view name) {
    return note_read(find_binding(name));
}

TyRef* Inference::find_binding(std::string_view name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
        for (auto entry = scope->rbegin(); entry != scope->rend(); ++entry)
            if (entry->first == name) return &vars_[entry->second];
    // A function's free name resolved to a top-level binding (rt/globals.h).
    // A lambda uses its enclosing named function's free-name map.
    const int owner = current_ >= 0 && static_cast<std::size_t>(current_) < plan_.functions.size()
                          ? current_
                          : owner_function_;
    const std::size_t slot = globals_.find(owner, name, persist_index_, vars_, ast_.size());
    if (slot != GlobalSlots::kNone) return &vars_[slot];
    const auto external = external_.find(name);
    if (external != external_.end() && (!plan_.index.count(name) ||
        (session_ && session_->phased_functions.count(std::string(name))))) return &vars_[external->second];
    if (current_ < 0 || static_cast<std::size_t>(current_) <= plan_.functions.size()) return nullptr;
    const auto top = top_level_slots_.find(name);
    return top == top_level_slots_.end() ? nullptr : &vars_[top->second];
}

} // namespace nift::rt::infer_detail

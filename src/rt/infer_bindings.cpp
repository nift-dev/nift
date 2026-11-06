#include "rt/infer_bindings.h"
#include "rt/compiler.h"
// rt-lint: cold-file compile-time analysis, once per unit
namespace nift::rt::infer_detail {

bool Inference::into_lambda_readers(const TyRef* var) {
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 32
    return false;  // A32: a lambda keeps what it read of a binding that grows after its walk (U1)
#endif
    if (var < vars_.data() || var >= vars_.data() + vars_.size()) return false;
    const auto found = lambda_readers_.find(static_cast<std::size_t>(var - vars_.data()));
    if (found == lambda_readers_.end()) return false;
    for (std::size_t unit : found->second) enqueue(unit);
    return true;
}

// U1: a lambda reads the binding it captures (or a global) in its own walk;
// when that binding grows later, in another unit's walk, the lambda is walked
// again (merge_local). Without this a lambda walked before its captured
// binding's last write kept a stale kind (exit 70 on B4a and B4b alike).
TyRef* Inference::find(std::string_view name) {
    TyRef* found = find_binding(name);
    if (found) {  // F1: this walk has read the binding (declare)
        const auto slot = static_cast<std::size_t>(found - vars_.data());
        if (slot >= slot_read_.size()) slot_read_.resize(slot + 1, 0);
        slot_read_[slot] = walk_serial_;
    }
    if (found && current_ > 0 && static_cast<std::size_t>(current_) > plan_.functions.size())
        lambda_readers_[static_cast<std::size_t>(found - vars_.data())].insert(static_cast<std::size_t>(current_));
    return found;
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

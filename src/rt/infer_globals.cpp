#include "rt/infer_globals.h"

// rt-lint: cold-file compile-time analysis, once per unit

namespace nift::rt::infer_detail {

std::size_t persistent_slot(std::vector<std::size_t>& persist, std::vector<TyRef>& vars, std::size_t nodes,
                            NodeId at) {
    if (persist.empty()) persist.assign(nodes, GlobalSlots::kNone);
    std::size_t& slot = persist[at];
    if (slot == GlobalSlots::kNone) {
        slot = vars.size();
        vars.push_back(empty_ty());
    }
    return slot;
}

std::size_t GlobalSlots::find(int f, std::string_view name, std::vector<std::size_t>& persist,
                              std::vector<TyRef>& vars, std::size_t nodes) {
    if (!plan_ || f < 0 || static_cast<std::size_t>(f) >= plan_->free.size()) return kNone;
    const auto& free = plan_->free[static_cast<std::size_t>(f)];
    const auto ref = free.find(name);
    if (ref == free.end() || !ref->second.ok) return kNone;
    // The declaration's own slot: main's `declare` finds the same one.
    const std::size_t slot = persistent_slot(persist, vars, nodes, ref->second.decl);
    slots_.insert(slot);
    return slot;
}

} // namespace nift::rt::infer_detail

#include "rt/infer_session_mutation.h"
#include "rt/session_mutation_shape.h"
// rt-lint: cold-file monotone effects through possible loop-location aliases
namespace nift::rt::infer_detail {
void Inference::propagate_session_alias(std::size_t slot) {
    std::unordered_set<std::size_t> seen;
    while (true) {
        const auto alias = session_for_aliases_.find(slot);
        if (alias == session_for_aliases_.end()) return;
        const auto parent = alias->second.first;
        if (!seen.insert(slot).second) { merge_local(vars_[parent], any_ty()); return; }
        const auto updated = session_mutation_shape(ast_, alias->second.second, vars_[parent], vars_[slot]);
        merge_local(vars_[parent], updated);
        slot = parent;
    }
}
} // namespace nift::rt::infer_detail

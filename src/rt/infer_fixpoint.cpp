#include "rt/infer_fixpoint.h"

// rt-lint: cold-file compile-time analysis, once per unit (tests only: verify_fixed_point)

namespace nift::rt::infer_detail {

// B4b 22 (review of 16f24f36): the invariant every invalidation path keeps, checked directly. A
// unit is walked again whenever an input of its last walk changed (a binding, a parameter, a
// return, a site class grown, opened or merged); so after convergence one more walk of every
// unit reproduces every node's kinds. A node whose kinds change was kept stale by a missed
// re-walk (the 16f24f36 defect: a class opened by a union, its precise readers never walked).
void Inference::check_fixed_point() {
    const UnitKinds converged = result_;  // the result is the converged one, whatever the check finds
    for (std::size_t u = 0; u <= plan_.functions.size() + lambdas_.size(); ++u) walk_unit(u);
    for (NodeId id = 0; id < result_.kinds.size(); ++id)
        if (id >= converged.kinds.size() || result_.kinds[id] != converged.kinds[id]) stale_.push_back(id);
    result_ = converged;
}

} // namespace nift::rt::infer_detail

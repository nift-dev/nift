#include "rt/infer_notify.h"

#include <algorithm>

// rt-lint: cold-file compile-time analysis, once per unit

namespace nift::rt::infer_detail {

// B4b 28 (review of 4c9632b8, M-1): a dependency list's readers walk again. The result is that of
// enqueue() on every listed unit (but `skip`), in list order, exactly; the cost is the shorter of
// the list and the walks since the list's last notification. On `many` (one class read by all n
// helpers, grown by each helper's store) the whole list was scanned at every growth: ~n^2 calls
// of enqueue, nearly all finding the unit queued already.
void Inference::notify(UnitSet& readers, std::size_t skip) {
    const std::size_t now = dequeued_.empty() ? 0 : dequeued_.size() - 1;  // the unit being walked
    const std::size_t from = std::min(readers.notified, dequeued_.size());
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 37
    const bool whole = true;  // A37: every notification scans the whole list (4c9632b8: quadratic on `many`)
#else
    // A session's enqueue refuses some lambdas until their scopes are known: the whole list there.
    const bool whole = session_ || dequeued_.size() - from >= readers.size();
#endif
    if (whole) {
        for (std::size_t u : readers.list)
            if (u != skip) enqueue(u);
    } else {
        // Every reader in the list at its last notification was queued then; one not dequeued
        // since is queued still, and every reader added since was being walked when it was
        // added (dequeued at `notified` or later). So the units dequeued from `notified` on are
        // the only readers enqueue() can queue; they are queued in their list order.
        result_.work.reader_checks += dequeued_.size() - from;
        std::vector<std::size_t> at;
        for (std::size_t k = from; k < dequeued_.size(); ++k) {
            const std::size_t i = readers.index(dequeued_[k]);
            if (i != UnitSet::kNone && dequeued_[k] != skip) at.push_back(i);
        }
        std::sort(at.begin(), at.end());
        at.erase(std::unique(at.begin(), at.end()), at.end());
        for (std::size_t i : at) enqueue(readers.list[i]);
    }
    readers.notified = now;
}

} // namespace nift::rt::infer_detail

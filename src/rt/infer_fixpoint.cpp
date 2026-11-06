#include "rt/infer_fixpoint.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

// rt-lint: cold-file compile-time analysis, once per unit (tests only: verify_fixed_point)

namespace nift::rt::infer_detail {

namespace {
// The per-node facts the compiler reads besides `kinds`, compared as sets (B4b 27: the check
// covers what code generation consumes, not only the kinds).
template <class Set> void differ_set(const Set& now, const Set& then, std::vector<NodeId>& out) {
    for (NodeId id : now)
        if (!then.count(id)) out.push_back(id);
    for (NodeId id : then)
        if (!now.count(id)) out.push_back(id);
}
template <class Map> void differ_map(const Map& now, const Map& then, std::vector<NodeId>& out) {
    for (const auto& [id, v] : now) {
        const auto f = then.find(id);
        if (f == then.end() || !(f->second == v)) out.push_back(id);
    }
    for (const auto& entry : then)
        if (!now.count(entry.first)) out.push_back(entry.first);
}
} // namespace

// B4b 22 (review of 16f24f36): the invariant every invalidation path keeps, checked directly. A
// unit is walked again whenever an input of its last walk changed (a binding, a parameter, a
// return, a site class grown, opened or merged); so after convergence one more walk of every
// unit reproduces every node's kinds. A node whose kinds change was kept stale by a missed
// re-walk (the 16f24f36 defect: a class opened by a union, its precise readers never walked).
// B4b 27 (review of 4c9632b8, L-1): the check is the definition of a fixed point of the whole
// analysis state, not only of the kinds: one more walk of every unit (each with its own walk
// serial, as run() does) must grow nothing (no unit is queued and no walk sets self_changed_:
// no binding, parameter, return, site class or lambda scope changed) and must reproduce every
// per-node fact the compiler reads. It reports; it never repairs: the converged result is
// returned whatever it finds, and the gate build (NIFT_B4B_FIXPOINT_GATE) fails the compile.
void Inference::check_fixed_point() {
    const UnitKinds converged = result_;  // the result is the converged one, whatever the check finds
    const std::size_t units = units_count();
    for (std::size_t u = 0; u < units; ++u) {
        self_changed_ = false;
        ++walk_serial_;
        dequeued_.push_back(u);  // the bookkeeping of run() (notify)
        walk_unit(u);
        if (self_changed_) state_changed_.push_back("unit " + std::to_string(u) + " grew in its own walk");
    }
    if (!queue_.empty()) {
        std::string units;
        for (std::size_t u : queue_) units += " " + std::to_string(u);
        state_changed_.push_back(std::to_string(queue_.size()) + " unit(s) queued again:" + units + " (of " +
                                 std::to_string(units_count()) + ")");
    }
    if (plan_.functions.size() + 1 + lambdas_.size() != units) state_changed_.push_back("new lambda units");
    for (NodeId id = 0; id < result_.kinds.size(); ++id)
        if (id >= converged.kinds.size() || result_.kinds[id] != converged.kinds[id]) stale_.push_back(id);
    differ_map(result_.elements, converged.elements, stale_);
    differ_map(result_.callable_unknown, converged.callable_unknown, stale_);
    differ_map(result_.struct_ids, converged.struct_ids, stale_);
    differ_map(result_.bindings, converged.bindings, stale_);
    differ_map(result_.collection_elements, converged.collection_elements, stale_);
    for (const auto& [id, v] : result_.callable_targets) {
        auto a = v;
        std::sort(a.begin(), a.end());
        const auto f = converged.callable_targets.find(id);
        auto b = f == converged.callable_targets.end() ? std::vector<NodeId>{} : f->second;
        std::sort(b.begin(), b.end());
        if (a != b) stale_.push_back(id);
    }
    differ_set(result_.collection_lambdas, converged.collection_lambdas, stale_);
    differ_set(result_.collection_objects, converged.collection_objects, stale_);
    differ_set(result_.collection_arrays, converged.collection_arrays, stale_);
    differ_set(result_.collection_elements_of, converged.collection_elements_of, stale_);
    differ_set(result_.collection_holders, converged.collection_holders, stale_);
    differ_set(result_.collection_inspections, converged.collection_inspections, stale_);
    differ_set(result_.collection_text_numbers, converged.collection_text_numbers, stale_);
    differ_set(result_.nested_aggregates, converged.nested_aggregates, stale_);
    differ_set(result_.future_holders, converged.future_holders, stale_);
    std::sort(stale_.begin(), stale_.end());
    stale_.erase(std::unique(stale_.begin(), stale_.end()), stale_.end());
    result_ = converged;
}

} // namespace nift::rt::infer_detail

namespace nift::rt {
// B4b 27: the fixed-point gate build (NIFT_B4B_FIXPOINT_GATE, a TEST configuration that never
// ships: tests/runtime2/b4b/fixpoint.py): any non-fixed point fails the compile loudly, with
// the positions of the nodes whose facts the extra walk changed. Exit 86 (never a program's).
void fixpoint_gate_fail(const Ast& ast, const std::vector<NodeId>& stale, const std::vector<std::string>& state,
                        bool session) {
    std::fprintf(stderr, "B4B-FIXPOINT: not a fixed point (%zu node(s), %zu state change(s), %s):", stale.size(),
                 state.size(), session ? "session" : "unit");
    for (const std::string& s : state) std::fprintf(stderr, " [%s]", s.c_str());
    std::size_t k = 0;
    for (NodeId id : stale) {
        if (k++ == 8) break;
        const Node& n = ast.at(id);
        std::uint32_t line = 1, col = 1;
        const std::string_view src = ast.source();
        for (std::uint32_t i = 0; i < n.begin && i < src.size(); ++i)
            if (src[i] == '\n') ++line, col = 1;
            else ++col;
        const auto t = ast.text(n);
        std::fprintf(stderr, " %u:%u[%.*s]", line, col, static_cast<int>(t.size() > 40 ? 40 : t.size()), t.data());
    }
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
    std::_Exit(86);
}
} // namespace nift::rt

#include "rt/infer_escape.h"

// rt-lint: cold-file compile-time analysis, once per unit

namespace nift::rt::infer_detail {

// C4: a function value passed to code the inference does not walk (a native
// that is not a collection method) may be called with anything: its
// parameters are anything.
void Inference::escape_callables(const std::vector<TyRef>& values) {
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 23
    return;  // A23: an escaping function keeps the parameter types of its known calls
#endif
    const std::size_t n = plan_.functions.size();
    for (const TyRef& v : values)
        for (std::uint32_t unit : v->callables) {
            if (!escaped_units_.insert(unit).second) continue;
            bool grew = false;
            if (unit < n) for (TyRef& p : params_[unit]) grew = merge(p, top_ty()) || grew;
            else if (unit > n && unit - n - 1 < lambdas_.size())
                for (TyRef& p : lambdas_[unit - n - 1].params) grew = merge(p, top_ty()) || grew;
            if (grew) enqueue(unit);
        }
}

// C4 (review H1): a call whose targets the inference does not know exactly
// (any value, an unknown kind, an untracked callable): any function used as a
// value may be the callee, with any arguments, now or later in the walk
// (untracked_seen_: record() widens a function first used as a value after).
void Inference::untracked_call() {
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 28
    return;  // A28: an inexact callee does not widen its possible targets (H1)
#endif
    untracked_seen_ = true;
    // B4b 28: every unit of value_units_ (a growing set) has been escaped by an earlier call
    // already (escape_callables escapes each unit once): nothing to do again.
    if (value_units_.size() == untracked_escaped_) return;
    untracked_escaped_ = value_units_.size();
    auto as_value = std::make_shared<Ty>();
    for (std::size_t unit : value_units_) as_value->callables.push_back(static_cast<std::uint32_t>(unit));
    escape_callables({as_value});
}

bool Inference::exact_callee(const TyRef& f) const {
    return f && !f->any && !(f->kinds & kKindUnknown) && !f->untracked_callable && !f->callables.empty();
}

// H1: `thread(f, ...)` and every runtime-invoked entry: an inexact `f` widens
// every function that may be it.
void Inference::thread_target(const TyRef& f) {
    if (!exact_callee(f)) untracked_call();
}

// C5 (review M1): a call into code compiled by an earlier session statement
// may store anything into every container that code can reach: the call's
// arguments and receiver, everything stored by any path (assignment, push,
// index or member store, at any depth) into an earlier statement's binding,
// and everything stored later into such a container (site_store). A function
// value made reachable may capture anything of this statement: then every
// container of the statement is reachable. Once earlier code is called in
// this statement (flow-insensitively), every reachable class reads as anything.
void Inference::expose(const TyRef& t, int depth) {
    if (!t) return;
    if (t->any || !t->callables.empty() || (t->kinds & kKindFunction)) {
        if (!closure_exposed_) closure_exposed_ = true, session_open();
        if (t->any) return;
    }
    if (depth > 8) return;
    bool open = false;
    expose_sites(classes(t->sites, open));
    expose(t->elem, depth + 1);
    for (const auto& entry : t->fields) expose(entry.second, depth + 1);
    for (const auto& item : t->items) expose(item, depth + 1);
}

void Inference::expose_sites(const std::vector<std::uint32_t>& sites) {
    for (std::uint32_t s : sites) {
        if (!exposed_.insert(s).second) continue;
        self_changed_ = true;  // the fixpoint re-walks: stores into it are exposed too
        const auto c = agg_.find(s);
        if (c != agg_.end()) {
            const SiteContents contents = c->second;
            expose(contents.values, 1);
            for (const auto& entry : contents.named) expose(entry.second, 1);
        }
    }
    session_open();
}

void Inference::session_open() {
    if (!earlier_called_) return;
    if (closure_exposed_ && !agg_unknown_) { agg_unknown_ = true; site_grew(); return; }
    bool grew = false;
    for (std::uint32_t s : exposed_) grew = opened_.insert(rep(s)).second || grew;
    if (grew) site_grew();
}

void Inference::session_store(std::string_view name, const TyRef* var, const TyRef& value) {
    if (!session_) return;
    const auto external = external_.find(name);
    if (external != external_.end() && &vars_[external->second] == var) expose(value);
}

TyRef Inference::session_effects(const TyRef& result, const Node& call) {
#if !(defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 21)
    if (!result) return result;
    earlier_called_ = true;
    const NodeId* args = ast_.list(call);
    for (std::uint32_t k = 0; k < call.count; ++k) expose(expr(args[k]));
    if (ast_.at(call.a).kind == NodeKind::Member) expose(expr(ast_.at(call.a).a));
    session_open();
#else
    (void)call;
#endif
    return result;
}

} // namespace nift::rt::infer_detail

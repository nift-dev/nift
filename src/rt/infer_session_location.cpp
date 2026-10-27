#include "rt/infer_session_location.h"
#include "rt/compiler.h"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
// rt-lint: cold-file location effects inside one callable-ID lifetime; no runtime value reads
namespace nift::rt::infer_detail {
struct SessionLocationInference {
    std::map<std::uintptr_t, TyRef> bounds;
    std::map<std::uintptr_t, std::vector<std::size_t>> slots;
    std::vector<SessionLocationEdge> edges;
    bool limit = false;
};
namespace {
bool grow(TyRef& into, const TyRef& value) {
    const auto next = unite(into, value);
    if (same(into, next)) return false;
    into = next; return true;
}
TyRef select(const TyRef& parent, const SessionLocationEdge& edge) {
    if (!parent) return empty_ty();
    if (edge.element) return parent->elem;
    if (!edge.member) return parent;
    const auto found = parent->fields.find(edge.field);
    return found == parent->fields.end() ? empty_ty() : found->second;
}
TyRef enclose(const TyRef& child, const SessionLocationEdge& edge) {
    if (!edge.element && !edge.member) return child;
    auto type = std::make_shared<Ty>();
    if (edge.element) { type->kinds = kKindArray; type->elem = child; }
    else { type->kinds = kKindObject; type->fields.emplace(edge.field, child); }
    return type;
}
}
void Inference::session_begin_locations() {
    if (!session_ || session_->capture_locations.empty()) return;
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 13
    return;
#endif
    session_locations_ = std::make_shared<SessionLocationInference>();
    std::set<std::tuple<std::uintptr_t,std::uintptr_t,bool,bool,std::string>> seen;
    const auto add = [&](const SessionCaptureLocation& location) {
        for (const auto& edge : location.edges)
            if (seen.emplace(edge.child,edge.parent,edge.element,edge.member,edge.field).second)
                session_locations_->edges.push_back(edge);
    };
    for (const auto& capture : session_->capture_locations) for (const auto& location : capture.second) add(location);
    for (const auto& global : session_->global_locations) add(global.second);
}
void Inference::session_location_slot(std::uintptr_t key, std::size_t slot) {
    if (!session_locations_ || !key) return;
    auto& slots = session_locations_->slots[key];
    if (std::find(slots.begin(),slots.end(),slot)!=slots.end()) return;
    slots.push_back(slot);
    if (session_sync_locations()) {
        self_changed_ = true;
        for (std::size_t unit=0;unit<=plan_.functions.size()+lambdas_.size();++unit) enqueue(unit);
    }
}
bool Inference::session_sync_locations() {
    if (!session_locations_) return false;
    auto& state=*session_locations_;
    bool any=false;
    for (unsigned round=0;round<64;++round) {
        bool changed=false;
        for (const auto& group:state.slots)
            for (auto slot:group.second) changed=grow(state.bounds[group.first],vars_[slot])||changed;
        for (const auto& edge:state.edges) {
            changed=grow(state.bounds[edge.child],select(state.bounds[edge.parent],edge))||changed;
            if (state.bounds[edge.child])
                changed=grow(state.bounds[edge.parent],enclose(state.bounds[edge.child],edge))||changed;
        }
        for (const auto& group:state.slots)
            for (auto slot:group.second) changed=grow(vars_[slot],state.bounds[group.first])||changed;
        any=any||changed;
        if (!changed) return any;
    }
    state.limit=true; return any;
}
bool Inference::session_locations_converged() const { return !session_locations_ || !session_locations_->limit; }
}

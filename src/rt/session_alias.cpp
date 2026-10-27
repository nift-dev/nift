#include "rt/session_alias.h"
#include "rt/session_shape.h"
#include <map>
// rt-lint: cold-file transactional closure-location bound fixpoint before code generation
namespace nift::rt {
namespace {
using namespace infer_detail;
using Bounds = std::map<std::uintptr_t, TyRef>;
bool grow(TyRef& into, const TyRef& value) {
    const auto next = unite(into, value);
    if (into && same(into, next)) return false;
    into = next;
    return true;
}
TyRef selected(const TyRef& parent, const SessionLocationEdge& edge) {
    if (!parent) return empty_ty();
    if (edge.element) return parent->elem;
    if (!edge.member) return parent;
    const auto field = parent->fields.find(edge.field);
    return field == parent->fields.end() ? empty_ty() : field->second;
}
TyRef enclosing(const TyRef& child, const SessionLocationEdge& edge) {
    if (!edge.element && !edge.member) return child;
    auto out = std::make_shared<Ty>();
    if (edge.element) { out->kinds = kKindArray; out->elem = child; }
    else { out->kinds = kKindObject; out->fields.emplace(edge.field, child); }
    return out;
}
}
UnitKinds infer_session_aliases(const Ast& ast, NodeId root, const UnitPlan& plan, std::size_t walks,
                               const GlobalPlan* globals, SessionCompileContext* session) {
    if (!session || !session->coherent_recompile)
        return infer_unit_kinds(ast, root, plan, walks, globals, session);
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 13
    return infer_unit_kinds(ast, root, plan, walks, globals, session);
#endif
    // Growing inference seeds must not replace the original admission bounds.
    // Code generation sees the original private transaction; successful export
    // publishes the final kinds only after every guard and compiler pass succeeds.
    SessionCompileContext seeds = *session;
    session = &seeds;
    Bounds bounds;
    std::vector<SessionLocationEdge> edges;
    const auto add = [&](const SessionCaptureLocation& location, const infer_detail::TyRef& type) {
        if (!location.key) return;
        grow(bounds[location.key], session_shape(type));
        edges.insert(edges.end(), location.edges.begin(), location.edges.end());
    };
    for (const auto& capture : session->capture_locations) {
        const auto initializer = session->capture_initializers.at(capture.first);
        for (const auto& location : capture.second) add(location, session->capture_seeds.at(initializer));
    }
    for (const auto& symbol : session->symbols) {
        const auto location = session->global_locations.find(symbol.name);
        if (location != session->global_locations.end()) add(location->second, symbol.structure);
    }
    // Every iteration is cold, private compilation. No source effects or
    // published metadata change until code generation also succeeds.
    UnitKinds result;
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
        result = infer_unit_kinds(ast, root, plan, walks, globals, session);
        if (!result.converged) return result;
        bool changed = false;
        for (const auto& capture : session->capture_locations) {
            const auto shape = result.session_decl_shapes.find(capture.first);
            if (shape != result.session_decl_shapes.end())
                for (const auto& location : capture.second) if (location.key)
                    changed = grow(bounds[location.key], session_shape(shape->second)) || changed;
        }
        for (const auto& binding : result.binding_shapes) {
            const auto location = session->global_locations.find(binding.first);
            if (location != session->global_locations.end() && location->second.key)
                changed = grow(bounds[location->second.key], session_shape(binding.second)) || changed;
        }
        for (const auto& edge : edges) {
            changed = grow(bounds[edge.child], selected(bounds[edge.parent], edge)) || changed;
            if (bounds[edge.child])
                changed = grow(bounds[edge.parent], enclosing(bounds[edge.child], edge)) || changed;
        }
        for (const auto& capture : session->capture_locations) {
            auto& seed = session->capture_seeds.at(session->capture_initializers.at(capture.first));
            for (const auto& location : capture.second) if (location.key)
                changed = grow(seed, bounds[location.key]) || changed;
        }
        for (auto& symbol : session->symbols) {
            const auto location = session->global_locations.find(symbol.name);
            if (location != session->global_locations.end() && location->second.key)
                changed = grow(symbol.structure, bounds[location->second.key]) || changed;
        }
        if (!changed) return result;
    }
    result.converged = false;
    return result;
}
}

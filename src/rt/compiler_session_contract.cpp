#include "rt/compiler_session_contract.h"
#include "rt/session_shape.h"
// rt-lint: cold-file persistent root/capture contract admission, including nested effects
namespace nift::rt::detail {
SessionContractRefusal session_shape_contract(const SessionCompileContext& session, const UnitKinds& kinds) {
    bool widen_roots = session.coherent_recompile, widen_captures = session.coherent_recompile;
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 7
    widen_roots = false;
#elif defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 8
    widen_captures = false;
#endif
    for (const auto& binding : kinds.bindings) {
        const auto shape = kinds.session_shapes.find(binding.first);
        if (shape == kinds.session_shapes.end()) continue;
        const auto capture = session.capture_contracts.find(binding.second);
        if (capture != session.capture_contracts.end() && !session_shape_within(shape->second, capture->second) &&
            !(widen_captures && !(shape->second->kinds & ~capture->second->kinds)))
            return {binding.first, "session:capture-contract"};
#if !defined(NIFT_SESSION_MUTANT) || NIFT_SESSION_MUTANT != 7
        for (const auto& symbol : session.symbols) {
            const auto external = session.declarations.find(symbol.name);
            if (external == session.declarations.end() || external->second != binding.second) continue;
            if (!session_shape_within(shape->second, symbol.structure) &&
                !(widen_roots && !(shape->second->kinds & ~symbol.contract)))
                return {binding.first, "session:binding-shape-contract"};
        }
#endif
    }
    return {};
}
} // namespace nift::rt::detail

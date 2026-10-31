#include "rt/session_alias.h"
// rt-lint: cold-file compatibility entry; storage effects now share the inference callable-ID lifetime
namespace nift::rt {
UnitKinds infer_session_aliases(const Ast& ast, NodeId root, const UnitPlan& plan, const GlobalPlan* globals,
                               const CompileLimits& limits) {
    return infer_unit_kinds(ast,root,plan,limits.kind_walks,globals,limits.session,limits.invocation);
}
}
